#include "shashoku/render.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "html/dom.hpp"
#include "html/parser.hpp"
#include "layout/box_tree.hpp"
#include "layout/layout.hpp"
#include "linebreak/line_breaker.hpp"
#include "paint/display_list_builder.hpp"
#include "png/png.hpp"
#include "raster/display_list.hpp"
#include "raster/rasterizer.hpp"
#include "style/computed_style.hpp"
#include "style/resolver.hpp"
#include "text/font_store.hpp"
#include "text/freetype_glyph_source.hpp"
#include "text/shaper.hpp"

// 公開 API（ARCHITECTURE.md §3.10）: 6 段のパイプラインをここで 1 本に繋ぐ。
//
//   ① html::parse → ② style::resolve → ③ layout::layout → ⑤a paint::build_display_list
//   → ⑤b raster::rasterize → ⑥ png::encode
//
// 純粋関数（DESIGN.md §3-5）: この翻訳単位に可変のグローバル変数・静的キャッシュはない。
// FontStore も画像テーブルも render() の呼び出しごとに作って捨てる。
namespace shashoku {
namespace {

// 入力の上限（A25）は RenderLimits に一本化してある。各モジュールは自分の既定値を
// 定数で持っているが、api は必ず RenderLimits の値を渡すので、両者が食い違っていないことを
// ここで機械的に確かめる（既定値が 2 か所に分かれて別々に動くのを防ぐ）。
static_assert(RenderLimits{}.nesting_depth == html::kMaxNestingDepth,
              "RenderLimits::nesting_depth と html::kMaxNestingDepth の既定値が食い違っている");
static_assert(RenderLimits{}.style_rules == style::kMaxStyleRules,
              "RenderLimits::style_rules と style::kMaxStyleRules の既定値が食い違っている");
static_assert(RenderLimits{}.image_pixels == png::kMaxPixels,
              "RenderLimits::image_pixels と png::kMaxPixels の既定値が食い違っている");
static_assert(RenderLimits{}.device_pixels == raster::kMaxDevicePixels,
              "RenderLimits::device_pixels と raster::kMaxDevicePixels の既定値が食い違っている");

// ---------------------------------------------------------------------------
// オプションの検証と変換
// ---------------------------------------------------------------------------

linebreak::Strictness to_internal(LineBreakStrictness strictness) {
  switch (strictness) {
    case LineBreakStrictness::Normal:
      return linebreak::Strictness::Normal;
    case LineBreakStrictness::Loose:
      return linebreak::Strictness::Loose;
    case LineBreakStrictness::Strict:
      break;
  }
  return linebreak::Strictness::Strict;
}

linebreak::OverflowPolicy to_internal(OverflowPolicy policy) {
  switch (policy) {
    case OverflowPolicy::Oikomi:
      return linebreak::OverflowPolicy::Oikomi;
    case OverflowPolicy::Burasage:
      return linebreak::OverflowPolicy::Burasage;
    case OverflowPolicy::Oidashi:
      break;
  }
  return linebreak::OverflowPolicy::Oidashi;
}

linebreak::Config to_internal(const LineBreakConfig& config) {
  linebreak::Config out;
  out.strictness = to_internal(config.strictness);
  out.overflow = to_internal(config.overflow);
  // break_anywhere は CSS の overflow-wrap から layout が決める（ここでは既定のまま）。
  out.collapse_punctuation_spacing = config.collapse_punctuation_spacing;
  out.trim_line_end = config.trim_line_end;
  out.trim_line_start = config.trim_line_start;
  out.extra_line_start_prohibited = config.extra_line_start_prohibited;
  out.extra_line_end_prohibited = config.extra_line_end_prohibited;
  return out;
}

Result<void> validate(const RenderOptions& options) {
  if (options.viewport_width <= 0) {
    return fail(ErrorKind::InvalidOption,
                std::format("viewport width must be positive (got {})", options.viewport_width));
  }
  if (options.viewport_height && *options.viewport_height <= 0) {
    return fail(ErrorKind::InvalidOption,
                std::format("viewport height must be positive (got {})", *options.viewport_height));
  }
  if (!std::isfinite(options.scale) || options.scale <= 0) {
    return fail(ErrorKind::InvalidOption,
                std::format("scale must be a positive finite number (got {})", options.scale));
  }
  if (options.scale > options.limits.scale) {
    return fail(ErrorKind::LimitExceeded, std::format("scale is {}, which exceeds the limit of {} "
                                                      "(raise RenderLimits::scale to allow it)",
                                                      options.scale, options.limits.scale));
  }
  return {};
}

// ---------------------------------------------------------------------------
// 入力の上限（ARCHITECTURE.md A25）
//
// 検査する場所は 3 つ:
//   (a) 入力を受けた時点（バイト数・枚数。パースより前）
//       … check_input_limits
//   (b) 計算値化のあと（ノード数・文字数・font-size）
//       … check_dom_limits / check_computed_limits
//   (c) 大きな確保の直前（画像のデコード、出力ビットマップ）
//       … png::decode / raster::rasterize に上限を渡す
//
// どの判定もサイズと個数だけを見る（時間やメモリの実測は見ない）ので決定的。
// ---------------------------------------------------------------------------

// (a) 入力を受けた時点。パースもデコードもする前に弾く。
Result<void> check_input_limits(std::string_view html, const ImageSet& images,
                                const RenderLimits& limits) {
  if (html.size() > limits.html_bytes) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the HTML input is {} bytes, which exceeds the limit of {} "
                            "(raise RenderLimits::html_bytes to allow it)",
                            html.size(), limits.html_bytes));
  }
  if (images.size() > limits.images) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the ImageSet has {} images, which exceeds the limit of {} "
                            "(raise RenderLimits::images to allow it)",
                            images.size(), limits.images));
  }
  return {};
}

