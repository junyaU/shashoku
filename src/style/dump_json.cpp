#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/json_writer.hpp"
#include "style/computed_style.hpp"
#include "style/css_keywords.hpp"
#include "style/resolver.hpp"

// --dump-stage=style の出力。キー順は ComputedStyle の宣言順に合わせて固定する。

namespace shashoku::style {
namespace {

void write_color(JsonWriter& writer, Color color) {
  writer.value(std::format("#{:02x}{:02x}{:02x}{:02x}", color.r, color.g, color.b, color.a));
}

// auto は文字列、長さと % はどちらの単位か分かる 1 キーの object にする。
void write_dimension(JsonWriter& writer, const Dimension& dimension) {
  switch (dimension.kind) {
    case Dimension::Kind::Auto:
      writer.value("auto");
      return;
    case Dimension::Kind::Px:
      writer.begin_object().key("px").value(dimension.value).end_object();
      return;
    case Dimension::Kind::Percent:
      writer.begin_object().key("percent").value(dimension.value).end_object();
      return;
  }
}

void write_line_height(JsonWriter& writer, const LineHeight& line_height) {
  switch (line_height.kind) {
    case LineHeight::Kind::Normal:
      writer.value("normal");
      return;
    case LineHeight::Kind::Number:
      writer.begin_object().key("number").value(line_height.value).end_object();
      return;
    case LineHeight::Kind::Px:
      writer.begin_object().key("px").value(line_height.value).end_object();
      return;
  }
}

void write_box_style(JsonWriter& writer, const ComputedStyle& style) {
  writer.key("display").value(to_css(style.display));
  writer.key("box-sizing").value(to_css(style.box_sizing));
  writer.key("width");
  write_dimension(writer, style.width);
  writer.key("height");
  write_dimension(writer, style.height);
  writer.key("margin").begin_array();
  for (const Dimension& side :
       {style.margin.top, style.margin.right, style.margin.bottom, style.margin.left}) {
    write_dimension(writer, side);
  }
  writer.end_array();
  writer.key("padding").begin_array();
  for (const float side :
       {style.padding.top, style.padding.right, style.padding.bottom, style.padding.left}) {
    writer.value(side);
  }
  writer.end_array();
  writer.key("border-width").value(style.border_width);
  writer.key("border-color");
  write_color(writer, style.border_color);
  writer.key("border-radius").value(style.border_radius);
  writer.key("background-color");
  write_color(writer, style.background_color);
}

void write_flex_style(JsonWriter& writer, const ComputedStyle& style) {
  writer.key("flex-direction").value(to_css(style.flex_direction));
  writer.key("justify-content").value(to_css(style.justify_content));
  writer.key("align-items").value(to_css(style.align_items));
  writer.key("row-gap").value(style.row_gap);
  writer.key("column-gap").value(style.column_gap);
  writer.key("flex-grow").value(style.flex_grow);
  writer.key("flex-shrink").value(style.flex_shrink);
  writer.key("flex-basis");
  write_dimension(writer, style.flex_basis);
}

void write_text_style(JsonWriter& writer, const ComputedStyle& style) {
  writer.key("color");
  write_color(writer, style.color);
  writer.key("font-size").value(style.font_size);
  writer.key("font-family").begin_array();
  for (const std::string& name : style.font_family) {
    writer.value(name);
  }
  writer.end_array();
  writer.key("font-weight").value(style.font_weight);
  writer.key("line-height");
  write_line_height(writer, style.line_height);
  writer.key("letter-spacing").value(style.letter_spacing);
  writer.key("text-align").value(to_css(style.text_align));
  writer.key("line-break").value(to_css(style.line_break));
  writer.key("overflow-wrap").value(to_css(style.overflow_wrap));
  writer.key("writing-mode").value(to_css(style.writing_mode));
}

void write_style(JsonWriter& writer, const ComputedStyle& style) {
  writer.begin_object();
  write_box_style(writer, style);
  write_flex_style(writer, style);
  write_text_style(writer, style);
  writer.end_object();
}

// NOLINTNEXTLINE(misc-no-recursion): スタイル付きツリーを前順で辿る
void write_node(JsonWriter& writer, const StyledNode& node) {
  writer.begin_object();
  if (node.type == StyledNode::Type::Text) {
    writer.key("type").value("text");
    writer.key("text").value(node.text);
    writer.key("style");
    write_style(writer, node.style);
    writer.end_object();
    return;
  }

  writer.key("type").value("element");
  writer.key("tag").value(node.tag);
  if (node.tag == "img") {
    writer.key("src").value(node.image_src);
    writer.key("attr-width");
    if (node.attr_width) {
      writer.value(*node.attr_width);
    } else {
      writer.null();
    }
    writer.key("attr-height");
    if (node.attr_height) {
      writer.value(*node.attr_height);
    } else {
      writer.null();
    }
  }
  writer.key("style");
  write_style(writer, node.style);
  writer.key("children").begin_array();
  for (const StyledNode& child : node.children) {
    write_node(writer, child);
  }
  writer.end_array();
  writer.end_object();
}

}  // namespace

std::string dump_json(const StyledNode& root) {
  JsonWriter writer;
  write_node(writer, root);
  return std::move(writer).str();
}

}  // namespace shashoku::style
