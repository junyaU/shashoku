#pragma once

#include <string_view>

#include "style/computed_style.hpp"
#include "style/declaration.hpp"

// 計算値の enum / longhand プロパティ → CSS の綴り（ケバブケース）。
// エラーメッセージと dump_json() の両方が使う。

namespace shashoku::style {

inline std::string_view to_css(Display value) {
  switch (value) {
    case Display::Block:
      return "block";
    case Display::Flex:
      return "flex";
    case Display::Inline:
      return "inline";
    case Display::None:
      return "none";
  }
  return "block";
}

inline std::string_view to_css(FlexDirection value) {
  return value == FlexDirection::Row ? "row" : "column";
}

inline std::string_view to_css(JustifyContent value) {
  switch (value) {
    case JustifyContent::FlexStart:
      return "flex-start";
    case JustifyContent::FlexEnd:
      return "flex-end";
    case JustifyContent::Center:
      return "center";
    case JustifyContent::SpaceBetween:
      return "space-between";
    case JustifyContent::SpaceAround:
      return "space-around";
    case JustifyContent::SpaceEvenly:
      return "space-evenly";
  }
  return "flex-start";
}

inline std::string_view to_css(AlignItems value) {
  switch (value) {
    case AlignItems::Stretch:
      return "stretch";
    case AlignItems::FlexStart:
      return "flex-start";
    case AlignItems::FlexEnd:
      return "flex-end";
    case AlignItems::Center:
      return "center";
  }
  return "stretch";
}

inline std::string_view to_css(TextAlign value) {
  switch (value) {
    case TextAlign::Start:
      return "start";
    case TextAlign::End:
      return "end";
    case TextAlign::Left:
      return "left";
    case TextAlign::Right:
      return "right";
    case TextAlign::Center:
      return "center";
    case TextAlign::Justify:
      return "justify";
  }
  return "start";
}

inline std::string_view to_css(LineBreak value) {
  switch (value) {
    case LineBreak::Auto:
      return "auto";
    case LineBreak::Loose:
      return "loose";
    case LineBreak::Normal:
      return "normal";
    case LineBreak::Strict:
      return "strict";
  }
  return "auto";
}

inline std::string_view to_css(OverflowWrap value) {
  switch (value) {
    case OverflowWrap::Normal:
      return "normal";
    case OverflowWrap::Anywhere:
      return "anywhere";
    case OverflowWrap::BreakWord:
      return "break-word";
  }
  return "normal";
}

inline std::string_view to_css(WritingMode value) {
  return value == WritingMode::HorizontalTb ? "horizontal-tb" : "vertical-rl";
}

inline std::string_view to_css(PropertyId value) {
  switch (value) {
    case PropertyId::Display:
      return "display";
    case PropertyId::Width:
      return "width";
    case PropertyId::Height:
      return "height";
    case PropertyId::MarginTop:
      return "margin-top";
    case PropertyId::MarginRight:
      return "margin-right";
    case PropertyId::MarginBottom:
      return "margin-bottom";
    case PropertyId::MarginLeft:
      return "margin-left";
    case PropertyId::PaddingTop:
      return "padding-top";
    case PropertyId::PaddingRight:
      return "padding-right";
    case PropertyId::PaddingBottom:
      return "padding-bottom";
    case PropertyId::PaddingLeft:
      return "padding-left";
    case PropertyId::BorderWidth:
      return "border-width";
    case PropertyId::BorderStyle:
      return "border-style";
    case PropertyId::BorderColor:
      return "border-color";
    case PropertyId::BorderRadius:
      return "border-radius";
    case PropertyId::BackgroundColor:
      return "background-color";
    case PropertyId::FlexDirection:
      return "flex-direction";
    case PropertyId::JustifyContent:
      return "justify-content";
    case PropertyId::AlignItems:
      return "align-items";
    case PropertyId::RowGap:
      return "row-gap";
    case PropertyId::ColumnGap:
      return "column-gap";
    case PropertyId::FlexGrow:
      return "flex-grow";
    case PropertyId::FlexShrink:
      return "flex-shrink";
    case PropertyId::FlexBasis:
      return "flex-basis";
    case PropertyId::Color:
      return "color";
    case PropertyId::FontSize:
      return "font-size";
    case PropertyId::FontFamily:
      return "font-family";
    case PropertyId::FontWeight:
      return "font-weight";
    case PropertyId::LineHeight:
      return "line-height";
    case PropertyId::LetterSpacing:
      return "letter-spacing";
    case PropertyId::TextAlign:
      return "text-align";
    case PropertyId::LineBreak:
      return "line-break";
    case PropertyId::OverflowWrap:
      return "overflow-wrap";
    case PropertyId::WritingMode:
      return "writing-mode";
  }
  return "display";
}

}  // namespace shashoku::style
