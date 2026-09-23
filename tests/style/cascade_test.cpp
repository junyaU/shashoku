#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// カスケード（UA < `<style>` < `style` 属性、詳細度、出現順）とセレクタ、UA スタイル。

namespace shashoku::style {
namespace {

ComputedStyle from_sheet(std::string_view css, std::vector<html::Attribute> attrs = {}) {
  const Result<ComputedStyle> style = sheet_style(css, std::move(attrs));
  EXPECT_TRUE(style.has_value()) << css << ": " << (style ? "" : style.error().message);
  return style.value_or(ComputedStyle{});
}

ComputedStyle of_tag(std::string_view tag) {
  const Result<ComputedStyle> style = tag_style(tag);
  EXPECT_TRUE(style.has_value()) << tag << ": " << (style ? "" : style.error().message);
  return style.value_or(ComputedStyle{});
}

// ---- UA スタイル ---------------------------------------------------------------

TEST(StyleCascade, UserAgentDisplay) {
  EXPECT_EQ(of_tag("div").display, Display::Block);
  EXPECT_EQ(of_tag("p").display, Display::Block);
  EXPECT_EQ(of_tag("h1").display, Display::Block);
  EXPECT_EQ(of_tag("h6").display, Display::Block);
  EXPECT_EQ(of_tag("span").display, Display::Inline);
  EXPECT_EQ(of_tag("ruby").display, Display::Inline);
  EXPECT_EQ(of_tag("rt").display, Display::Inline);
  EXPECT_EQ(of_tag("br").display, Display::Inline);
}

TEST(StyleCascade, UserAgentHeadings) {
  // font-size はブラウザ既定値、margin は自身の font-size 基準の em
  struct Case {
    std::string_view tag;
    float font_size;
    float margin;
  };
  const std::vector<Case> cases = {
      {"h1", 32.0F, 0.67F * 32.0F},
      {"h2", 1.5F * 16.0F, 0.83F * 1.5F * 16.0F},
      {"h3", 1.17F * 16.0F, 1.17F * 16.0F},
      {"h4", 16.0F, 1.33F * 16.0F},
      {"h5", 0.83F * 16.0F, 1.67F * 0.83F * 16.0F},
      {"h6", 0.67F * 16.0F, 2.33F * 0.67F * 16.0F},
  };
  for (const Case& test : cases) {
    SCOPED_TRACE(test.tag);
    const ComputedStyle style = of_tag(test.tag);
    EXPECT_FLOAT_EQ(style.font_size, test.font_size);
    EXPECT_EQ(style.font_weight, 700);
    EXPECT_EQ(style.margin.top, Dimension::px(test.margin));
    EXPECT_EQ(style.margin.bottom, Dimension::px(test.margin));
    EXPECT_EQ(style.margin.left, Dimension::px(0));
    EXPECT_EQ(style.margin.right, Dimension::px(0));
  }
}

TEST(StyleCascade, UserAgentParagraphAndRubyText) {
  const ComputedStyle paragraph = of_tag("p");
  EXPECT_EQ(paragraph.margin.top, Dimension::px(16));
  EXPECT_EQ(paragraph.margin.bottom, Dimension::px(16));
  EXPECT_EQ(paragraph.margin.left, Dimension::px(0));
  EXPECT_FLOAT_EQ(of_tag("rt").font_size, 8.0F);  // 50%
}

TEST(StyleCascade, SyntheticRootUsesInitialValuesAndBlockDisplay) {
  const html::Node tree = test_root(test_style_element("* { color: red }"));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->tag, "#root");
  EXPECT_EQ(styled->style.display, Display::Block);  // 合成ルートだけの例外
  EXPECT_EQ(styled->style.color, kBlack);            // 作者の規則は #root に当たらない
  EXPECT_FLOAT_EQ(styled->style.font_size, 16.0F);
  EXPECT_TRUE(styled->style.font_family.empty());
  EXPECT_EQ(styled->style.writing_mode, WritingMode::HorizontalTb);
}

// ---- 由来（origin）の順序 -------------------------------------------------------

TEST(StyleCascade, AuthorSheetBeatsUserAgent) {
  const html::Node tree = test_root(test_style_element("p { margin: 0 }"), test_element("p"));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->children.front().style.margin.top, Dimension::px(0));
}

TEST(StyleCascade, InlineStyleBeatsAuthorSheetEvenWithHigherSpecificity) {
  const html::Node tree =
      test_root(test_style_element("#a.b { color: red }"),
                test_element("div", {test_attr("id", "a"), test_attr("class", "b"),
                                     test_attr("style", "color: blue")}));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->children.front().style.color, (Color{0, 0, 255, 255}));
}

