#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "html/dom.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"
#include "style/resolver.hpp"
#include "style_test_dom.hpp"

// 計算値の長さの上限（ARCHITECTURE.md A-new / issue #19）。
//
// style の出口では「font-size を除くすべての長さが有限で、絶対値が length_px 以内」である。
// `1e38em` のように em の乗算で float をあふれる入力は、ここで宣言の位置つきのエラーになる
// （以前は inf のまま layout まで流れ、raster が黙って命令を捨てて「その要素だけ消えた PNG」が
// 終了コード 0 で返っていた）。
namespace shashoku::style {
namespace {

// `<div style="...">` を、長さの上限を指定して解決する。
Result<ComputedStyle> resolve_with_limit(std::string_view declarations, float max_length_px) {
  const html::Node tree = test_root(test_element("div", {test_attr("style", declarations)}));
  Result<StyledNode> styled = resolve(tree, kMaxStyleRules, max_length_px);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (styled->children.empty()) {
    return fail(ErrorKind::Internal, "the test element was dropped from the tree");
  }
  return styled->children.front().style;
}

// 失敗を期待し、エラーを返す（成功したらその場でテストを落とす）。
Error style_failure(std::string_view declarations, float max_length_px = kMaxLengthPx) {
  const Result<ComputedStyle> style = resolve_with_limit(declarations, max_length_px);
  if (style) {
    ADD_FAILURE() << "エラーになるはずが成功した: " << declarations;
    return Error{};
  }
  return style.error();
}

// `1e38em` を通すと `em x font-size` が float をあふれる 11 プロパティ（issue #19 の表）。
// どれも「エラーの種類は limit-exceeded、位置はその宣言」でなければならない。
struct OverflowCase {
  std::string_view css;      // 宣言 1 つ
  std::string_view mention;  // メッセージに出るべきプロパティ名（longhand）
};

const std::vector<OverflowCase>& em_overflow_cases() {
  static const std::vector<OverflowCase> cases = {
      {"padding: 1e38em", "padding-top"},
      {"padding-left: 1e38em", "padding-left"},
      {"margin: 1e38em", "margin-top"},
      {"margin: -1e38em", "margin-top"},
      {"width: 1e38em", "width"},
      {"height: 1e38em", "height"},
      {"border: 1e38em solid #000", "border-width"},
      {"border-radius: 1e38em", "border-radius"},
      {"letter-spacing: 1e38em", "letter-spacing"},
      {"letter-spacing: -1e38em", "letter-spacing"},
      {"line-height: 1e38em", "line-height"},
      {"row-gap: 1e38em", "row-gap"},
      {"column-gap: 1e38em", "column-gap"},
      {"flex-basis: 1e38em", "flex-basis"},
  };
  return cases;
}

TEST(StyleLengthLimit, EmOverflowIsRejected) {
  for (const OverflowCase& test : em_overflow_cases()) {
    SCOPED_TRACE(test.css);
    const Error error = style_failure(test.css);
    EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
    EXPECT_NE(error.message.find(test.mention), std::string::npos) << error.message;
    // A25 の流儀: どの RenderLimits のフィールドで緩められるかを書く
    EXPECT_NE(error.message.find("RenderLimits::length_px"), std::string::npos) << error.message;
  }
}

// `<style>` の中ではオフセットを入力 HTML 上の位置に写すので、位置は「その宣言」を指す
// （同じ規則に別の宣言が並んでいても、超えた方を指す）。
TEST(StyleLengthLimit, LocationPointsAtTheDeclaration) {
  constexpr std::string_view kCss = "div { color: red; padding-left: 1e38em }";
  const html::Node tree = test_root(test_style_element(kCss), test_element("div"));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value()) << "エラーになるはず";
  EXPECT_EQ(styled.error().kind, ErrorKind::LimitExceeded) << styled.error().message;
  ASSERT_TRUE(styled.error().location.has_value());
  // Declaration::location は値の先頭（declaration.hpp）
  EXPECT_EQ(styled.error().location.value_or(SourceLocation{}).column,
            static_cast<std::uint32_t>(kCss.find("1e38em") + 1))
      << styled.error().message;
}

// `style` 属性の中は宣言ごとの位置を持たない（実体参照で桁がずれるので css_parser が
// 写さない）。その場合は属性の位置を指す。これは既存の流儀（writing-mode など）と同じ。
TEST(StyleLengthLimit, InlineStyleLocationIsTheAttribute) {
  constexpr SourceLocation kAttr{.offset = 40, .line = 3, .column = 6};
  const html::Node tree =
      test_root(test_element("div", {test_attr("style", "padding-left: 1e38em", kAttr)}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value()) << "エラーになるはず";
  EXPECT_EQ(styled.error().location.value_or(SourceLocation{}), kAttr) << styled.error().message;
}

// px で直接書いた巨大な値も同じ上限で止まる（style では有限なので、以前は素通りしていた）。
TEST(StyleLengthLimit, HugeFinitePxIsRejected) {
  const Error error = style_failure("padding: 3e38px");
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::length_px"), std::string::npos) << error.message;
}

// `line-height` の倍率は px ではないが、倍率 x font-size が上限以内かは style で分かる。
TEST(StyleLengthLimit, LineHeightNumberIsCheckedAgainstFontSize) {
  const Error error = style_failure("line-height: 1e38");
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
  EXPECT_NE(error.message.find("line-height"), std::string::npos) << error.message;
}

// 倍率は倍率のまま継承するので、親では収まっていても子の font-size で超えることがある。
TEST(StyleLengthLimit, InheritedLineHeightNumberIsRecheckedOnTheChild) {
  const html::Node tree =
      test_root(test_parent("div", {test_attr("style", "line-height: 1e6")},
                            test_element("div", {test_attr("style", "font-size: 100px")})));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value()) << "1e6 x 100px = 1e8 px は上限を超える";
  EXPECT_EQ(styled.error().kind, ErrorKind::LimitExceeded) << styled.error().message;
}

// 常識的な値は従来どおり通り、計算値も変わらない（回帰）。
TEST(StyleLengthLimit, OrdinaryValuesStillResolve) {
  const Result<ComputedStyle> style =
      inline_style("padding: 1000px; width: 5000px; letter-spacing: -2px; line-height: 1.8");
  ASSERT_TRUE(style.has_value()) << (style ? std::string{} : style.error().message);
  EXPECT_EQ(style->padding.top, 1000.0F);
  EXPECT_EQ(style->width, Dimension::px(5000.0F));
  EXPECT_EQ(style->letter_spacing, -2.0F);
  EXPECT_EQ(style->line_height.kind, LineHeight::Kind::Number);
  EXPECT_EQ(style->line_height.value, 1.8F);
}

// 上限そのものは通り、少しでも超えると落ちる。
TEST(StyleLengthLimit, Boundary) {
  const Result<ComputedStyle> ok = resolve_with_limit("padding: 100px", 100.0F);
  ASSERT_TRUE(ok.has_value()) << (ok ? std::string{} : ok.error().message);
  EXPECT_EQ(ok->padding.top, 100.0F);

  const Error error = style_failure("padding: 101px", 100.0F);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
}

// 負の値は絶対値で見る（margin と letter-spacing だけが負を取れる）。
TEST(StyleLengthLimit, NegativeLengthsUseTheAbsoluteValue) {
  const Result<ComputedStyle> ok = resolve_with_limit("margin: -100px", 100.0F);
  ASSERT_TRUE(ok.has_value()) << (ok ? std::string{} : ok.error().message);

  const Error error = style_failure("margin: -101px", 100.0F);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
}

// font-size は A25 の font_size_device_px が api で止める（そちらの方が厳しく、scale も見る）。
// style は「有限であること」だけを見て、エラーの種類と位置は従来どおり要素を指す。
TEST(StyleLengthLimit, NonFiniteFontSizeStopsAtTheElement) {
  const Error error = style_failure("font-size: 1e38em");
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
  EXPECT_NE(error.message.find("font-size"), std::string::npos) << error.message;
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 1U) << error.message;
}

