#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/json_writer.hpp"
#include "layout/layout.hpp"
#include "shashoku/error.hpp"

// --dump-stage=box（DESIGN.md §3-3）。キー順は固定。
// 既定値のキーは省く: 透明な背景、幅 0 の枠線、半径 0、padding が全部 0、sideways = false。
// 座標はすべて論理座標（横書きなら物理座標と同じ値で読める。A1）。
namespace shashoku::layout {
namespace {

// 入力位置は "行:桁"（1 始まり）。to_string(RenderError) の " at L:C" と同じ書式（A31）。
std::string location_text(const SourceLocation& location) {
  return std::to_string(location.line) + ':' + std::to_string(location.column);
}

std::string color_hex(const Color& color) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out = "#";
  for (const std::uint8_t component : {color.r, color.g, color.b, color.a}) {
    out.push_back(kDigits[component >> 4U]);
    out.push_back(kDigits[component & 0x0FU]);
  }
  return out;
}

void write_rect(JsonWriter& writer, const LogicalRect& rect) {
  writer.begin_array()
      .value(rect.inline_start)
      .value(rect.block_start)
      .value(rect.inline_size)
      .value(rect.block_size)
      .end_array();
}

void write_text_fragment(JsonWriter& writer, const TextFragment& fragment) {
  writer.begin_object();
  writer.key("type").value("text");
  writer.key("text").value(fragment.text);
  writer.key("font").value(static_cast<std::uint64_t>(fragment.font));
  writer.key("font_size").value(fragment.font_size);
  writer.key("color").value(color_hex(fragment.color));
  if (fragment.sideways) {
    writer.key("sideways").value(true);
  }
  writer.key("inline_start").value(fragment.inline_start);
  writer.key("inline_size").value(fragment.inline_size);
  writer.key("baseline").value(fragment.baseline);
  // 元のテキストノードの位置（issue #9）。断片の先頭のグリフのもの
  writer.key("location").value(location_text(fragment.location));
  // グリフごとに [glyph_id, ペン位置の inline 座標, x_offset, y_offset]
  writer.key("glyphs").begin_array();
  for (const PositionedGlyph& glyph : fragment.glyphs) {
    writer.begin_array()
        .value(static_cast<std::uint64_t>(glyph.glyph_id))
        .value(glyph.inline_position)
        .value(glyph.x_offset)
        .value(glyph.y_offset)
        .end_array();
  }
  writer.end_array();
  writer.end_object();
}

// 塗り（既定値は省く）。ブロックと画像断片で共通。
void write_decoration(JsonWriter& writer, const BoxDecoration& decoration) {
  if (!decoration.background_color.transparent()) {
    writer.key("background_color").value(color_hex(decoration.background_color));
  }
  if (decoration.border_width > 0) {
    writer.key("border_width").value(decoration.border_width);
    writer.key("border_color").value(color_hex(decoration.border_color));
  }
  if (decoration.border_radius > 0) {
    writer.key("border_radius").value(decoration.border_radius);
  }
}

void write_image_fragment(JsonWriter& writer, const ImageFragment& image) {
  writer.begin_object();
  writer.key("type").value("image");
  writer.key("image").value(static_cast<std::uint64_t>(image.image));
  writer.key("rect");
  write_rect(writer, image.rect);
  writer.key("content_rect");
  write_rect(writer, image.content_rect);
  write_decoration(writer, image.decoration);
  writer.end_object();
}

void write_line(JsonWriter& writer, const LineBox& line) {
  writer.begin_object();
  writer.key("rect");
  write_rect(writer, line.rect);
  writer.key("baseline").value(line.baseline);
  writer.key("fragments").begin_array();
  for (const InlineFragment& fragment : line.fragments) {
    if (const auto* text = std::get_if<TextFragment>(&fragment)) {
      write_text_fragment(writer, *text);
      continue;
    }
    if (const auto* image = std::get_if<ImageFragment>(&fragment)) {
      write_image_fragment(writer, *image);
      continue;
    }
    const auto& background = std::get<InlineBackground>(fragment);
    writer.begin_object();
    writer.key("type").value("background");
    writer.key("rect");
    write_rect(writer, background.rect);
    writer.key("color").value(color_hex(background.color));
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
}

void write_block(JsonWriter& writer, const BlockBox& box) {
  writer.begin_object();
  writer.key("tag").value(box.tag);
  // この箱を生んだ要素の位置（A36）。出口の検査のエラーがどこを指すかを目で追えるようにする
  writer.key("location").value(location_text(box.location));
  writer.key("rect");
  write_rect(writer, box.rect);
  write_decoration(writer, box.decoration);
  const LogicalEdges<float>& padding = box.padding;
  if (padding != LogicalEdges<float>{}) {
    writer.key("padding")
        .begin_array()
        .value(padding.inline_start)
        .value(padding.inline_end)
        .value(padding.block_start)
        .value(padding.block_end)
        .end_array();
  }
  if (const std::vector<LineBox>* lines = box.lines()) {
    writer.key("lines").begin_array();
    for (const LineBox& line : *lines) {
      write_line(writer, line);
    }
    writer.end_array();
  } else {
    writer.key("blocks").begin_array();
    for (const BlockBox& child : *box.blocks()) {
      write_block(writer, child);
    }
    writer.end_array();
  }
  writer.end_object();
}

}  // namespace

std::string dump_json(const BoxTree& tree) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("writing_mode")
      .value(tree.writing_mode == WritingMode::VerticalRl ? "vertical-rl" : "horizontal-tb");
  writer.key("viewport_width").value(tree.viewport_width);
  writer.key("viewport_height");
  if (tree.viewport_height) {
    writer.value(*tree.viewport_height);
  } else {
    writer.null();
  }
  writer.key("root");
  write_block(writer, tree.root);
  // 豆腐（A31）。1 件も無ければキーごと省く（既定値のキーは出さない）
  if (!tree.missing_glyphs.empty()) {
    writer.key("missing_glyphs").begin_array();
    for (const MissingGlyph& missing : tree.missing_glyphs) {
      writer.begin_object();
      writer.key("codepoint")
          .value(std::format("U+{:04X}", static_cast<std::uint32_t>(missing.codepoint)));
      writer.key("location").value(location_text(missing.location));
      // 理由は既定（どのフォントにも無い）以外のときだけ出す（既定値のキーは出さない）。
      if (missing.reason == text::MissingReason::ColorOnly) {
        writer.key("reason").value("color-only");
      }
      writer.end_object();
    }
    writer.end_array();
  }
  writer.end_object();
  return std::move(writer).str();
}

}  // namespace shashoku::layout
