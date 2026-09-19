#include "shashoku/error.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// fail loudly（DESIGN.md §3-6）の網羅。黙って無視される宣言が 1 つもないことを確かめる。

namespace shashoku::style {
namespace {

struct Case {
  std::string_view css;
  ErrorKind kind;
};

void expect_errors(const std::vector<Case>& cases) {
  for (const Case& test : cases) {
    SCOPED_TRACE(test.css);
    const Result<ComputedStyle> style = inline_style(test.css);
    ASSERT_FALSE(style.has_value()) << "should have failed";
    EXPECT_EQ(style.error().kind, test.kind) << style.error().message;
    EXPECT_FALSE(style.error().message.empty());
  }
}

void expect_sheet_errors(const std::vector<Case>& cases) {
  for (const Case& test : cases) {
    SCOPED_TRACE(test.css);
    const Result<ComputedStyle> style = sheet_style(test.css);
    ASSERT_FALSE(style.has_value()) << "should have failed";
    EXPECT_EQ(style.error().kind, test.kind) << style.error().message;
    EXPECT_FALSE(style.error().message.empty());
  }
}

// ---- 対応外のプロパティ ---------------------------------------------------------

TEST(StyleError, UnsupportedProperties) {
  expect_errors({
      {"float: left", ErrorKind::UnsupportedProperty},
      {"position: absolute", ErrorKind::UnsupportedProperty},
      {"box-shadow: 0 0 4px black", ErrorKind::UnsupportedProperty},
      {"grid-template-columns: 1fr 1fr", ErrorKind::UnsupportedProperty},
      {"box-sizing: border-box", ErrorKind::UnsupportedProperty},
      {"border-top: 1px solid red", ErrorKind::UnsupportedProperty},
      {"border-top-left-radius: 4px", ErrorKind::UnsupportedProperty},
      {"overflow: hidden", ErrorKind::UnsupportedProperty},
      {"transform: rotate(3deg)", ErrorKind::UnsupportedProperty},
      {"opacity: 0.5", ErrorKind::UnsupportedProperty},
      {"flex-wrap: wrap", ErrorKind::UnsupportedProperty},
      {"white-space: nowrap", ErrorKind::UnsupportedProperty},
      {"text-orientation: upright", ErrorKind::UnsupportedProperty},
      {"font: 16px serif", ErrorKind::UnsupportedProperty},
      {"-webkit-line-clamp: 2", ErrorKind::UnsupportedProperty},
  });
}

TEST(StyleError, UnsupportedPropertyMessageNamesTheProperty) {
  const Result<ComputedStyle> style = inline_style("float: left");
  ASSERT_FALSE(style.has_value());
  EXPECT_NE(style.error().message.find("float"), std::string::npos) << style.error().message;
}

// ---- 対応外の値・単位 -----------------------------------------------------------

TEST(StyleError, UnsupportedUnits) {
  expect_errors({
      {"width: 2rem", ErrorKind::UnsupportedValue},
      {"width: 50vw", ErrorKind::UnsupportedValue},
      {"font-size: 12pt", ErrorKind::UnsupportedValue},
      {"width: calc(100% - 10px)", ErrorKind::UnsupportedValue},
      {"margin: 1ex", ErrorKind::UnsupportedValue},
      {"width: 10", ErrorKind::UnsupportedValue},  // 単位なしの非 0
  });
}

TEST(StyleError, UnsupportedKeywords) {
  expect_errors({
      {"display: grid", ErrorKind::UnsupportedValue},
      {"display: inline-block", ErrorKind::UnsupportedValue},
      {"flex-direction: row-reverse", ErrorKind::UnsupportedValue},
      {"flex-direction: column-reverse", ErrorKind::UnsupportedValue},
      {"align-items: baseline", ErrorKind::UnsupportedValue},
      {"justify-content: stretch", ErrorKind::UnsupportedValue},
      {"font-weight: bolder", ErrorKind::UnsupportedValue},
      {"font-weight: lighter", ErrorKind::UnsupportedValue},
      {"font-weight: 450", ErrorKind::UnsupportedValue},
      {"writing-mode: vertical-lr", ErrorKind::UnsupportedValue},
      {"line-break: anywhere", ErrorKind::UnsupportedValue},
      {"text-align: justify-all", ErrorKind::UnsupportedValue},
      {"border-style: dashed", ErrorKind::UnsupportedValue},
      {"width: min-content", ErrorKind::UnsupportedValue},
      {"font-size: large", ErrorKind::UnsupportedValue},
  });
}

TEST(StyleError, UnsupportedValueMessageListsWhatIsSupported) {
  const Result<ComputedStyle> style = inline_style("display: grid");
  ASSERT_FALSE(style.has_value());
  EXPECT_EQ(style.error().message,
            "`display: grid` is not supported (supported: block, flex, inline, none)");
}

TEST(StyleError, UnsupportedColors) {
  expect_errors({
      {"color: hsl(0, 100%, 50%)", ErrorKind::UnsupportedValue},
      {"color: hsla(0, 100%, 50%, 0.5)", ErrorKind::UnsupportedValue},
      {"color: lab(50% 40 59)", ErrorKind::UnsupportedValue},
      {"color: #12345", ErrorKind::UnsupportedValue},
      {"color: #gggggg", ErrorKind::UnsupportedValue},
      {"color: notacolor", ErrorKind::UnsupportedValue},
      {"color: rgb(1, 2)", ErrorKind::UnsupportedValue},
      {"color: rgb(1 2 3 4)", ErrorKind::UnsupportedValue},
      // currentColor は border-color だけ
      {"color: currentColor", ErrorKind::UnsupportedValue},
      {"background-color: currentColor", ErrorKind::UnsupportedValue},
  });
}

TEST(StyleError, PercentAndNegativeRestrictions) {
  expect_errors({
      {"height: 50%", ErrorKind::UnsupportedValue},
      {"margin: 10%", ErrorKind::UnsupportedValue},
      {"padding: 10%", ErrorKind::UnsupportedValue},
      {"font-size: 50%", ErrorKind::UnsupportedValue},
      {"width: -10px", ErrorKind::UnsupportedValue},
      {"padding: -1px", ErrorKind::UnsupportedValue},
      {"border-radius: -1px", ErrorKind::UnsupportedValue},
      {"gap: -2px", ErrorKind::UnsupportedValue},
      {"flex-grow: -1", ErrorKind::UnsupportedValue},
      {"line-height: -1", ErrorKind::UnsupportedValue},
  });
}

TEST(StyleError, ShorthandArity) {
  expect_errors({
      {"margin: 1px 2px 3px 4px 5px", ErrorKind::UnsupportedValue},
      {"padding: 1px 2px 3px 4px 5px", ErrorKind::UnsupportedValue},
      {"gap: 1px 2px 3px", ErrorKind::UnsupportedValue},
      {"flex: 1 2 3px 4px", ErrorKind::UnsupportedValue},
      {"border-radius: 4px 8px", ErrorKind::UnsupportedValue},
      {"border-radius: 50% / 20%", ErrorKind::UnsupportedValue},
      {"width: 10px 20px", ErrorKind::UnsupportedValue},
  });
}

TEST(StyleError, UnsetAndRevertAreNotSupported) {
  expect_errors({
      {"color: unset", ErrorKind::UnsupportedValue},
      {"color: revert", ErrorKind::UnsupportedValue},
      {"margin: unset", ErrorKind::UnsupportedValue},
  });
}

// ---- 構文エラー -----------------------------------------------------------------

TEST(StyleError, SyntaxErrorsInDeclarations) {
  expect_errors({
      {"color red", ErrorKind::CssParse},  // コロンなし
      {"color: ", ErrorKind::CssParse},    // 値なし
      {"color:", ErrorKind::CssParse},     // 値なし
      {": red", ErrorKind::CssParse},      // プロパティ名なし
      {"color: /* 閉じていない", ErrorKind::CssParse},
      {R"(font-family: "unterminated)", ErrorKind::CssParse},
      {"color: rgb(1, 2, 3", ErrorKind::CssParse},  // 閉じていない `(`
      {"color: red !important", ErrorKind::CssParse},
      {"color: red}", ErrorKind::CssParse},  // style 属性に `}`
  });
}

TEST(StyleError, SyntaxErrorsInStylesheets) {
  expect_sheet_errors({
      {"div { color: red", ErrorKind::CssParse},   // 閉じていない `{`
      {"div color: red }", ErrorKind::CssParse},   // `{` がない
      {"div { color red }", ErrorKind::CssParse},  // コロンなし
      {"div { /* 閉じていない }", ErrorKind::CssParse},
      {"} div { color: red }", ErrorKind::CssParse},  // いきなり `}`
      {"div { color: red !important }", ErrorKind::CssParse},
  });
}

TEST(StyleError, UnsupportedSelectors) {
  expect_sheet_errors({
      {"div p { color: red }", ErrorKind::CssParse},      // 子孫結合子
      {"div > p { color: red }", ErrorKind::CssParse},    // 子結合子
      {"div + p { color: red }", ErrorKind::CssParse},    // 隣接
      {"div ~ p { color: red }", ErrorKind::CssParse},    // 後続
      {"div:hover { color: red }", ErrorKind::CssParse},  // 擬似クラス
      {"div::before { color: red }", ErrorKind::CssParse},
      {"div[data-x] { color: red }", ErrorKind::CssParse},
      {"div, { color: red }", ErrorKind::CssParse},  // 空のセレクタ
      {". { color: red }", ErrorKind::CssParse},     // クラス名がない
      {"#a#b { color: red }", ErrorKind::CssParse},
  });
}

TEST(StyleError, SelectorErrorMessagesExplainWhy) {
  const Result<ComputedStyle> descendant = sheet_style("div p { color: red }");
  ASSERT_FALSE(descendant.has_value());
  EXPECT_NE(descendant.error().message.find("combinator"), std::string::npos)
      << descendant.error().message;
  const Result<ComputedStyle> pseudo = sheet_style("a:hover { color: red }");
  ASSERT_FALSE(pseudo.has_value());
  EXPECT_NE(pseudo.error().message.find("pseudo"), std::string::npos) << pseudo.error().message;
}

TEST(StyleError, AtRulesAreNotSupported) {
  expect_sheet_errors({
      {"@media screen { div { color: red } }", ErrorKind::CssParse},
      {"@import url(x.css);", ErrorKind::CssParse},
      {"@font-face { font-family: x }", ErrorKind::CssParse},
  });
  const Result<ComputedStyle> style = sheet_style("@media screen { div { color: red } }");
  ASSERT_FALSE(style.has_value());
  EXPECT_NE(style.error().message.find("at-rule"), std::string::npos) << style.error().message;
}

// ---- エラーの位置 ---------------------------------------------------------------

TEST(StyleError, InlineStyleErrorsPointAtTheAttribute) {
  const SourceLocation attribute{.offset = 42, .line = 3, .column = 6};
  const html::Node tree =
      test_root(test_element("div", {test_attr("style", "float: left", attribute)}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().location.value_or(SourceLocation{}), attribute);
}

TEST(StyleError, StyleElementErrorsAddTheCssLineAndColumn) {
  // <style> のテキストは 5 行目 8 桁目から始まる、という想定
  const SourceLocation base{.offset = 100, .line = 5, .column = 8};
  const html::Node tree = test_root(test_style_element("div {\n  float: left;\n}", base));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedProperty);
  // CSS の 2 行目 3 桁目 → 入力の 6 行目 3 桁目（2 行目以降は桁がそのまま）
  const SourceLocation location = styled.error().location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 6U);
  EXPECT_EQ(location.column, 3U);
  EXPECT_EQ(location.offset, 100U + 8U);
}

TEST(StyleError, FirstLineOfCssKeepsTheAttributeColumnOffset) {
  const SourceLocation base{.offset = 10, .line = 2, .column = 4};
  const html::Node tree = test_root(test_style_element("div { float: left }", base));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  const SourceLocation location = styled.error().location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 2U);
  EXPECT_EQ(location.column, 4U + 6U);  // `float` は CSS の 7 桁目
}

