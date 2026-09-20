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

// scale の上限。ラスタライザのデバイスピクセル上限に当たる前に、
// 「1.0 のつもりが 1000」のような取り違えを InvalidOption として弾く。
constexpr float kMaxScale = 256.0F;

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
  if (options.scale > kMaxScale) {
    return fail(ErrorKind::InvalidOption,
                std::format("scale must not exceed {} (got {})", kMaxScale, options.scale));
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

Result<Resources> load_resources(const FontSet& fonts, const ImageSet& images) {
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
  for (std::size_t i = 0; i < images.size(); ++i) {
    const std::string_view name = images.name(i);
    Result<Bitmap> bitmap = png::decode(images.bytes(i));
    if (!bitmap) {
      return fail(ErrorKind::ImageDecode,
                  std::format("image \"{}\": {}", name, bitmap.error().message));
    }
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
  const Result<html::Node> dom = html::parse(html);
  if (!dom) {
    return std::unexpected(dom.error());
  }
  const Result<style::StyledNode> styled = style::resolve(*dom);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  Result<Resources> resources = load_resources(fonts, images);
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
                              .background = kTransparent};
  text::FreeTypeGlyphSource glyphs(resources->fonts);
  const Result<Bitmap> bitmap = raster::rasterize(list, target, glyphs, resources->images);
  if (!bitmap) {
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
  const Result<html::Node> dom = html::parse(html);
  if (!dom) {
    return std::unexpected(dom.error());
  }
  if (stage == DumpStage::Dom) {
    return html::dump_json(*dom);
  }
  const Result<style::StyledNode> styled = style::resolve(*dom);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (stage == DumpStage::Style) {
    return style::dump_json(*styled);
  }

  Result<Resources> resources = load_resources(fonts, images);
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

}  // namespace

std::expected<RenderResult, RenderError> render(std::string_view html, const FontSet& fonts,
                                                const RenderOptions& opts) {
  return render_impl(html, fonts, ImageSet{}, opts);
}

std::expected<RenderResult, RenderError> render(std::string_view html, const FontSet& fonts,
                                                const ImageSet& images, const RenderOptions& opts) {
  return render_impl(html, fonts, images, opts);
}

std::expected<std::string, RenderError> dump(std::string_view html, const FontSet& fonts,
                                             const ImageSet& images, const RenderOptions& opts,
                                             DumpStage stage) {
  return dump_impl(html, fonts, images, opts, stage);
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
