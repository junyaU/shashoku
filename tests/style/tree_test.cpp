#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// スタイル付きツリーの形: display: none / <style> / <rp> の除去、Text ノード、img の属性、
// flex コンテナの子の block 化（A53）。

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

// ---- flex コンテナの子の block 化（A53）------------------------------------------
//
// CSS Display 3 §2.7 / CSS Flexbox 1 §4: flex コンテナの**直接の子要素**はブロック化される。
// layout（`flex_layout.cpp` の `build_items()`）はもともと子を flex アイテム（ブロック級）として
// 組んでおり、止めていたのは style の検査だけだった。

// flex の親の下に置いた要素 1 つの計算値を返す。
Result<ComputedStyle> flex_child_style(std::string_view tag, std::string_view style) {
  const html::Node tree = test_root(test_parent("div", {test_attr("style", "display: flex")},
                                                test_element(tag, {test_attr("style", style)})));
  Result<StyledNode> styled = resolve_for_test(tree);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (styled->children.empty() || styled->children.front().children.empty()) {
    return fail(ErrorKind::Internal, "the test element was dropped from the tree");
  }
  return styled->children.front().children.front().style;
}

TEST(StyleTree, FlexContainerBlockifiesItsInlineChildren) {
  const Result<ComputedStyle> style = flex_child_style("span", "padding: 4px 8px");
  ASSERT_TRUE(style.has_value()) << (style ? "" : style.error().message);
  EXPECT_EQ(style->display, Display::Block);
  EXPECT_EQ(style->padding, (Edges<float>{4, 8, 4, 8}));
}

// 作者が明示的に `display: inline` と書いても block 化する（CSS Display 3 §2.7）。
TEST(StyleTree, ExplicitDisplayInlineIsStillBlockifiedInAFlexContainer) {
  const Result<ComputedStyle> style = flex_child_style("span", "display: inline; width: 40px");
  ASSERT_TRUE(style.has_value()) << (style ? "" : style.error().message);
  EXPECT_EQ(style->display, Display::Block);
}

// 変えるのは直接の子だけ。孫は inline のまま（文中の span は文の流れに残る）。
TEST(StyleTree, FlexBlockificationDoesNotReachGrandchildren) {
  const html::Node tree = test_root(test_parent(
      "div", {test_attr("style", "display: flex")},
      test_parent("span", {}, test_parent("span", {}, test_text("孫")), test_text("子"))));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  const StyledNode& child = styled->children.front().children.front();
  EXPECT_EQ(child.style.display, Display::Block);
  ASSERT_FALSE(child.children.empty());
  EXPECT_EQ(child.children.front().tag, "span");
  EXPECT_EQ(child.children.front().style.display, Display::Inline);
  // テキストノードは対象外（親の継承プロパティを持つ inline のまま）
  EXPECT_EQ(child.children.back().type, StyledNode::Type::Text);
  EXPECT_EQ(child.children.back().style.display, Display::Inline);
}

// 例外の 4 つ: img（置換要素）/ ruby（layout が無名アイテムの中に inline のまま入れる）/
// br（強制改行）/ display: none（none のまま = 木から落ちる）。
TEST(StyleTree, FlexBlockificationExceptions) {
  const html::Node tree = test_root(test_parent(
      "div", {test_attr("style", "display: flex")}, test_element("img", {test_attr("src", "x")}),
      test_parent("ruby", {}, test_text("漢"), test_parent("rt", {}, test_text("かん"))),
      test_element("br"), test_element("span", {test_attr("style", "display: none")})));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  const StyledNode& container = styled->children.front();
  ASSERT_EQ(container.children.size(), 3U);  // display: none の span は落ちる
  EXPECT_EQ(container.children.at(0).tag, "img");
  EXPECT_EQ(container.children.at(0).style.display, Display::Inline);
  EXPECT_EQ(container.children.at(1).tag, "ruby");
  EXPECT_EQ(container.children.at(1).style.display, Display::Inline);
  EXPECT_EQ(container.children.at(2).tag, "br");
  EXPECT_EQ(container.children.at(2).style.display, Display::Inline);
}

// flex でない親の下では何も変わらない（文中の span は inline のまま）。
TEST(StyleTree, NonFlexParentsDoNotBlockifyInlineChildren) {
  for (const std::string_view container : {"display: block", "display: inline"}) {
    SCOPED_TRACE(container);
    const html::Node tree =
        test_root(test_parent("div", {test_attr("style", container)}, test_element("span")));
    const Result<StyledNode> styled = resolve_for_test(tree);
    ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
    EXPECT_EQ(styled->children.front().children.front().style.display, Display::Inline);
  }
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
