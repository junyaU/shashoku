#include <array>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// ランダムな宣言文字列を流して、落ちずに「成功かエラー」を必ず返すことを確かめる
// （DESIGN.md §10-4 のファジングを、種を固定した性質テストとして通常のテストに含める）。

namespace shashoku::style {
namespace {

// CSS でよく出る記号と、対応 / 非対応のプロパティ名・キーワードの断片。
// 末尾の 3 バイトは「あ」の UTF-8。1 バイトずつ拾うと不正な UTF-8 にもなる（落ちないこと）。
constexpr std::string_view kAlphabet =
    "abcdefgilmnoprstuwxyz0123456789-_:;{}()[]#.,%/*'\"!+ \n\t<>=@\\\xE3\x81\x82";

constexpr auto kFragments = std::to_array<std::string_view>({
    "color", "margin",     "display", "font-size", "border", "flex",   "writing-mode",
    "float", "px",         "em",      "rem",       "%",      "auto",   "inherit",
    "red",   "rgb(1,2,3)", "#abc",    "solid",     "1.5",    "-2",     "!important",
    "grid",  "block",      "none",    "initial",   "unset",  "normal", "calc(1px)",
});

std::string random_css(std::mt19937& rng) {
  std::uniform_int_distribution<std::size_t> length(0, 64);
  std::uniform_int_distribution<std::size_t> letter(0, kAlphabet.size() - 1);
  std::uniform_int_distribution<int> mode(0, 3);
  std::uniform_int_distribution<std::size_t> fragment(0, kFragments.size() - 1);

  std::string out;
  const std::size_t count = length(rng);
  for (std::size_t i = 0; i < count; ++i) {
    if (mode(rng) == 0) {
      out += kFragments[fragment(rng)];
    } else {
      out += kAlphabet[letter(rng)];
    }
  }
  return out;
}

TEST(StyleFuzz, RandomInlineDeclarationsAlwaysReturn) {
  // 種は固定する（DESIGN.md §3-5: 出力を乱数に依存させない。再現できない失敗を作らない）
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937 rng(20260919);
  for (int i = 0; i < 3000; ++i) {
    const std::string css = random_css(rng);
    SCOPED_TRACE(css);
    const Result<ComputedStyle> style = inline_style(css);
    if (!style) {
      EXPECT_FALSE(style.error().message.empty());
    }
  }
}

TEST(StyleFuzz, RandomStylesheetsAlwaysReturn) {
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): 種は固定（DESIGN.md §3-5）
  std::mt19937 rng(20260920);
  for (int i = 0; i < 3000; ++i) {
    const std::string css = random_css(rng);
    SCOPED_TRACE(css);
    const Result<ComputedStyle> style = sheet_style(css);
    if (!style) {
      EXPECT_FALSE(style.error().message.empty());
    }
  }
}

TEST(StyleFuzz, RandomDeclarationsInsideRulesAlwaysReturn) {
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): 種は固定（DESIGN.md §3-5）
  std::mt19937 rng(20260921);
  for (int i = 0; i < 3000; ++i) {
    const std::string css = "div { " + random_css(rng) + " }";
    SCOPED_TRACE(css);
    const Result<ComputedStyle> style = sheet_style(css);
    if (!style) {
      EXPECT_FALSE(style.error().message.empty());
    }
  }
}

TEST(StyleFuzz, DeeplyNestedFunctionsAreRejectedNotCrashed) {
  std::string css = "color: ";
  for (int i = 0; i < 200; ++i) {
    css += "rgb(";
  }
  const Result<ComputedStyle> style = inline_style(css);
  ASSERT_FALSE(style.has_value());
  EXPECT_EQ(style.error().kind, ErrorKind::CssParse);
}

TEST(StyleFuzz, SameInputGivesTheSameResult) {
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): 種は固定（DESIGN.md §3-5）
  std::mt19937 rng(20260922);
  for (int i = 0; i < 500; ++i) {
    const std::string css = random_css(rng);
    SCOPED_TRACE(css);
    const Result<ComputedStyle> first = inline_style(css);
    const Result<ComputedStyle> second = inline_style(css);
    ASSERT_EQ(first.has_value(), second.has_value());
    if (first) {
      EXPECT_EQ(*first, *second);
    } else {
      EXPECT_EQ(first.error(), second.error());
    }
  }
}

}  // namespace
}  // namespace shashoku::style
