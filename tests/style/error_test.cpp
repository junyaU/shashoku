#include "shashoku/error.hpp"

#include <array>
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

// 「未対応です」だけでは次に何をすればよいか分からない（#20。試用版の体験に直接効く）。
// **未対応だと分かっているプロパティ**には、代替の書き方を一言だけ後ろに足す。
// 文面の**先頭は変えない**（前方一致で見ているものがあるかもしれないため、括弧で足すだけ）。
// 添える内容は「shashoku で実際に同じ結果が出せること」を確かめたものだけにする。
TEST(StyleError, KnownUnsupportedPropertiesCarryAWorkaround) {
  struct Hint {
    std::string_view css;
    std::string_view property;
    std::string_view needle;
  };
  const std::array<Hint, 12> cases{{
      {"box-sizing: border-box", "box-sizing", "content-box"},
      // CSS Box Alignment 3 §8.4 の legacy gap properties。写し先は 3 つとも対応済みだが、
      // shashoku に grid は無く flex に `grid-gap` と書く動機もないので別名は入れない（A35）。
      // 代わりに写し先を案内する
      {"grid-gap: 4px", "grid-gap", "`gap`"},
      {"grid-row-gap: 4px", "grid-row-gap", "`row-gap`"},
      {"grid-column-gap: 4px", "grid-column-gap", "`column-gap`"},
      {"max-width: 200px", "max-width", "`width`"},
      {"min-width: 200px", "min-width", "`width`"},
      {"max-height: 200px", "max-height", "`height`"},
      {"min-height: 200px", "min-height", "`height`"},
      {"float: left", "float", "display: flex"},
      {"position: absolute", "position", "display: flex"},
      {"flex-wrap: wrap", "flex-wrap", "single-line"},
      {"text-combine-upright: all", "text-combine-upright", "not implemented"},
  }};
  for (const Hint& test : cases) {
    SCOPED_TRACE(test.css);
    const Result<ComputedStyle> style = inline_style(test.css);
    ASSERT_FALSE(style.has_value()) << "should have failed";
    EXPECT_EQ(style.error().kind, ErrorKind::UnsupportedProperty);
    const std::string& message = style.error().message;
    // 先頭は今までどおり
    const std::string head = "`" + std::string(test.property) + "` is not a supported property";
    EXPECT_TRUE(message.starts_with(head)) << message;
    EXPECT_NE(message.find(test.needle), std::string::npos) << message;
  }
}

// 表に無い名前（綴り間違い・そもそも知らないプロパティ）には何も足さない。
// 間違った助言をするくらいなら、何も言わないほうがよい。
TEST(StyleError, UnknownPropertiesGetNoWorkaround) {
  for (const std::string_view css :
       {"floatt: left", "-webkit-line-clamp: 2", "grid-template-columns: 1fr 1fr",
        "text-orientation: upright"}) {
    SCOPED_TRACE(css);
    const Result<ComputedStyle> style = inline_style(css);
    ASSERT_FALSE(style.has_value()) << "should have failed";
    EXPECT_EQ(style.error().kind, ErrorKind::UnsupportedProperty);
    EXPECT_EQ(style.error().message.find('('), std::string::npos) << style.error().message;
  }
}