// 正しい UTF-8 の継続バイト（10xxxxxx）以外を数えればコードポイント数になる。
// html::parse() が UTF-8 の妥当性を保証済みなので、ここで検証はしない。
std::size_t count_code_points(std::string_view text) {
  std::size_t count = 0;
  for (const char c : text) {
    if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
      ++count;
    }
  }
  return count;
}

// (b) DOM のノード数とテキストの総コードポイント数。合成ルート `#root` は数えない。
// `<style>` の中身は「組む対象のテキスト」ではないのでコードポイントには数えない
// （その量は html_bytes と style_rules が押さえる）。
//
// 木は 1 回だけ前順に辿り、超過したノードの位置を覚えておいて総数と一緒に報告する
// （「いくつに対していくつだったか」を出すため。木はもう全部メモリにある）。
Result<void> check_dom_limits(const html::Node& root, const RenderLimits& limits) {
  struct Frame {
    const html::Node* node = nullptr;
    bool in_style = false;
  };

  std::vector<Frame> stack;
  const auto push_children = [&stack](const html::Node& node, bool in_style) {
    for (std::size_t i = node.children.size(); i > 0; --i) {
      stack.push_back(Frame{.node = &node.children[i - 1], .in_style = in_style});
    }
  };

  std::size_t nodes = 0;
  std::size_t code_points = 0;
  std::optional<SourceLocation> node_overflow;
  std::optional<SourceLocation> text_overflow;
  push_children(root, false);
  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();
    const html::Node& node = *frame.node;

    ++nodes;
    if (nodes == limits.dom_nodes + 1 && !node_overflow) {
      node_overflow = node.location;
    }
    if (node.type == html::Node::Type::Text) {
      if (!frame.in_style) {
        const std::size_t before = code_points;
        code_points += count_code_points(node.text);
        if (before <= limits.text_code_points && code_points > limits.text_code_points &&
            !text_overflow) {
          text_overflow = node.location;
        }
      }
      continue;
    }
    push_children(node, node.tag == "style");
  }

  if (node_overflow) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the document has {} nodes, which exceeds the limit of {} "
                            "(raise RenderLimits::dom_nodes to allow it)",
                            nodes, limits.dom_nodes),
                node_overflow);
  }
  if (text_overflow) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the document has {} text code points, which exceeds the limit of {} "
                            "(raise RenderLimits::text_code_points to allow it)",
                            code_points, limits.text_code_points),
                text_overflow);
  }
  return {};
}

