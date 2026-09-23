#include <string>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// スタイル付きツリーの形: display: none / <style> / <rp> の除去、Text ノード、img の属性。

namespace shashoku::style {
namespace {

TEST(StyleTree, RootIsKeptWithItsTag) {
  const Result<StyledNode> styled = resolve_for_test(test_root());
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->type, StyledNode::Type::Element);
  EXPECT_EQ(styled->tag, "#root");
  EXPECT_TRUE(styled->children.empty());
}

TEST(StyleTree, DisplayNoneSubtreesAreDropped) {
  const html::Node tree =
      test_root(test_parent("div", {test_attr("style", "display: none")}, test_text("hidden")),
                test_parent("div", {}, test_text("kept")));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  ASSERT_EQ(styled->children.size(), 1U);
  ASSERT_EQ(styled->children.front().children.size(), 1U);
  EXPECT_EQ(styled->children.front().children.front().text, "kept");
}

TEST(StyleTree, StyleAndRpElementsAreDropped) {
  const html::Node tree = test_root(
      test_style_element("div { color: red }"),
      test_parent("ruby", {}, test_text("漢"), test_parent("rp", {}, test_text("(")),
                  test_parent("rt", {}, test_text("かん")), test_parent("rp", {}, test_text(")"))));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  ASSERT_EQ(styled->children.size(), 1U);
  const StyledNode& ruby = styled->children.front();
  EXPECT_EQ(ruby.tag, "ruby");
  ASSERT_EQ(ruby.children.size(), 2U);  // テキスト「漢」と <rt> だけが残る
  EXPECT_EQ(ruby.children.at(0).type, StyledNode::Type::Text);
  EXPECT_EQ(ruby.children.at(1).tag, "rt");
}

TEST(StyleTree, TextNodesCopyInheritedStyleAndResetBoxProperties) {
  const html::Node tree =
      test_root(test_parent("div",
                            {test_attr("style",
                                       "color: red; font-size: 20px; padding: 5px; width: 100px; "
                                       "background-color: blue; border: 2px solid green")},
                            test_text("こんにちは")));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  ASSERT_EQ(styled->children.size(), 1U);
  ASSERT_EQ(styled->children.front().children.size(), 1U);
  const StyledNode& text = styled->children.front().children.front();

  EXPECT_EQ(text.type, StyledNode::Type::Text);
  EXPECT_EQ(text.text, "こんにちは");
  EXPECT_TRUE(text.tag.empty());
  // 継承する群は親と同じ
  EXPECT_EQ(text.style.color, (Color{255, 0, 0, 255}));
  EXPECT_FLOAT_EQ(text.style.font_size, 20.0F);
  // 箱の群は初期値、display は inline
  EXPECT_EQ(text.style.display, Display::Inline);
  EXPECT_EQ(text.style.width, Dimension::auto_());
  EXPECT_FLOAT_EQ(text.style.padding.top, 0.0F);
  EXPECT_FLOAT_EQ(text.style.border_width, 0.0F);
  EXPECT_EQ(text.style.background_color, kTransparent);
  EXPECT_EQ(text.style.border_color, text.style.color);  // currentColor
}

TEST(StyleTree, TextNodesKeepSourceWhitespaceAndLocation) {
  const SourceLocation location{.offset = 7, .line = 1, .column = 8};
  const html::Node tree = test_root(test_parent("div", {}, test_text("  a\n  b  ", location)));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  const StyledNode& text = styled->children.front().children.front();
  EXPECT_EQ(text.text, "  a\n  b  ");  // 空白の畳み込みは ③ の仕事
  EXPECT_EQ(text.location, location);
}

TEST(StyleTree, ImageAttributes) {
  const html::Node tree = test_root(
      test_element("img", {test_attr("src", "hero.png"), test_attr("width", "320"),
                           test_attr("height", "180.5"), test_attr("alt", "見出し画像")}));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  const StyledNode& image = styled->children.front();
  EXPECT_EQ(image.tag, "img");
  EXPECT_EQ(image.image_src, "hero.png");
  EXPECT_FLOAT_EQ(image.attr_width.value_or(0), 320.0F);
  EXPECT_FLOAT_EQ(image.attr_height.value_or(0), 180.5F);
  EXPECT_EQ(image.style.display, Display::Inline);
}

TEST(StyleTree, ImageWithoutSizeAttributes) {
  const html::Node tree = test_root(test_element("img", {test_attr("src", "x")}));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_FALSE(styled->children.front().attr_width.has_value());
  EXPECT_FALSE(styled->children.front().attr_height.has_value());
}

TEST(StyleTree, ElementLocationIsCarriedOver) {
  const SourceLocation location{.offset = 3, .line = 2, .column = 1};
  const html::Node tree = test_root(test_element("div", {}, location));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->children.front().location, location);
}

TEST(StyleTree, ResolveIsDeterministic) {
  const html::Node tree = test_root(
      test_style_element("div { color: red } .a { color: blue } div.a { font-size: 2em }"),
      test_parent("div", {test_attr("class", "a")}, test_text("あ")),
      test_parent("p", {}, test_text("い")));
  const Result<StyledNode> first = resolve_for_test(tree);
  const Result<StyledNode> second = resolve_for_test(tree);
  ASSERT_TRUE(first.has_value()) << (first ? "" : first.error().message);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(dump_json(*first), dump_json(*second));
}

}  // namespace
}  // namespace shashoku::style
