#include "shashoku/render.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/loaded_resources.hpp"
#include "api/out_of_memory.hpp"
#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/diagnostics.hpp"
#include "core/ids.hpp"
#include "core/number_text.hpp"
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
// FontStore も画像テーブルも、`FontSet` / `ImageSet` を渡す経路では呼び出しごとに作って
// 捨てる。`LoadedFonts` / `LoadedImages` を渡す経路（A34）では**利用者が**持つ読み取り専用の
// 共有資源を参照するだけで、どちらの経路でも出力はバイト単位で同じになる。
namespace shashoku {
namespace {

// 入力の上限（A25）は RenderLimits に一本化してある。各モジュールは自分の既定値を
// 定数で持っているが、api は必ず RenderLimits の値を渡すので、両者が食い違っていないことを
// ここで機械的に確かめる（既定値が 2 か所に分かれて別々に動くのを防ぐ）。
static_assert(RenderLimits{}.nesting_depth == html::kMaxNestingDepth,
              "RenderLimits::nesting_depth と html::kMaxNestingDepth の既定値が食い違っている");
static_assert(RenderLimits{}.style_rules == style::kMaxStyleRules,
              "RenderLimits::style_rules と style::kMaxStyleRules の既定値が食い違っている");
static_assert(RenderLimits{}.length_px == style::kMaxLengthPx,
              "RenderLimits::length_px と style::kMaxLengthPx の既定値が食い違っている");
// layout の出口の上限（A36）は「1 要素あたりの長さ x 要素数」で導く。導出の式が 2 か所で
// 別々に動かないよう、既定値の一致をここで確かめる。
static_assert(RenderLimits{}.length_px * static_cast<float>(RenderLimits{}.dom_nodes) ==
                  layout::kMaxGeometryPx,
              "layout::kMaxGeometryPx が RenderLimits::length_px x dom_nodes と食い違っている");
static_assert(RenderLimits{}.image_pixels == png::kMaxPixels,
              "RenderLimits::image_pixels と png::kMaxPixels の既定値が食い違っている");
static_assert(RenderLimits{}.device_pixels == raster::kMaxDevicePixels,
              "RenderLimits::device_pixels と raster::kMaxDevicePixels の既定値が食い違っている");

// 圧縮レベル（A33）も同じ流儀: 既定値は RenderOptions が正で、png は単体利用の既定を持つ。
static_assert(
    RenderOptions{}.compression_level == png::kDefaultCompressionLevel,
    "RenderOptions::compression_level と png::kDefaultCompressionLevel の既定値が食い違っている");

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
  // wrap（A35）は CSS の overflow-wrap から layout がアイテムごとに決める（ここでは既定のまま）。
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
    return fail(
        ErrorKind::InvalidOption,
        std::format("scale must be a positive finite number (got {})", number_text(options.scale)));
  }
  if (options.scale > options.limits.scale) {
    return fail(ErrorKind::LimitExceeded, std::format("scale is {}, which exceeds the limit of {} "
                                                      "(raise RenderLimits::scale to allow it)",
                                                      options.scale, options.limits.scale));
  }
  // 圧縮レベル（A33）。png::encode も同じ範囲を検査するが、ここで弾けば
  // 「オプションの誤りは HTML を読む前に分かる」という順序（§3.10）を保てる。
  if (options.compression_level < 0 || options.compression_level > 9) {
    return fail(ErrorKind::InvalidOption,
                std::format("compression level must be between 0 and 9 (got {})",
                            options.compression_level));
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

// (a) 入力を受けた時点。パースもデコードもする前に弾く。画像は枚数だけを見るので、
// バイト列（ImageSet）でもデコード済み（LoadedImages）でも同じ判定になる。
Result<void> check_input_limits(std::string_view html, std::size_t image_count,
                                const RenderLimits& limits) {
  if (html.size() > limits.html_bytes) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the HTML input is {} bytes, which exceeds the limit of {} "
                            "(raise RenderLimits::html_bytes to allow it)",
                            html.size(), limits.html_bytes));
  }
  if (image_count > limits.images) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the ImageSet has {} images, which exceeds the limit of {} "
                            "(raise RenderLimits::images to allow it)",
                            image_count, limits.images));
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
                                number_text(node.style.font_size), number_text(scale),
                                number_text(device_px), number_text(limits.font_size_device_px)),
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