// (b) 計算値化のあと。font-size はここで初めて px に解決される（`<rt>` の 50% も含む）。
// グリフのビットマップは pixel_size = font-size * scale の 2 乗で大きくなるので、
// 出力画像が小さくても font-size だけで数 GB を確保しうる（issue #6 の実測）。
Result<void> check_computed_limits(const style::StyledNode& root, float scale,
                                   const RenderLimits& limits) {
  std::vector<const style::StyledNode*> stack{&root};
  while (!stack.empty()) {
    const style::StyledNode& node = *stack.back();
    stack.pop_back();
    if (node.type == style::StyledNode::Type::Element) {
      const float device_px = node.style.font_size * scale;
      if (device_px > limits.font_size_device_px) {
        return fail(ErrorKind::LimitExceeded,
                    std::format("font-size {} px x scale {} = {} device px, which exceeds the "
                                "limit of {} (raise RenderLimits::font_size_device_px to allow it)",
                                node.style.font_size, scale, device_px, limits.font_size_device_px),
                    node.location);
      }
    }
    for (std::size_t i = node.children.size(); i > 0; --i) {
      stack.push_back(&node.children[i - 1]);
    }
  }
  return {};
}

// ---------------------------------------------------------------------------
// フォントと画像の読み込み
// ---------------------------------------------------------------------------

// フォント実体と画像ピクセルの持ち主（DESIGN.md §3-2）。Shaper / FreeTypeGlyphSource は
// FontStore を参照するだけなので、これが一番長生きする必要がある。
struct Resources {
  text::FontStore fonts;
  std::vector<Bitmap> images;      // 添字がそのまま ImageId
  std::vector<std::string> names;  // images と同じ長さ・同じ順
};

Result<Resources> load_resources(const FontSet& fonts, const ImageSet& images,
                                 const RenderLimits& limits) {
  if (fonts.empty()) {
    return fail(ErrorKind::NoFonts, "no fonts were given: add at least one font to the FontSet");
  }
  Resources resources;
  for (std::size_t i = 0; i < fonts.size(); ++i) {
    const Result<FontId> id = resources.fonts.load(fonts.at(i));
    if (!id) {
      return fail(ErrorKind::FontLoad, std::format("font #{}: {}", i, id.error().message));
    }
  }
  // 名前の重複はバイト列を見る前に弾く（ImageSet の形の問題で、中身の問題ではない）。
  for (std::size_t i = 0; i < images.size(); ++i) {
    for (std::size_t seen = 0; seen < i; ++seen) {
      if (images.name(seen) == images.name(i)) {
        return fail(ErrorKind::InvalidOption,
                    std::format("image \"{}\" was added to the ImageSet twice", images.name(i)));
      }
    }
  }
  // (c) 大きな確保の直前。png::decode は IHDR を読んだ時点（画素を確保する前）に判定するので、
  // 「巨大な寸法を名乗るだけの小さな PNG」でもメモリは 1 バイトも増えない。
  // 1 枚あたりの上限と合計の上限のうち、いま効いている方をそのまま渡す。
  std::uint64_t total_pixels = 0;
  for (std::size_t i = 0; i < images.size(); ++i) {
    const std::string_view name = images.name(i);
    const std::uint64_t remaining =
        limits.total_image_pixels > total_pixels ? limits.total_image_pixels - total_pixels : 0;
    const bool per_image_binds = limits.image_pixels <= remaining;
    const std::uint64_t cap = per_image_binds ? limits.image_pixels : remaining;

    Result<Bitmap> bitmap = png::decode(images.bytes(i), cap);
    if (!bitmap) {
      if (bitmap.error().kind == ErrorKind::LimitExceeded) {
        return fail(ErrorKind::LimitExceeded,
                    std::format("image \"{}\": {} (raise RenderLimits::{} to allow it)", name,
                                bitmap.error().message,
                                per_image_binds ? "image_pixels" : "total_image_pixels"));
      }
      return fail(ErrorKind::ImageDecode,
                  std::format("image \"{}\": {}", name, bitmap.error().message));
    }
    total_pixels += std::uint64_t{bitmap->width} * bitmap->height;
    resources.images.push_back(std::move(*bitmap));
    resources.names.emplace_back(name);
  }
  return resources;
}

// ---------------------------------------------------------------------------
// パイプライン
// ---------------------------------------------------------------------------

Result<layout::BoxTree> run_layout(const style::StyledNode& styled, const RenderOptions& options,
                                   text::TextMeasurer& measurer, const Resources& resources) {
  layout::Options layout_options;
  layout_options.viewport_width = static_cast<float>(options.viewport_width);
  if (options.viewport_height) {
    layout_options.viewport_height = static_cast<float>(*options.viewport_height);
  }
  layout_options.line_break = to_internal(options.line_break);

  // 名前 → 画像（A12）。追加順の線形探索: 決定的で、数十枚までなら十分速い。
  const layout::ImageLookup lookup =
      [&resources](std::string_view src) -> std::optional<layout::ImageInfo> {
    for (std::size_t i = 0; i < resources.names.size(); ++i) {
      if (resources.names[i] == src) {
        return layout::ImageInfo{static_cast<ImageId>(i),
                                 static_cast<float>(resources.images[i].width),
                                 static_cast<float>(resources.images[i].height)};
      }
    }
    return std::nullopt;
  };
  return layout::layout(styled, layout_options, measurer, lookup);
}