// `word-wrap` は `overflow-wrap` の legacy name alias（CSS Text 3 §5.4。issue #25）なので
// プロパティとしては通る。値が対応外のときだけ落ち、そのときは**著者が書いた綴り**と
// 宣言の位置で報告する（CSSOM を持たない shashoku で旧名が見える唯一の場所。A35）。
TEST(StyleError, WordWrapValueErrorsKeepTheAuthorSpellingAndLocation) {
  const SourceLocation attribute{.offset = 12, .line = 1, .column = 6};
  const html::Node tree =
      test_root(test_element("div", {test_attr("style", "word-wrap: foo", attribute)}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value());
  EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(styled.error().location.value_or(SourceLocation{}), attribute);
  EXPECT_TRUE(styled.error().message.starts_with("`word-wrap: foo` is not supported"))
      << styled.error().message;
  EXPECT_EQ(styled.error().message.find("overflow-wrap"), std::string::npos)
      << styled.error().message;
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

TEST(StyleError, PercentIsOnlyForWidthAndFlexBasis) {
  expect_errors({
      {"height: 50%", ErrorKind::UnsupportedValue},
      {"margin: 10%", ErrorKind::UnsupportedValue},
      {"padding: 10%", ErrorKind::UnsupportedValue},
      {"font-size: 50%", ErrorKind::UnsupportedValue},
      {"letter-spacing: 10%", ErrorKind::UnsupportedValue},
      {"gap: 10%", ErrorKind::UnsupportedValue},
      {"border-radius: 10%", ErrorKind::UnsupportedValue},
  });
}

// ---- 負の値（issue #26）----------------------------------------------------------
//
// 負を許さないプロパティを**総当たり**で並べる。DESIGN.md §10 の「テスト群が実質的な
// 仕様書」を値の検査にも当てるためで、ここから下の 5 つのテストを並べて読めば
// 「どのプロパティが負を許すか」が表から分かる。
//
// 表に無い項目は `to_length()` の引数（`allow_negative`）を 1 つ間違えただけで黙って
// 通るようになり、`font-size` が通れば送り 0 の「文字が消えた成功 PNG」になる
// （#19 が A36 で潰したのと同じ形）。判定そのものは前からあるので製品コードは変えていない。
//
// 種類は `UnsupportedValue`。表現範囲外（`1e39px`）は `LimitExceeded` で別物
// （A36 / #19。`tests/style/limit_test.cpp` の OutOfRangeNumbersAreLimitExceeded 以下）。

constexpr std::string_view kNegativeLength =
    "negative lengths are only allowed for `margin` and `letter-spacing`";
constexpr std::string_view kNegativePercent = "negative percentages are not allowed";
constexpr std::string_view kNonNegativeNumber = "expected a non-negative number";

// 宣言の位置が付くことまで見る（fail loudly は「どこが原因か」まで含めて成り立つ）。
constexpr SourceLocation kStyleAttribute{.offset = 41, .line = 3, .column = 9};

struct ValueCase {
  std::string_view css;
  std::string_view detail;  // 文面に必ず出る手がかり（どの規則で止まったか）
};

void expect_rejected_with_location(const std::vector<ValueCase>& cases) {
  for (const ValueCase& test : cases) {
    SCOPED_TRACE(test.css);
    const html::Node tree =
        test_root(test_element("div", {test_attr("style", test.css, kStyleAttribute)}));
    const Result<StyledNode> styled = resolve(tree);
    ASSERT_FALSE(styled.has_value()) << "should have failed";
    EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedValue) << styled.error().message;
    EXPECT_EQ(styled.error().location.value_or(SourceLocation{}), kStyleAttribute)
        << styled.error().message;
    EXPECT_NE(styled.error().message.find(test.detail), std::string::npos)
        << styled.error().message;
  }
}

TEST(StyleError, NegativeValuesAreRejectedExceptForMarginAndLetterSpacing) {
  expect_rejected_with_location({
      // 長さ（px / em）。ショートハンドも longhand も同じ規則
      {"width: -10px", kNegativeLength},
      {"height: -10px", kNegativeLength},
      {"padding: -4px", kNegativeLength},
      {"padding-left: -4px", kNegativeLength},
      {"border-radius: -4px", kNegativeLength},
      {"border-width: -2px", kNegativeLength},
      {"border: -2px solid red", kNegativeLength},
      {"gap: -4px", kNegativeLength},
      {"row-gap: -4px", kNegativeLength},
      {"column-gap: -4px", kNegativeLength},
      {"flex-basis: -10px", kNegativeLength},
      // font-size は「通ると送り 0 の空の PNG になる」筆頭（#19 / #26）
      {"font-size: -16px", kNegativeLength},
      {"font-size: -1em", kNegativeLength},
      {"line-height: -16px", kNegativeLength},
      {"line-height: -1em", kNegativeLength},
      // `%`（width と flex-basis だけが `%` を取れるので、負の `%` もこの 2 つだけ）
      {"width: -10%", kNegativePercent},
      {"flex-basis: -10%", kNegativePercent},
      // 単位なしの数値
      {"flex-grow: -1", kNonNegativeNumber},
      {"flex-shrink: -1", kNonNegativeNumber},
      {"flex: -1", kNonNegativeNumber},
      {"flex: 1 -1", kNonNegativeNumber},
      {"line-height: -1.5", "`line-height` must not be negative"},
      {"font-weight: -400", "supported: normal (400), bold (700), or 100..900"},
  });
}

// `<img>` の寸法属性は CSS を通らない別経路なので、同じ表に並べて見る。
TEST(StyleError, NegativeImgSizeAttributesAreRejectedWithTheAttributeLocation) {
  for (const std::string_view name : {"width", "height"}) {
    SCOPED_TRACE(name);
    const SourceLocation attribute{.offset = 17, .line = 2, .column = 11};
    const html::Node tree =
        test_root(test_element("img", {test_attr("src", "x"), test_attr(name, "-5", attribute)}));
    const Result<StyledNode> styled = resolve(tree);
    ASSERT_FALSE(styled.has_value()) << "should have failed";
    EXPECT_EQ(styled.error().kind, ErrorKind::UnsupportedValue) << styled.error().message;
    EXPECT_EQ(styled.error().location.value_or(SourceLocation{}), attribute);
    EXPECT_NE(styled.error().message.find("non-negative number of px, without a unit"),
              std::string::npos)
        << styled.error().message;
  }
}

// 表の「負が有効」側。ここが黙って 0 に丸められたりエラーになったりしないことを、
// **計算値の数値まで**見て固定する（CSS Box 3 §4: margin は負を取れる。
// CSS Text 3 §8.2: letter-spacing も負を取れる）。
TEST(StyleError, MarginAndLetterSpacingAcceptNegativeLengths) {
  const auto ok = [](std::string_view css) {
    const Result<ComputedStyle> style = inline_style(css);
    EXPECT_TRUE(style.has_value()) << css << ": " << (style ? "" : style.error().message);
    return style.value_or(ComputedStyle{});
  };
  EXPECT_EQ(ok("margin: -4px").margin.top, Dimension::px(-4));
  EXPECT_EQ(ok("margin: -4px").margin.left, Dimension::px(-4));
  EXPECT_EQ(ok("margin-left: -4px").margin.left, Dimension::px(-4));
  // em の基準は自分の font-size（既定の 16px）
  EXPECT_EQ(ok("margin: -1em").margin.top, Dimension::px(-16));
  EXPECT_FLOAT_EQ(ok("letter-spacing: -2px").letter_spacing, -2.0F);
  EXPECT_FLOAT_EQ(ok("letter-spacing: -2em").letter_spacing, -32.0F);
}

// `font-size: 0` は CSS Fonts 4 §2.5 の `<length-percentage [0,∞]>` で**有効**。
// 送りが 0 になるだけなので、エラーにも警告にもしない（負との境目はちょうどここ）。
TEST(StyleError, ZeroFontSizeIsValidAndMakesEverythingZero) {
  const Result<ComputedStyle> zero = inline_style("font-size: 0");
  ASSERT_TRUE(zero.has_value()) << (zero ? "" : zero.error().message);
  EXPECT_FLOAT_EQ(zero->font_size, 0.0F);

  // 組み合わせ: 0 を基準に em が解決される側もすべて 0 になる
  const Result<ComputedStyle> nested =
      inline_style("font-size: 0; padding: 2em; line-height: 1.5; letter-spacing: -0.05em");
  ASSERT_TRUE(nested.has_value()) << (nested ? "" : nested.error().message);
  EXPECT_FLOAT_EQ(nested->font_size, 0.0F);
  EXPECT_EQ(nested->padding, (Edges<float>{0, 0, 0, 0}));
  EXPECT_FLOAT_EQ(nested->letter_spacing, 0.0F);
  EXPECT_EQ(nested->line_height, (LineHeight{LineHeight::Kind::Number, 1.5F}));
}

// `-0` は `0` と等しいので CSS 上は有効。ただし**ダンプに `-0` を漏らさない**
// （同じ入力から同じバイト列、という約束の入口。DESIGN.md §3-5）。
TEST(StyleError, NegativeZeroIsAcceptedAndNeverPrintedAsMinusZero) {
  for (const std::string_view css :
       {"font-size: -0px", "font-size: -0", "padding: -0", "width: -0px", "margin-left: -0em",
        "letter-spacing: -0em", "font-size: 0; letter-spacing: -0.05em",
        "font-size: 0; padding: 2em; line-height: 1.5; letter-spacing: -0.05em"}) {
    SCOPED_TRACE(css);
    const html::Node tree = test_root(test_element("div", {test_attr("style", css)}));
    const Result<StyledNode> styled = resolve(tree);
    ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
    // プロパティ名の `-` の後ろは必ず英字なので、`-0` が出るのは数値だけ
    EXPECT_EQ(dump_json(*styled).find("-0"), std::string::npos) << dump_json(*styled);
  }
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