// パイプラインの (7) の位置で解決された資源への参照（DESIGN.md §3-2）。
// Shaper / FreeTypeGlyphSource は FontStore を参照するだけなので、実体の方が長生きする。
struct ResourceRefs {
  const text::FontStore* fonts = nullptr;
  const std::vector<Bitmap>* images = nullptr;      // 添字がそのまま ImageId
  const std::vector<std::string>* names = nullptr;  // images と同じ長さ・同じ順
};

// 「その場で用意する」経路で、用意したものを 1 回の render の間だけ生かしておく置き場。
struct OwnedResources {
  std::optional<LoadedFonts> fonts;
  std::optional<LoadedImages> images;
  std::vector<Bitmap> no_images;  // 画像を渡されなかった経路が指す空の表
  std::vector<std::string> no_names;
};

// 用意済みの画像に (c) の上限を掛け直す（A34）。prepare() に渡した RenderLimits と
// opts.limits が違っても「同じ HTML + 同じ limits なら同じ結果」が崩れないようにする。
// 画素はもう確保済みなので、ここで見るのは「この上限で描いてよいか」だけ。
Result<void> recheck_image_limits(const detail::ImageTable& table, const RenderLimits& limits) {
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < table.images->size(); ++i) {
    const Bitmap& bitmap = (*table.images)[i];
    const std::uint64_t pixels = std::uint64_t{bitmap.width} * bitmap.height;
    if (pixels > limits.image_pixels) {
      return fail(
          ErrorKind::LimitExceeded,
          std::format("image \"{}\" is {}x{} = {} pixels, which exceeds the limit of {} "
                      "(raise RenderLimits::image_pixels to allow it)",
                      (*table.names)[i], bitmap.width, bitmap.height, pixels, limits.image_pixels));
    }
    total += pixels;
    if (total > limits.total_image_pixels) {
      return fail(ErrorKind::LimitExceeded,
                  std::format("the prepared images have {} pixels in total, which exceeds the "
                              "limit of {} (raise RenderLimits::total_image_pixels to allow it)",
                              total, limits.total_image_pixels));
    }
  }
  return {};
}

// 資源の用意のしかた。経路が 2 つあるのはここだけで、パイプライン本体
// （render_impl / dump_impl）は 1 本のまま:
//   - `FontSet` / `ImageSet`: パイプラインの (7) の位置でその場で用意する（従来どおり）
//   - `LoadedFonts` / `LoadedImages`: 用意済みの共有資源を参照し、上限を掛け直す（A34）
// 検査の順序はどちらでも同じなので、同じ入力からは同じエラーが同じ順で出る。
class ResourceSource {
 public:
  static ResourceSource from_sets(const FontSet& fonts, const ImageSet& images) {
    ResourceSource source;
    source.font_set_ = &fonts;
    source.image_set_ = &images;
    return source;
  }
  // images が nullptr なら「画像なし」。
  static ResourceSource from_loaded(const LoadedFonts& fonts, const LoadedImages* images) {
    ResourceSource source;
    source.loaded_fonts_ = &fonts;
    source.loaded_images_ = images;
    return source;
  }

  [[nodiscard]] std::size_t image_count() const noexcept {
    if (image_set_ != nullptr) {
      return image_set_->size();
    }
    return loaded_images_ != nullptr ? loaded_images_->size() : 0;
  }

  [[nodiscard]] Result<ResourceRefs> acquire(OwnedResources& owned,
                                             const RenderLimits& limits) const {
    const Result<const text::FontStore*> fonts = acquire_fonts(owned);
    if (!fonts) {
      return std::unexpected(fonts.error());
    }
    const Result<detail::ImageTable> images = acquire_images(owned, limits);
    if (!images) {
      return std::unexpected(images.error());
    }
    return ResourceRefs{.fonts = *fonts, .images = images->images, .names = images->names};
  }

 private:
  [[nodiscard]] Result<const text::FontStore*> acquire_fonts(OwnedResources& owned) const {
    if (font_set_ != nullptr) {
      std::expected<LoadedFonts, RenderError> loaded = LoadedFonts::prepare(*font_set_);
      if (!loaded) {
        return std::unexpected(loaded.error());
      }
      return detail::LoadedFontsAccess::fonts(owned.fonts.emplace(std::move(*loaded)));
    }
    const text::FontStore* fonts = detail::LoadedFontsAccess::fonts(*loaded_fonts_);
    if (fonts == nullptr) {
      return fail(ErrorKind::InvalidOption,
                  "the LoadedFonts was moved from: call LoadedFonts::prepare() again");
    }
    return fonts;
  }