// 出力の高さ（CSS px）。viewport_height があればそれ、なければ内容の高さの切り上げ。
Result<float> output_height(const layout::BoxTree& tree, const RenderOptions& options) {
  if (options.viewport_height) {
    return static_cast<float>(*options.viewport_height);
  }
  if (tree.writing_mode != layout::WritingMode::HorizontalTb) {
    // 縦書きでは内容の伸びる向きが横なので、高さは指定してもらうしかない（§3.8 と同じ規則）。
    return fail(ErrorKind::InvalidOption, "viewport height is required in vertical writing mode");
  }
  const float height = std::ceil(tree.content_block_size());
  if (!(height > 0)) {
    return fail(ErrorKind::InvalidOption,
                "nothing to render: the content height is 0 and no viewport height was given");
  }
  return height;
}

std::vector<Warning> to_warnings(std::vector<text::MissingGlyph> missing) {
  // 報告順を入力の出現順でなくコードポイント昇順に固定する（DESIGN.md §3-5 の決定性）。
  std::sort(missing.begin(), missing.end(),
            [](const text::MissingGlyph& a, const text::MissingGlyph& b) { return a.cp < b.cp; });
  std::vector<Warning> warnings;
  warnings.reserve(missing.size());
  for (const text::MissingGlyph& glyph : missing) {
    warnings.push_back(Warning{
        WarningKind::MissingGlyph,
        std::format("no font has a glyph for U+{:04X}", static_cast<std::uint32_t>(glyph.cp)),
        glyph.cp});
  }
  return warnings;
}

Result<RenderResult> render_impl(std::string_view html, const FontSet& fonts,
                                 const ImageSet& images, const RenderOptions& options) {
  if (const Result<void> ok = validate(options); !ok) {
    return std::unexpected(ok.error());
  }
  if (const Result<void> ok = check_input_limits(html, images, options.limits); !ok) {
    return std::unexpected(ok.error());
  }
  const Result<html::Node> dom = html::parse(html, options.limits.nesting_depth);
  if (!dom) {
    return std::unexpected(dom.error());
  }
  if (const Result<void> ok = check_dom_limits(*dom, options.limits); !ok) {
    return std::unexpected(ok.error());
  }
  const Result<style::StyledNode> styled = style::resolve(*dom, options.limits.style_rules);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (const Result<void> ok = check_computed_limits(*styled, options.scale, options.limits); !ok) {
    return std::unexpected(ok.error());
  }
  Result<Resources> resources = load_resources(fonts, images, options.limits);
  if (!resources) {
    return std::unexpected(resources.error());
  }

  text::Shaper shaper(resources->fonts);
  const Result<layout::BoxTree> tree = run_layout(*styled, options, shaper, *resources);
  if (!tree) {
    return std::unexpected(tree.error());
  }
  const Result<float> height = output_height(*tree, options);
  if (!height) {
    return std::unexpected(height.error());
  }

  const raster::DisplayList list = paint::build_display_list(*tree);
  const raster::Target target{.width = static_cast<float>(options.viewport_width),
                              .height = *height,
                              .scale = options.scale,
                              .background = kTransparent,
                              .max_device_pixels = options.limits.device_pixels};
  text::FreeTypeGlyphSource glyphs(resources->fonts);
  const Result<Bitmap> bitmap = raster::rasterize(list, target, glyphs, resources->images);
  if (!bitmap) {
    if (bitmap.error().kind == ErrorKind::LimitExceeded) {
      return fail(ErrorKind::LimitExceeded,
                  std::format("{} (raise RenderLimits::device_pixels to allow it)",
                              bitmap.error().message));
    }
    return std::unexpected(bitmap.error());
  }
  Result<std::vector<std::uint8_t>> encoded = png::encode(*bitmap);
  if (!encoded) {
    return std::unexpected(encoded.error());
  }

  RenderResult result;
  result.png = std::move(*encoded);
  result.warnings = to_warnings(shaper.take_missing_glyphs());
  result.width = static_cast<int>(bitmap->width);
  result.height = static_cast<int>(bitmap->height);
  return result;
}