// ---- inline 要素への箱の指定 -----------------------------------------------------

TEST(StyleError, BoxPropertiesOnInlineElements) {
  const std::vector<std::string_view> properties = {
      "width: 10px",           "height: 10px",      "margin: 1px",        "padding: 1px",
      "border: 1px solid red", "border-width: 1px", "border-radius: 2px",
  };
  for (const std::string_view property : properties) {
    SCOPED_TRACE(property);
    const html::Node tree = test_root(test_element("span", {test_attr("style", property)}));
    const Result<StyledNode> styled = resolve(tree);
    ASSERT_FALSE(styled.has_value()) << "should have failed";
    EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedLayout) << styled.error().message;
  }
}

TEST(StyleError, InlineBoxCheckOnlyLooksAtAuthorDeclarations) {
  // UA が p に付ける margin は作者の宣言ではないので、display: inline にしても通る
  const html::Node tree = test_root(test_element("p", {test_attr("style", "display: inline")}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->children.front().style.display, Display::Inline);
  // 継承も作者の宣言ではない
  const html::Node inherited = test_root(test_parent(
      "div", {test_attr("style", "color: red; border: 1px solid blue")}, test_element("span")));
  EXPECT_TRUE(resolve(inherited).has_value());
}

TEST(StyleError, ImgIsExemptFromTheInlineBoxCheck) {
  const html::Node tree = test_root(test_element(
      "img", {test_attr("src", "logo"), test_attr("style", "width: 10px; margin: 2px")}));
  EXPECT_TRUE(resolve(tree).has_value());
}

TEST(StyleError, BoxPropertyOnInlineIsFineWhenDisplayIsChanged) {
  const html::Node tree =
      test_root(test_element("span", {test_attr("style", "display: block; width: 10px")}));
  EXPECT_TRUE(resolve(tree).has_value());
}

// ---- writing-mode の規則（A1）----------------------------------------------------

TEST(StyleError, WritingModeOnTopLevelElementIsAdoptedByTheRoot) {
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "writing-mode: vertical-rl")}, test_element("div")));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->style.writing_mode, WritingMode::VerticalRl);
  EXPECT_EQ(styled->children.at(0).style.writing_mode, WritingMode::VerticalRl);
  EXPECT_EQ(styled->children.at(0).children.at(0).style.writing_mode, WritingMode::VerticalRl);
}

