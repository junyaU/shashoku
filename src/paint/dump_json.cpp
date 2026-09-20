#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/json_writer.hpp"
#include "paint/display_list_builder.hpp"
#include "raster/display_list.hpp"

// --dump-stage=display-list（DESIGN.md §3-3）。キー順は固定で、既定値のキーは省く
// （sideways = false）。座標はすべて物理座標の CSS px。
namespace shashoku::paint {
namespace {

std::string color_hex(const Color& color) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out = "#";
  for (const std::uint8_t component : {color.r, color.g, color.b, color.a}) {
    out.push_back(kDigits[component >> 4U]);
    out.push_back(kDigits[component & 0x0FU]);
  }
  return out;
}

void write_rect(JsonWriter& writer, const Rect& rect) {
  writer.begin_array().value(rect.x).value(rect.y).value(rect.width).value(rect.height).end_array();
}

void write_command(JsonWriter& writer, const raster::FillRect& cmd) {
  writer.key("op").value("fill_rect");
  writer.key("rect");
  write_rect(writer, cmd.rect);
  writer.key("color").value(color_hex(cmd.color));
}

void write_command(JsonWriter& writer, const raster::FillRoundedRect& cmd) {
  writer.key("op").value("fill_rounded_rect");
  writer.key("rect");
  write_rect(writer, cmd.rect);
  writer.key("radius").value(cmd.radius);
  writer.key("color").value(color_hex(cmd.color));
}

void write_command(JsonWriter& writer, const raster::StrokeRoundedRect& cmd) {
  writer.key("op").value("stroke_rounded_rect");
  writer.key("rect");
  write_rect(writer, cmd.rect);
  writer.key("radius").value(cmd.radius);
  writer.key("width").value(cmd.width);
  writer.key("color").value(color_hex(cmd.color));
}

void write_command(JsonWriter& writer, const raster::DrawGlyphs& cmd) {
  writer.key("op").value("draw_glyphs");
  writer.key("font").value(static_cast<std::uint64_t>(cmd.font));
  writer.key("size").value(cmd.size);
  writer.key("color").value(color_hex(cmd.color));
  if (cmd.sideways) {
    writer.key("sideways").value(true);
  }
  // グリフごとに [glyph_id, 原点 x, 原点 y]
  writer.key("glyphs").begin_array();
  for (const raster::GlyphInstance& glyph : cmd.glyphs) {
    writer.begin_array()
        .value(static_cast<std::uint64_t>(glyph.glyph_id))
        .value(glyph.origin.x)
        .value(glyph.origin.y)
        .end_array();
  }
  writer.end_array();
}

void write_command(JsonWriter& writer, const raster::DrawImage& cmd) {
  writer.key("op").value("draw_image");
  writer.key("image").value(static_cast<std::uint64_t>(cmd.image));
  writer.key("dest");
  write_rect(writer, cmd.dest);
}

void write_command(JsonWriter& writer, const raster::PushClip& cmd) {
  writer.key("op").value("push_clip");
  writer.key("rect");
  write_rect(writer, cmd.rect);
  writer.key("radius").value(cmd.radius);
}

void write_command(JsonWriter& writer, const raster::PopClip& /*cmd*/) {
  writer.key("op").value("pop_clip");
}

}  // namespace

std::string dump_json(const raster::DisplayList& list) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("commands").begin_array();
  for (const raster::DrawCmd& command : list) {
    writer.begin_object();
    std::visit([&writer](const auto& cmd) { write_command(writer, cmd); }, command);
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
  return std::move(writer).str();
}

}  // namespace shashoku::paint