  [[nodiscard]] Result<detail::ImageTable> acquire_images(OwnedResources& owned,
                                                          const RenderLimits& limits) const {
    if (image_set_ != nullptr) {
      std::expected<LoadedImages, RenderError> loaded = LoadedImages::prepare(*image_set_, limits);
      if (!loaded) {
        return std::unexpected(loaded.error());
      }
      return detail::LoadedImagesAccess::table(owned.images.emplace(std::move(*loaded)));
    }
    if (loaded_images_ == nullptr) {
      return detail::ImageTable{.images = &owned.no_images, .names = &owned.no_names};
    }
    const detail::ImageTable table = detail::LoadedImagesAccess::table(*loaded_images_);
    if (table.images == nullptr) {
      return fail(ErrorKind::InvalidOption,
                  "the LoadedImages was moved from: call LoadedImages::prepare() again");
    }
    // 用意済みの画像には opts.limits を掛け直す（prepare() と違う上限でも辻褄が合うように）。
    if (const Result<void> ok = recheck_image_limits(table, limits); !ok) {
      return std::unexpected(ok.error());
    }
    return table;
  }

  const FontSet* font_set_ = nullptr;
  const ImageSet* image_set_ = nullptr;
  const LoadedFonts* loaded_fonts_ = nullptr;
  const LoadedImages* loaded_images_ = nullptr;
};

// ---------------------------------------------------------------------------
// 失敗の組み立て（ARCHITECTURE.md A46）
// ---------------------------------------------------------------------------

// 段が返した 1 件の失敗を、①② で集めた診断と合わせて RenderFailure にする。
std::unexpected<RenderFailure> to_failure(Diagnostics& diagnostics, RenderError error) {
  diagnostics.sort();
  return std::unexpected(std::move(diagnostics).into_failure({std::move(error)}));
}

// ①② が集めた診断だけで失敗にする（致命的な失敗は無いが、対応外のものが 1 件以上ある）。
std::unexpected<RenderFailure> to_failure(Diagnostics& diagnostics) {
  diagnostics.sort();
  return std::unexpected(std::move(diagnostics).into_failure());
}

// ---------------------------------------------------------------------------
// パイプライン
// ---------------------------------------------------------------------------

