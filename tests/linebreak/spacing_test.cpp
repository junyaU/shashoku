#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// 約物の空き（JLREQ 3.1.2〜3.1.5 / ARCHITECTURE.md §3.4 (4)）。
// 全角の約物は 1em の送りのうち半分（中点類は両側 1/4 ずつ）が字面のない空きで、
// 行分割器はその空きを減らす方向にだけ触る（Spacing は 0 か負の値）。
namespace shashoku::linebreak {
namespace {

constexpr float kEm = test::kEm;
constexpr float kHalf = 0.5F * kEm;      // 二分アキ = 8px
constexpr float kQuarter = 0.25F * kEm;  // 四分アキ = 4px

// 1 行に収まる幅で流して、その行の Spacing と幅を取り出す。
struct SingleLine {
  std::vector<Item> items;
  Breaks breaks;

  [[nodiscard]] const Spacing& spacing(std::size_t i) const { return breaks.spacing[i]; }
  [[nodiscard]] float width() const { return breaks.lines.front().width; }
};

SingleLine lay_out(const char* input, const Config& config = {}, float width = 1000.0F) {
  SingleLine result;
  result.items = test::items_of(input);
  result.breaks = LineBreaker(config).break_lines(result.items, width);
  return result;
}

void expect_spacing(const SingleLine& line, std::size_t index, float before, float after) {
  SCOPED_TRACE("item " + std::to_string(index));
  EXPECT_FLOAT_EQ(line.spacing(index).before, before);
  EXPECT_FLOAT_EQ(line.spacing(index).after, after);
}

// ------------------------------------------------- 連続する約物のアキ詰め（JLREQ 3.1.4）

TEST(LineBreakSpacing, CollapseClosingThenOpening) {
  // 終わり括弧類・句読点 → 始め括弧類: 二分 + 二分 = 全角アキになるので半角ぶん詰める。
  const SingleLine line = lay_out("あ」「い");
  ASSERT_EQ(line.breaks.lines.size(), 1U);
  expect_spacing(line, 0, 0.0F, 0.0F);
  expect_spacing(line, 1, 0.0F, -kHalf);  // 」 の後ろのアキを捨てる
  expect_spacing(line, 2, 0.0F, 0.0F);    // 「 の前のアキは残す
  EXPECT_FLOAT_EQ(line.width(), 4 * kEm - kHalf);

  const SingleLine period = lay_out("あ。「い");
  expect_spacing(period, 1, 0.0F, -kHalf);
  const SingleLine comma = lay_out("あ、「い");
  expect_spacing(comma, 1, 0.0F, -kHalf);
}

TEST(LineBreakSpacing, CollapseClosingRun) {
  // 終わり括弧類・句読点どうし: 間はベタ（アキなし）にする。
  const SingleLine line = lay_out("あ」」い");
  expect_spacing(line, 1, 0.0F, -kHalf);
  expect_spacing(line, 2, 0.0F, 0.0F);  // 最後の 」 の後ろのアキは残る
  EXPECT_FLOAT_EQ(line.width(), 4 * kEm - kHalf);

  const SingleLine mixed = lay_out("「あ。」い");
  expect_spacing(mixed, 2, 0.0F, -kHalf);  // 。 の後ろのアキを捨てる
  expect_spacing(mixed, 3, 0.0F, 0.0F);
  EXPECT_FLOAT_EQ(mixed.width(), 5 * kEm - kHalf);
}

TEST(LineBreakSpacing, CollapseOpeningRun) {
  // 始め括弧類どうし: 2 つ目の前のアキを捨てる。
  const SingleLine line = lay_out("あ「「い");
  expect_spacing(line, 1, 0.0F, 0.0F);
  expect_spacing(line, 2, -kHalf, 0.0F);
  EXPECT_FLOAT_EQ(line.width(), 4 * kEm - kHalf);
}

TEST(LineBreakSpacing, NoCollapseForOrdinaryNeighbours) {
  // 約物と普通の文字の間のアキは組版上必要なので触らない。
  const SingleLine line = lay_out("あ」い「う");
  for (std::size_t i = 0; i < line.items.size(); ++i) {
    expect_spacing(line, i, 0.0F, 0.0F);
  }
  EXPECT_FLOAT_EQ(line.width(), 5 * kEm);
}

TEST(LineBreakSpacing, CollapseCanBeDisabled) {
  Config config;
  config.collapse_punctuation_spacing = false;
  const SingleLine line = lay_out("あ」「い", config);
  expect_spacing(line, 1, 0.0F, 0.0F);
  EXPECT_FLOAT_EQ(line.width(), 4 * kEm);
}

TEST(LineBreakSpacing, CollapseDoesNotCrossLines) {
  // 行をまたいだペアには適用しない（ARCHITECTURE.md §3.4 (4)）。
  const SingleLine line = lay_out("あ」「い", {}, 2 * kEm);
  ASSERT_EQ(line.breaks.lines.size(), 2U);
  EXPECT_EQ(test::line_texts(line.items, line.breaks), (std::vector<std::string>{"あ」", "「い"}));
  expect_spacing(line, 1, 0.0F, 0.0F);  // 「 は次の行なので詰めない
  expect_spacing(line, 2, 0.0F, 0.0F);
  EXPECT_FLOAT_EQ(line.breaks.lines[0].width, 2 * kEm);
  EXPECT_FLOAT_EQ(line.breaks.lines[1].width, 2 * kEm);
}

// -------------------------------------------------------- 行末のアキ詰め / 天付き

TEST(LineBreakSpacing, TrimLineEnd) {
  // 行末に来た終わり括弧・句読点が収まらないとき、後ろの二分アキを捨てて収める。
  const SingleLine line = lay_out("あい。", {}, 2.5F * kEm);
  ASSERT_EQ(line.breaks.lines.size(), 1U);
  expect_spacing(line, 2, 0.0F, -kHalf);
  EXPECT_FLOAT_EQ(line.width(), 2.5F * kEm);
  EXPECT_FALSE(line.breaks.lines[0].overflows);

  // 収まるなら詰めない。
  const SingleLine roomy = lay_out("あい。", {}, 3 * kEm);
  expect_spacing(roomy, 2, 0.0F, 0.0F);
  EXPECT_FLOAT_EQ(roomy.width(), 3 * kEm);
}

TEST(LineBreakSpacing, TrimLineEndCanBeDisabled) {
  Config config;
  config.trim_line_end = false;
  const SingleLine line = lay_out("あい。", config, 2.5F * kEm);
  EXPECT_EQ(test::line_texts(line.items, line.breaks), (std::vector<std::string>{"あ", "い。"}));
  expect_spacing(line, 2, 0.0F, 0.0F);
}

TEST(LineBreakSpacing, CollapseAndTrimTogether) {
  // 「。」」が行末に並ぶと、アキ詰め（二分）と行末の半角化（二分）で全角ぶん縮む。
  const SingleLine line = lay_out("あいうえお。」かき", {}, 6 * kEm);
  EXPECT_EQ(test::line_texts(line.items, line.breaks),
            (std::vector<std::string>{"あいうえお。」", "かき"}));
  expect_spacing(line, 5, 0.0F, -kHalf);  // 。 と 」 の間（JLREQ 3.1.4）
  expect_spacing(line, 6, 0.0F, -kHalf);  // 行末の 」 の後ろ
  EXPECT_FLOAT_EQ(line.breaks.lines[0].width, 6 * kEm);
}

TEST(LineBreakSpacing, TrimLineStart) {
  // 天付き: 行頭の始め括弧の前のアキを捨てる。既定は捨てない（CSS の既定と同じ）。
  const SingleLine plain = lay_out("「あ」");
  expect_spacing(plain, 0, 0.0F, 0.0F);
  EXPECT_FLOAT_EQ(plain.width(), 3 * kEm);

  Config config;
  config.trim_line_start = true;
  const SingleLine trimmed = lay_out("「あ」", config);
  expect_spacing(trimmed, 0, -kHalf, 0.0F);
  EXPECT_FLOAT_EQ(trimmed.width(), 3 * kEm - kHalf);
}

TEST(LineBreakSpacing, TrimLineStartAppliesToEachLine) {
  Config config;
  config.trim_line_start = true;
  const SingleLine line = lay_out("あい「うえ", config, 3 * kEm);
  ASSERT_EQ(line.breaks.lines.size(), 2U);
  EXPECT_EQ(test::line_texts(line.items, line.breaks),
            (std::vector<std::string>{"あい", "「うえ"}));
  expect_spacing(line, 2, -kHalf, 0.0F);
  EXPECT_FLOAT_EQ(line.breaks.lines[1].width, 3 * kEm - kHalf);
}

// -------------------------------------------------------------- 空き量の単位

TEST(LineBreakSpacing, MiddleDotHasQuarterSpaceOnBothSides) {
  // 中点類は両側 1/4 ずつ。追い込みで詰めると左右に分かれて現れる。
  Config config;
  config.overflow = OverflowPolicy::Oikomi;
  const SingleLine line = lay_out("あ・いう", config, 3.5F * kEm);
  ASSERT_EQ(line.breaks.lines.size(), 1U);
  expect_spacing(line, 1, -kQuarter, -kQuarter);
  EXPECT_FLOAT_EQ(line.width(), 3.5F * kEm);
}

TEST(LineBreakSpacing, SpacingUsesItemEm) {
  // アキは Item::em から計算する。em が 0 のときは advance を 1em とみなす（契約ヘッダ）。
  std::vector<Item> items = test::items_of("あ」「い", 32.0F);
  for (Item& item : items) {
    item.advance = 32.0F;
  }
  const Breaks breaks = LineBreaker().break_lines(items, 1000.0F);
  EXPECT_FLOAT_EQ(breaks.spacing[1].after, -16.0F);

  for (Item& item : items) {
    item.em = 0.0F;
  }
  const Breaks fallback = LineBreaker().break_lines(items, 1000.0F);
  EXPECT_FLOAT_EQ(fallback.spacing[1].after, -16.0F);
}

TEST(LineBreakSpacing, SpacingVectorMatchesItemCount) {
  const SingleLine line = lay_out("あいうえお。かき", {}, 5 * kEm);
  EXPECT_EQ(line.breaks.spacing.size(), line.items.size());
}

}  // namespace
}  // namespace shashoku::linebreak