// ---- 詳細度と出現順 -------------------------------------------------------------

TEST(StyleCascade, SpecificityIdBeatsClassBeatsTag) {
  const std::vector<html::Attribute> attrs = {test_attr("id", "x"), test_attr("class", "c")};
  EXPECT_EQ(from_sheet("#x { color: red } .c { color: blue } div { color: green }", attrs).color,
            (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet(".c { color: blue } div { color: green }", attrs).color,
            (Color{0, 0, 255, 255}));
  // 詳細度は個数で比べる: .c.c（0,2,0）> #なし .c（0,1,0）
  EXPECT_EQ(from_sheet(".c.c { color: red } .c { color: blue }", attrs).color,
            (Color{255, 0, 0, 255}));
}

TEST(StyleCascade, SameSpecificityUsesSourceOrder) {
  const std::vector<html::Attribute> attrs = {test_attr("class", "c")};
  EXPECT_EQ(from_sheet(".c { color: red } .c { color: blue }", attrs).color,
            (Color{0, 0, 255, 255}));
}

TEST(StyleCascade, MultipleStyleElementsAreConcatenatedInDocumentOrder) {
  const html::Node tree = test_root(test_style_element("div { color: red; background: white }"),
                                    test_element("div"), test_style_element("div { color: blue }"));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  ASSERT_EQ(styled->children.size(), 1U);  // <style> は木から落ちる
  EXPECT_EQ(styled->children.front().style.color, (Color{0, 0, 255, 255}));
  EXPECT_EQ(styled->children.front().style.background_color, kWhite);
}

TEST(StyleCascade, StyleElementAppliesToTheWholeDocumentWhereverItIs) {
  // 木の深いところに書いた <style> も文書全体に効く
  const html::Node tree = test_root(test_parent("div", {test_attr("class", "outer")},
                                                test_style_element(".outer { color: red }")));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->children.front().style.color, (Color{255, 0, 0, 255}));
}

TEST(StyleCascade, CommaSeparatedSelectors) {
  EXPECT_EQ(from_sheet("p, div, span { color: red }").color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet("p, span { color: red }").color, kBlack);
}

TEST(StyleCascade, CompoundSelectors) {
  const std::vector<html::Attribute> attrs = {test_attr("id", "x"), test_attr("class", "a b")};
  EXPECT_EQ(from_sheet("div.a#x { color: red }", attrs).color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet("div.a.b { color: red }", attrs).color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet("div.a.c { color: red }", attrs).color, kBlack);
  EXPECT_EQ(from_sheet("span.a { color: red }", attrs).color, kBlack);
  EXPECT_EQ(from_sheet("#y { color: red }", attrs).color, kBlack);
}

TEST(StyleCascade, UniversalSelector) {
  EXPECT_EQ(from_sheet("* { color: red }").color, (Color{255, 0, 0, 255}));
  // `*` は詳細度 0 なのでタグセレクタに負ける
  EXPECT_EQ(from_sheet("div { color: blue } * { color: red }").color, (Color{0, 0, 255, 255}));
  // `*.a` はクラス 1 つぶんの詳細度を持つ
  EXPECT_EQ(from_sheet("div { color: blue } *.a { color: red }", {test_attr("class", "a")}).color,
            (Color{255, 0, 0, 255}));
}

TEST(StyleCascade, TagSelectorsAreCaseInsensitiveClassesAreNot) {
  EXPECT_EQ(from_sheet("DIV { color: red }").color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet(".A { color: red }", {test_attr("class", "a")}).color, kBlack);
}

TEST(StyleCascade, ClassAttributeSplitsOnWhitespace) {
  const std::vector<html::Attribute> attrs = {test_attr("class", "  alpha\tbeta\n gamma ")};
  EXPECT_EQ(from_sheet(".beta { color: red }", attrs).color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet(".gamma { color: red }", attrs).color, (Color{255, 0, 0, 255}));
}

// ---- セレクタごとの詳細度は「最も高いもの」を使う -------------------------------

TEST(StyleCascade, RuleUsesItsMostSpecificMatchingSelector) {
  const std::vector<html::Attribute> attrs = {test_attr("id", "x")};
  // `div` と `#x` の両方が当たる規則は詳細度 (1,0,0) として扱う
  EXPECT_EQ(from_sheet("div { color: blue } div, #x { color: red }", attrs).color,
            (Color{255, 0, 0, 255}));
  EXPECT_EQ(from_sheet("div, #x { color: red } #x { color: blue }", attrs).color,
            (Color{0, 0, 255, 255}));
}

}  // namespace
}  // namespace shashoku::style
