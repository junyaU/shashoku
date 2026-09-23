#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// 継承（する群 / しない群）、inherit / initial、em の連鎖、line-height の継承。

namespace shashoku::style {
namespace {

// `<div style="outer"><span style="inner">` を解決して span の計算値を返す。
Result<ComputedStyle> nested(std::string_view outer, std::string_view inner,
                             std::string_view inner_tag = "span") {
  const html::Node tree = test_root(test_parent(
      "div", {test_attr("style", outer)}, test_element(inner_tag, {test_attr("style", inner)})));
  Result<StyledNode> styled = resolve_for_test(tree);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (styled->children.empty() || styled->children.front().children.empty()) {
    return fail(ErrorKind::Internal, "the inner test element was dropped from the tree");
  }
  return styled->children.front().children.front().style;
}

ComputedStyle child_of(std::string_view outer, std::string_view inner = "",
                       std::string_view inner_tag = "span") {
  const Result<ComputedStyle> style = nested(outer, inner, inner_tag);
  EXPECT_TRUE(style.has_value()) << outer << " / " << inner << ": "
                                 << (style ? "" : style.error().message);
  return style.value_or(ComputedStyle{});
}

// ---- 継承する群 / しない群 -----------------------------------------------------

TEST(StyleInherit, TextPropertiesAreInherited) {
  const ComputedStyle style = child_of(
      "color: red; font-size: 20px; font-family: serif; font-weight: bold; line-height: 1.5; "
      "letter-spacing: 2px; text-align: center; line-break: strict; overflow-wrap: anywhere");
  EXPECT_EQ(style.color, (Color{255, 0, 0, 255}));
  EXPECT_FLOAT_EQ(style.font_size, 20.0F);
  EXPECT_EQ(style.font_family, (std::vector<std::string>{"serif"}));
  EXPECT_EQ(style.font_weight, 700);
  EXPECT_EQ(style.line_height, (LineHeight{LineHeight::Kind::Number, 1.5F}));
  EXPECT_FLOAT_EQ(style.letter_spacing, 2.0F);
  EXPECT_EQ(style.text_align, TextAlign::Center);
  EXPECT_EQ(style.line_break, LineBreak::Strict);
  EXPECT_EQ(style.overflow_wrap, OverflowWrap::Anywhere);
}

TEST(StyleInherit, BoxPropertiesAreNotInherited) {
  const ComputedStyle style = child_of(
      "width: 100px; height: 50px; margin: 5px; padding: 6px; border: 2px solid red; "
      "border-radius: 4px; background-color: blue; flex-direction: column; "
      "justify-content: center; align-items: center; gap: 3px; flex: 2 2 10px",
      "", "div");
  EXPECT_EQ(style.width, Dimension::auto_());
  EXPECT_EQ(style.height, Dimension::auto_());
  EXPECT_EQ(style.margin.top, Dimension::px(0));
  EXPECT_FLOAT_EQ(style.padding.top, 0.0F);
  EXPECT_FLOAT_EQ(style.border_width, 0.0F);
  EXPECT_FLOAT_EQ(style.border_radius, 0.0F);
  EXPECT_EQ(style.background_color, kTransparent);
  EXPECT_EQ(style.flex_direction, FlexDirection::Row);
  EXPECT_EQ(style.justify_content, JustifyContent::FlexStart);
  EXPECT_EQ(style.align_items, AlignItems::Stretch);
  EXPECT_FLOAT_EQ(style.row_gap, 0.0F);
  EXPECT_FLOAT_EQ(style.flex_grow, 0.0F);
  EXPECT_FLOAT_EQ(style.flex_shrink, 1.0F);
  EXPECT_EQ(style.flex_basis, Dimension::auto_());
}

TEST(StyleInherit, BorderColorDefaultsToOwnCurrentColor) {
  // 親の border-color は継承しない。自分の color（= 継承した色）が currentColor になる
  const ComputedStyle style = child_of("color: red; border: 1px solid blue");
  EXPECT_EQ(style.color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(style.border_color, (Color{255, 0, 0, 255}));
}

// ---- inherit / initial ---------------------------------------------------------

TEST(StyleInherit, InheritKeywordOnNonInheritedProperties) {
  EXPECT_EQ(child_of("width: 100px", "width: inherit", "div").width, Dimension::px(100));
  EXPECT_EQ(child_of("background-color: blue", "background-color: inherit", "div").background_color,
            (Color{0, 0, 255, 255}));
  EXPECT_FLOAT_EQ(child_of("padding: 7px", "padding: inherit", "div").padding.left, 7.0F);
  EXPECT_FLOAT_EQ(child_of("border: 3px solid red", "border: inherit", "div").border_width, 3.0F);
}

TEST(StyleInherit, InitialKeywordResetsToTheInitialValue) {
  EXPECT_EQ(child_of("color: red", "color: initial").color, kBlack);
  EXPECT_FLOAT_EQ(child_of("font-size: 40px", "font-size: initial").font_size, 16.0F);
  EXPECT_EQ(child_of("text-align: center", "text-align: initial").text_align, TextAlign::Start);
  EXPECT_TRUE(child_of("font-family: serif", "font-family: initial").font_family.empty());
  // ショートハンドの initial は全 longhand に配る
  const ComputedStyle margin = child_of("", "margin: 5px; margin: initial", "div");
  EXPECT_EQ(margin.margin.top, Dimension::px(0));
  EXPECT_EQ(margin.margin.left, Dimension::px(0));
}

TEST(StyleInherit, InheritOnUserAgentDisplay) {
  // display は継承しないが inherit は書ける
  EXPECT_EQ(child_of("display: flex", "display: inherit", "div").display, Display::Flex);
}

// ---- em の連鎖 ------------------------------------------------------------------

TEST(StyleInherit, FontSizeEmResolvesAgainstTheParent) {
  const ComputedStyle style = child_of("font-size: 2em", "font-size: 1.5em");
  EXPECT_FLOAT_EQ(style.font_size, 48.0F);  // 16 → 32 → 48
}

TEST(StyleInherit, OtherEmValuesResolveAgainstTheOwnFontSize) {
  const ComputedStyle style = child_of("font-size: 20px", "font-size: 2em; width: 1em", "div");
  EXPECT_FLOAT_EQ(style.font_size, 40.0F);
  EXPECT_EQ(style.width, Dimension::px(40));  // 自分の 40px 基準（親の 20px ではない）
}

TEST(StyleInherit, EmInsideHeadingUsesTheHeadingFontSize) {
  const html::Node tree = test_root(test_style_element("h2 { padding: 1em }"), test_element("h2"));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_FLOAT_EQ(styled->children.front().style.padding.top, 24.0F);  // 1.5em → 24px
}

// ---- line-height の継承 ---------------------------------------------------------

TEST(StyleInherit, LineHeightNumberIsInheritedAsARatio) {
  const ComputedStyle style =
      child_of("font-size: 20px; line-height: 1.5", "font-size: 40px", "div");
  EXPECT_EQ(style.line_height, (LineHeight{LineHeight::Kind::Number, 1.5F}));
}

TEST(StyleInherit, LineHeightLengthIsInheritedAsPx) {
  // em は指定した要素の font-size で px に解決してから継承する
  const ComputedStyle style =
      child_of("font-size: 20px; line-height: 1.5em", "font-size: 40px", "div");
  EXPECT_EQ(style.line_height, (LineHeight{LineHeight::Kind::Px, 30.0F}));
}

// ---- 継承は木をまたいで続く -----------------------------------------------------

TEST(StyleInherit, InheritanceFlowsThroughSeveralLevels) {
  const html::Node tree = test_root(test_parent(
      "div", {test_attr("style", "color: red; font-size: 2em")},
      test_parent("div", {}, test_element("span", {test_attr("style", "font-size: 0.5em")}))));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  const StyledNode& span = styled->children.at(0).children.at(0).children.at(0);
  EXPECT_EQ(span.tag, "span");
  EXPECT_EQ(span.style.color, (Color{255, 0, 0, 255}));
  EXPECT_FLOAT_EQ(span.style.font_size, 16.0F);  // 16 → 32 → 32 → 16
}

}  // namespace
}  // namespace shashoku::style