// font-size 自体は length_px では縛らない（1e7 px は length_px 以内だが font_size_device_px
// 超え）。
TEST(StyleLengthLimit, LargeButFiniteFontSizeIsLeftToTheApi) {
  const Result<ComputedStyle> style = resolve_with_limit("font-size: 1000000px", kMaxLengthPx);
  ASSERT_TRUE(style.has_value()) << (style ? std::string{} : style.error().message);
  EXPECT_EQ(style->font_size, 1000000.0F);
}

// <img> の width / height 属性も layout に渡る長さなので、同じ上限で見る。
TEST(StyleLengthLimit, ImageAttributesAreBounded) {
  const html::Node tree =
      test_root(test_element("img", {test_attr("src", "icon"), test_attr("width", "300000000")}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_FALSE(styled.has_value()) << "3e8 px は length_px を超える";
  EXPECT_EQ(styled.error().kind, ErrorKind::LimitExceeded) << styled.error().message;
}

// ---- パーサが弾く範囲（float にできない数値）--------------------------------

// `1e39px` は float にできないのでパーサが弾く。種類は limit-exceeded に寄せてある
// （利用者から見て `1e39px` と `1e38em` が別種なのは説明しづらい。A-new）。
TEST(StyleLengthLimit, OutOfRangeNumbersAreLimitExceeded) {
  constexpr std::array<std::string_view, 7> kCases = {{
      "padding: 1e39px",
      "padding: 1e400px",
      "padding: 1e39em",
      "width: 1e39%",
      "line-height: 1e39",
      "flex-grow: 1e39",
      "flex-shrink: 1e39",
  }};
  for (const std::string_view css : kCases) {
    SCOPED_TRACE(css);
    const Error error = style_failure(css);
    EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
    EXPECT_TRUE(error.location.has_value());
  }
}

// メッセージの誤り（issue #19）: 範囲外を「負の数」と言わない。
TEST(StyleLengthLimit, OutOfRangeMessagesDoNotSayNegative) {
  const Error line_height = style_failure("line-height: 1e39");
  EXPECT_EQ(line_height.message.find("negative"), std::string::npos) << line_height.message;
  EXPECT_NE(line_height.message.find("range"), std::string::npos) << line_height.message;

  const Error grow = style_failure("flex-grow: 1e39");
  EXPECT_EQ(grow.message.find("non-negative"), std::string::npos) << grow.message;
  EXPECT_NE(grow.message.find("range"), std::string::npos) << grow.message;
}

// 本当に負の値は従来どおり「負の数は不可」と言う（UnsupportedValue のまま）。
TEST(StyleLengthLimit, ActuallyNegativeValuesKeepTheirMessage) {
  const Error line_height = style_failure("line-height: -1");
  EXPECT_EQ(line_height.kind, ErrorKind::UnsupportedValue) << line_height.message;
  EXPECT_NE(line_height.message.find("negative"), std::string::npos) << line_height.message;

  const Error grow = style_failure("flex-grow: -1");
  EXPECT_EQ(grow.kind, ErrorKind::UnsupportedValue) << grow.message;
  EXPECT_NE(grow.message.find("non-negative"), std::string::npos) << grow.message;
}

// `inf` / `nan` という字句は数値にならない（従来どおり UnsupportedValue）。
TEST(StyleLengthLimit, InfAndNanIdentifiersAreStillUnsupportedValues) {
  for (const std::string_view css : {"padding: infpx", "padding: inf", "padding: nan"}) {
    SCOPED_TRACE(css);
    const Error error = style_failure(css);
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedValue) << error.message;
  }
}

}  // namespace
}  // namespace shashoku::style