TEST(StyleError, TopLevelElementsMustAgreeOnWritingMode) {
  const html::Node tree =
      test_root(test_element("div", {test_attr("style", "writing-mode: vertical-rl")}),
                test_element("div", {test_attr("style", "writing-mode: horizontal-tb")}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedLayout);
}

TEST(StyleError, WritingModeCannotChangeDeeperInTheTree) {
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "writing-mode: vertical-rl")},
                  test_element("div", {test_attr("style", "writing-mode: horizontal-tb")})));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedLayout);
}

TEST(StyleError, RepeatingTheDocumentWritingModeDeeperIsAllowed) {
  // 値が同じなら「変更」ではない
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "writing-mode: vertical-rl")},
                  test_element("div", {test_attr("style", "writing-mode: vertical-rl")})));
  EXPECT_TRUE(resolve(tree).has_value());
}

TEST(StyleError, WritingModeDeeperThanTopLevelIsRejected) {
  const html::Node tree = test_root(test_parent(
      "div", {}, test_element("div", {test_attr("style", "writing-mode: vertical-rl")})));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedLayout);
}

// ---- img の属性 -------------------------------------------------------------------

TEST(StyleError, ImgRequiresSrc) {
  const html::Node tree = test_root(test_element("img"));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(styled.error().message, "`<img>` requires a `src` attribute");
}

TEST(StyleError, ImgSizeAttributesMustBeNonNegativeNumbers) {
  const std::vector<std::string_view> bad = {"-1", "10px", "abc", "", "50%"};
  for (const std::string_view value : bad) {
    SCOPED_TRACE(value);
    const html::Node tree =
        test_root(test_element("img", {test_attr("src", "x"), test_attr("width", value)}));
    const Result<StyledNode> styled = resolve(tree);
    ASSERT_FALSE(styled.has_value()) << "should have failed";
    EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedValue) << styled.error().message;
  }
}

// ---- 落とされる部分木の中でも黙らない ---------------------------------------------

TEST(StyleError, DeclarationsInsideDisplayNoneSubtreesAreStillChecked) {
  const html::Node tree =
      test_root(test_parent("div", {test_attr("style", "display: none")},
                            test_element("div", {test_attr("style", "float: left")})));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedProperty);
}

TEST(StyleError, RulesThatMatchNothingAreStillChecked) {
  const Result<ComputedStyle> style = sheet_style("nosuchtag { float: left }");
  ASSERT_FALSE(style.has_value());
  EXPECT_EQ(style.error().kind, ErrorKind::UnsupportedProperty);
}

}  // namespace
}  // namespace shashoku::style