Result<layout::BoxTree> run_layout(const style::StyledNode& styled, const RenderOptions& options,
                                   text::TextMeasurer& measurer, const ResourceRefs& resources) {
  layout::Options layout_options;
  layout_options.viewport_width = static_cast<float>(options.viewport_width);
  if (options.viewport_height) {
    layout_options.viewport_height = static_cast<float>(*options.viewport_height);
  }
  layout_options.line_break = to_internal(options.line_break);
  // 出口の検査の上限（A36）。「1 要素あたりの長さ x 要素数」で導くので、この 2 つの上限を
  // 守った文書では絶対に発動しない。どちらかを極端に緩めて積が inf になったら、
  // 出口の検査は「有限であること」だけを見る（それでも黙って消える事故は止まる）。
  layout_options.max_geometry_px =
      options.limits.length_px * static_cast<float>(options.limits.dom_nodes);

  // 名前 → 画像（A12）。追加順の線形探索: 決定的で、数十枚までなら十分速い。
  const layout::ImageLookup lookup =
      [&resources](std::string_view src) -> std::optional<layout::ImageInfo> {
    for (std::size_t i = 0; i < resources.names->size(); ++i) {
      if ((*resources.names)[i] == src) {
        return layout::ImageInfo{static_cast<ImageId>(i),
                                 static_cast<float>((*resources.images)[i].width),
                                 static_cast<float>((*resources.images)[i].height)};
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
  // 非有限な内容高さは③ layout の出口（A36）が位置つきで止めるので、ここには届かない。
  // 届いたら layout の不変条件が破れている = shashoku 側のバグなので、
  // 「高さが 0」と言い切らずに Internal で報告する（issue #19 のメッセージの誤り 3 件目）。
  if (!std::isfinite(height)) {
    return fail(ErrorKind::Internal,
                std::format("the laid out content height is {}, which is not a finite number "
                            "(layout should have rejected it; please report this input)",
                            number_text(height)));
  }
  if (!(height > 0)) {
    return fail(ErrorKind::InvalidOption,
                "nothing to render: the content height is 0 and no viewport height was given");
  }
  return height;
}

// 豆腐の記録（③ レイアウトが集める。A31）→ 公開 API の Warning。
// 並びは layout が決めている（入力位置の昇順 → コードポイントの昇順。決定的）ので、
// ここでは並べ替えない。detail には RenderError と同じ書式で位置を添える。
std::vector<Warning> to_warnings(const std::vector<layout::MissingGlyph>& missing) {
  std::vector<Warning> warnings;
  warnings.reserve(missing.size());
  for (const layout::MissingGlyph& glyph : missing) {
    // 種類は MissingGlyph のまま。文面だけ理由で分ける（A43。WarningKind は増やさない）。
    const auto codepoint = static_cast<std::uint32_t>(glyph.codepoint);
    std::string detail =
        glyph.reason == text::MissingReason::ColorOnly
            ? std::format("the glyph for U+{:04X} has only color layers (COLR); drawn as tofu",
                          codepoint)
            : std::format("no font has a glyph for U+{:04X}", codepoint);
    detail += std::format(" at {}:{}", glyph.location.line, glyph.location.column);
    warnings.push_back(Warning{.kind = WarningKind::MissingGlyph,
                               .detail = std::move(detail),
                               .codepoint = glyph.codepoint,
                               .location = glyph.location});
  }
  return warnings;
}

std::expected<RenderResult, RenderFailure> render_impl(std::string_view html,
                                                       const ResourceSource& source,
                                                       const RenderOptions& options) {
  // 集める診断（A46）。①② に渡し、段の失敗もここを通して RenderFailure にする。
  // opts.warnings_as_errors はまだ読まない（格上げは W4）。
  Diagnostics diagnostics{options.limits.max_diagnostics};
  if (const Result<void> ok = validate(options); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  if (const Result<void> ok = check_input_limits(html, source.image_count(), options.limits); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  const Result<html::Node> dom = html::parse(html, diagnostics, options.limits.nesting_depth);
  if (!dom) {
    return to_failure(diagnostics, dom.error());
  }
  if (const Result<void> ok = check_dom_limits(*dom, options.limits); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  const Result<style::StyledNode> styled =
      style::resolve(*dom, diagnostics, options.limits.style_rules, options.limits.length_px);
  if (!styled) {
    return to_failure(diagnostics, styled.error());
  }
  if (const Result<void> ok = check_computed_limits(*styled, options.scale, options.limits); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  // ①② が集めた問題が 1 件でもあれば、layout に進まずまとめて返す（A46 / §3.10）。
  // ② が集め始めるまでは、ここに来るのは html の UnsupportedTag / UnsupportedAttribute だけ。
  if (diagnostics.has_errors()) {
    return to_failure(diagnostics);
  }
  OwnedResources owned;
  const Result<ResourceRefs> resources = source.acquire(owned, options.limits);
  if (!resources) {
    return to_failure(diagnostics, resources.error());
  }

  text::Shaper shaper(*resources->fonts);
  const Result<layout::BoxTree> tree = run_layout(*styled, options, shaper, *resources);
  if (!tree) {
    return to_failure(diagnostics, tree.error());
  }
  const Result<float> height = output_height(*tree, options);
  if (!height) {
    return to_failure(diagnostics, height.error());
  }

  const raster::DisplayList list = paint::build_display_list(*tree);
  const raster::Target target{.width = static_cast<float>(options.viewport_width),
                              .height = *height,
                              .scale = options.scale,
                              .background = kTransparent,
                              .max_device_pixels = options.limits.device_pixels};
  text::FreeTypeGlyphSource glyphs(*resources->fonts);
  const Result<Bitmap> bitmap = raster::rasterize(list, target, glyphs, *resources->images);
  if (!bitmap) {
    if (bitmap.error().kind == ErrorKind::LimitExceeded) {
      return to_failure(
          diagnostics,
          RenderError{.kind = ErrorKind::LimitExceeded,
                      .message = std::format("{} (raise RenderLimits::device_pixels to allow it)",
                                             bitmap.error().message),
                      .location = std::nullopt,
                      .hint = {},
                      .warning = std::nullopt});
    }
    return to_failure(diagnostics, bitmap.error());
  }
  // 圧縮レベルは必ず明示的に渡す（既定値を 2 か所で別々に持たない。A25 / A33）。
  Result<std::vector<std::uint8_t>> encoded = png::encode(*bitmap, options.compression_level);
  if (!encoded) {
    return to_failure(diagnostics, encoded.error());
  }

  RenderResult result;
  result.png = std::move(*encoded);
  result.warnings = to_warnings(tree->missing_glyphs);
  // 警告はまだ Diagnostics を通していない（上限と格上げは W4）ので、打ち切りは起きない。
  result.diagnostics_truncated = diagnostics.truncated();
  result.width = static_cast<int>(bitmap->width);
  result.height = static_cast<int>(bitmap->height);
  return result;
}

std::expected<std::string, RenderFailure> dump_impl(std::string_view html,
                                                    const ResourceSource& source,
                                                    const RenderOptions& options, DumpStage stage) {
  Diagnostics diagnostics{options.limits.max_diagnostics};
  if (const Result<void> ok = validate(options); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  if (const Result<void> ok = check_input_limits(html, source.image_count(), options.limits); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  const Result<html::Node> dom = html::parse(html, diagnostics, options.limits.nesting_depth);
  if (!dom) {
    return to_failure(diagnostics, dom.error());
  }
  if (const Result<void> ok = check_dom_limits(*dom, options.limits); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  if (stage == DumpStage::Dom) {
    return html::dump_json(*dom);
  }
  const Result<style::StyledNode> styled =
      style::resolve(*dom, diagnostics, options.limits.style_rules, options.limits.length_px);
  if (!styled) {
    return to_failure(diagnostics, styled.error());
  }
  if (const Result<void> ok = check_computed_limits(*styled, options.scale, options.limits); !ok) {
    return to_failure(diagnostics, ok.error());
  }
  // ①② が集めた問題が 1 件でもあれば、layout に進まずまとめて返す（A46 / §3.10）。
  // ② が集め始めるまでは、ここに来るのは html の UnsupportedTag / UnsupportedAttribute だけ。
  if (diagnostics.has_errors()) {
    return to_failure(diagnostics);
  }
  if (stage == DumpStage::Style) {
    return style::dump_json(*styled);
  }

  OwnedResources owned;
  const Result<ResourceRefs> resources = source.acquire(owned, options.limits);
  if (!resources) {
    return to_failure(diagnostics, resources.error());
  }
  text::Shaper shaper(*resources->fonts);
  const Result<layout::BoxTree> tree = run_layout(*styled, options, shaper, *resources);
  if (!tree) {
    return to_failure(diagnostics, tree.error());
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
    return to_failure(diagnostics, height.error());
  }
  return paint::dump_svg(list, static_cast<float>(options.viewport_width), *height);
}

}  // namespace

// メモリ不足（A26）を捕まえるのは、この 5 本 + prepare() の公開関数の境界だけ
// （api/out_of_memory.hpp）。

std::expected<RenderResult, RenderFailure> render(std::string_view html, const FontSet& fonts,
                                                  const RenderOptions& opts) {
  const ImageSet images;
  return detail::catch_out_of_memory<RenderResult, RenderFailure>(
      [&] { return render_impl(html, ResourceSource::from_sets(fonts, images), opts); });
}

std::expected<RenderResult, RenderFailure> render(std::string_view html, const FontSet& fonts,
                                                  const ImageSet& images,
                                                  const RenderOptions& opts) {
  return detail::catch_out_of_memory<RenderResult, RenderFailure>(
      [&] { return render_impl(html, ResourceSource::from_sets(fonts, images), opts); });
}

std::expected<RenderResult, RenderFailure> render(std::string_view html, const LoadedFonts& fonts,
                                                  const RenderOptions& opts) {
  return detail::catch_out_of_memory<RenderResult, RenderFailure>(
      [&] { return render_impl(html, ResourceSource::from_loaded(fonts, nullptr), opts); });
}

std::expected<RenderResult, RenderFailure> render(std::string_view html, const LoadedFonts& fonts,
                                                  const LoadedImages& images,
                                                  const RenderOptions& opts) {
  return detail::catch_out_of_memory<RenderResult, RenderFailure>(
      [&] { return render_impl(html, ResourceSource::from_loaded(fonts, &images), opts); });
}

std::expected<std::string, RenderFailure> dump(std::string_view html, const FontSet& fonts,
                                               const ImageSet& images, const RenderOptions& opts,
                                               DumpStage stage) {
  return detail::catch_out_of_memory<std::string, RenderFailure>(
      [&] { return dump_impl(html, ResourceSource::from_sets(fonts, images), opts, stage); });
}

std::expected<std::string, RenderFailure> dump(std::string_view html, const LoadedFonts& fonts,
                                               const LoadedImages& images,
                                               const RenderOptions& opts, DumpStage stage) {
  return detail::catch_out_of_memory<std::string, RenderFailure>(
      [&] { return dump_impl(html, ResourceSource::from_loaded(fonts, &images), opts, stage); });
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