Result<std::string> dump_impl(std::string_view html, const FontSet& fonts, const ImageSet& images,
                              const RenderOptions& options, DumpStage stage) {
  if (const Result<void> ok = validate(options); !ok) {
    return std::unexpected(ok.error());
  }
  if (const Result<void> ok = check_input_limits(html, images, options.limits); !ok) {
    return std::unexpected(ok.error());
  }
  const Result<html::Node> dom = html::parse(html, options.limits.nesting_depth);
  if (!dom) {
    return std::unexpected(dom.error());
  }
  if (const Result<void> ok = check_dom_limits(*dom, options.limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (stage == DumpStage::Dom) {
    return html::dump_json(*dom);
  }
  const Result<style::StyledNode> styled = style::resolve(*dom, options.limits.style_rules);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (const Result<void> ok = check_computed_limits(*styled, options.scale, options.limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (stage == DumpStage::Style) {
    return style::dump_json(*styled);
  }

  Result<Resources> resources = load_resources(fonts, images, options.limits);
  if (!resources) {
    return std::unexpected(resources.error());
  }
  text::Shaper shaper(resources->fonts);
  const Result<layout::BoxTree> tree = run_layout(*styled, options, shaper, *resources);
  if (!tree) {
    return std::unexpected(tree.error());
  }
  if (stage == DumpStage::Box) {
    return layout::dump_json(*tree);
  }

  const raster::DisplayList list = paint::build_display_list(*tree);
  if (stage == DumpStage::DisplayList) {
    return paint::dump_json(list);
  }
  const Result<float> height = output_height(*tree, options);
  if (!height) {
    return std::unexpected(height.error());
  }
  return paint::dump_svg(list, static_cast<float>(options.viewport_width), *height);
}

// ---------------------------------------------------------------------------
// メモリ不足（ARCHITECTURE.md A26）
// ---------------------------------------------------------------------------

// 「例外を投げない・捕まえない」（ARCHITECTURE.md §2）の**唯一の例外規定**。
// 公開関数の境界でだけ std::bad_alloc / std::length_error を捕まえ、OutOfMemory にして返す。
// 内部では従来どおり例外を使わず、失敗は Result<T> で返す。
//
// これは最善努力である: Linux の既定のオーバーコミットでは、確保そのものは成功して
// あとから OOM killer に殺されるので bad_alloc が来ないことがある。メモリの保証は
// RenderLimits の側で行う（limits.hpp）。
template <class T, class Body>
std::expected<T, RenderError> catch_out_of_memory(Body&& body) {
  try {
    return std::forward<Body>(body)();
  } catch (const std::bad_alloc&) {
    return std::unexpected(RenderError{.kind = ErrorKind::OutOfMemory,
                                       .message = "out of memory (std::bad_alloc)",
                                       .location = std::nullopt});
  } catch (const std::length_error&) {
    return std::unexpected(
        RenderError{.kind = ErrorKind::OutOfMemory,
                    .message = "out of memory (a container exceeded its maximum size)",
                    .location = std::nullopt});
  }
}

}  // namespace

std::expected<RenderResult, RenderError> render(std::string_view html, const FontSet& fonts,
                                                const RenderOptions& opts) {
  return catch_out_of_memory<RenderResult>(
      [&] { return render_impl(html, fonts, ImageSet{}, opts); });
}

std::expected<RenderResult, RenderError> render(std::string_view html, const FontSet& fonts,
                                                const ImageSet& images, const RenderOptions& opts) {
  return catch_out_of_memory<RenderResult>([&] { return render_impl(html, fonts, images, opts); });
}

std::expected<std::string, RenderError> dump(std::string_view html, const FontSet& fonts,
                                             const ImageSet& images, const RenderOptions& opts,
                                             DumpStage stage) {
  return catch_out_of_memory<std::string>(
      [&] { return dump_impl(html, fonts, images, opts, stage); });
}

std::string_view to_string(DumpStage stage) noexcept {
  switch (stage) {
    case DumpStage::Dom:
      return "dom";
    case DumpStage::Style:
      return "style";
    case DumpStage::Box:
      return "box";
    case DumpStage::DisplayList:
      return "display-list";
    case DumpStage::Svg:
      return "svg";
  }
  return "unknown";
}

}  // namespace shashoku
