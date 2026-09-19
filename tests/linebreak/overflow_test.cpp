#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// あふれ処理（ARCHITECTURE.md §3.4 (5) / JLREQ 3.8「行の調整処理」）。
// 追い出し（Oidashi）/ 追い込み（Oikomi）/ ぶら下げ（Burasage）と、
// break_anywhere・「禁則 > 幅」（ARCHITECTURE.md A4）。
namespace shashoku::linebreak {
namespace {

constexpr float kEm = test::kEm;
constexpr float kHalf = 0.5F * kEm;
constexpr float kQuarter = 0.25F * kEm;

struct Laid {
  std::vector<Item> items;
  Breaks breaks;

  [[nodiscard]] std::vector<std::string> texts() const { return test::line_texts(items, breaks); }
};

Laid run(const char* input, float width, const Config& config = {}) {
  Laid laid;
  laid.items = test::items_of(input);
  laid.breaks = LineBreaker(config).break_lines(laid.items, width);
  return laid;
}

// ------------------------------------------------------------ 追い出し（既定）

TEST(LineBreakOverflow, OidashiIsTheDefault) {
  const Laid laid = run("あいうえお。かき", 5 * kEm);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あいうえ", "お。かき"}));
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].hang, 0.0F);
  EXPECT_FALSE(laid.breaks.lines[0].overflows);
}

// ------------------------------------------------------------ ぶら下げ（Burasage）

TEST(LineBreakOverflow, BurasageHangsPunctuation) {
  // 行末に来た句読点 1 文字を行の外へ出す。width にはその幅を含めない。
  const Laid laid = run("あいうえお。かき", 5 * kEm, test::with_overflow(OverflowPolicy::Burasage));
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あいうえお。", "かき"}));
  ASSERT_EQ(laid.breaks.lines.size(), 2U);
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].width, 5 * kEm);
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].hang, kEm);
  EXPECT_EQ(laid.breaks.lines[0].content_end, 6U);  // ぶら下げた文字は content に含む
  EXPECT_FALSE(laid.breaks.lines[0].overflows);

  const Laid comma =
      run("あいうえお、かき", 5 * kEm, test::with_overflow(OverflowPolicy::Burasage));
  EXPECT_EQ(comma.texts(), (std::vector<std::string>{"あいうえお、", "かき"}));
  EXPECT_FLOAT_EQ(comma.breaks.lines[0].hang, kEm);
}

TEST(LineBreakOverflow, BurasageOnlyForPunctuation) {
  // 句読点以外（終わり括弧など）はぶら下げない → 追い出しになる。
  const Laid laid = run("あいうえお」かき", 5 * kEm, test::with_overflow(OverflowPolicy::Burasage));
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あいうえ", "お」かき"}));
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].hang, 0.0F);
}

TEST(LineBreakOverflow, BurasageNotUsedWhenItFits) {
  // 収まるならぶら下げない（ARCHITECTURE.md §3.4 (5)）。
  const Laid laid = run("あいうえ。かき", 5 * kEm, test::with_overflow(OverflowPolicy::Burasage));
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あいうえ。", "かき"}));
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].hang, 0.0F);
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].width, 5 * kEm);
}

// ------------------------------------------------------------ 追い込み（Oikomi）

TEST(LineBreakOverflow, OikomiSqueezesPunctuationSpacing) {
  // 行内の約物のアキ（始め括弧の前 / 終わり括弧の後ろ）を詰めて次の位置まで引き込む。
  const Laid laid = run("「あい」うえお", 5 * kEm, test::with_overflow(OverflowPolicy::Oikomi));
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"「あい」うえ", "お"}));
  ASSERT_EQ(laid.breaks.lines.size(), 2U);
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].width, 5 * kEm);
  EXPECT_FLOAT_EQ(laid.breaks.spacing[0].before, -kHalf);  // 「 の前を詰めきった
  EXPECT_FLOAT_EQ(laid.breaks.spacing[3].after, -kHalf);   // 」 の後ろを詰めきった
  EXPECT_FALSE(laid.breaks.lines[0].overflows);
}

TEST(LineBreakOverflow, OikomiDistributesProportionally) {
  // 詰め量は各約物の詰め可能量に比例配分する。必要量 8px を 2 か所で半分ずつ。
  const Laid laid = run("「あ」いうえお", 5.5F * kEm, test::with_overflow(OverflowPolicy::Oikomi));
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"「あ」いうえ", "お"}));
  EXPECT_FLOAT_EQ(laid.breaks.lines[0].width, 5.5F * kEm);
  EXPECT_FLOAT_EQ(laid.breaks.spacing[0].before, -kQuarter);
  EXPECT_FLOAT_EQ(laid.breaks.spacing[2].after, -kQuarter);
}

TEST(LineBreakOverflow, OikomiFallsBackToOidashi) {
  const Config oikomi = test::with_overflow(OverflowPolicy::Oikomi);
  {  // 詰められる約物が 1 つもない
    const Laid laid = run("あいうえおかき", 5 * kEm, oikomi);
    EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あいうえお", "かき"}));
  }
  {  // 中点の 1/4 + 1/4 = 8px では 16px 足りない
    const Laid laid = run("あ・いうえおか", 5 * kEm, oikomi);
    EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あ・いうえ", "おか"}));
    EXPECT_FLOAT_EQ(laid.breaks.spacing[1].before, 0.0F);
    EXPECT_FLOAT_EQ(laid.breaks.spacing[1].after, 0.0F);
  }
}

// ------------------------------------------------- 禁則 > 幅（ARCHITECTURE.md A4）

TEST(LineBreakOverflow, ProhibitionBeatsWidth) {
  // 幅 1em の箱に「あ。」を流したら、割らずにはみ出す。行頭に句点は出さない。
  for (const OverflowPolicy policy :
       {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
    SCOPED_TRACE(static_cast<int>(policy));
    const Laid laid = run("あ。", kEm, test::with_overflow(policy));
    EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あ。"}));
    ASSERT_EQ(laid.breaks.lines.size(), 1U);
    EXPECT_TRUE(laid.breaks.lines[0].overflows);
    // 行末のアキ詰めだけは効くので 2em ではなく 1.5em。
    EXPECT_FLOAT_EQ(laid.breaks.lines[0].width, 1.5F * kEm);
  }
}

TEST(LineBreakOverflow, VeryNarrowWidth) {
  // 1 文字も入らない幅。各行 1 アイテムになり、すべて overflows。
  const Laid laid = run("あいう", 0.5F * kEm);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あ", "い", "う"}));
  for (const Line& line : laid.breaks.lines) {
    EXPECT_TRUE(line.overflows);
    EXPECT_FLOAT_EQ(line.width, kEm);
  }
}

TEST(LineBreakOverflow, LongUnbreakableRunOverflows) {
  // 分割可能位置がないので 1 行のままはみ出す（break_anywhere なし）。
  const Laid laid = run("ABCDEFGHIJ", 2.5F * kEm);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"ABCDEFGHIJ"}));
  EXPECT_TRUE(laid.breaks.lines[0].overflows);
}

// ------------------------------------------------------------- break_anywhere

TEST(LineBreakOverflow, BreakAnywhereSplitsLongRun) {
  Config config;
  config.break_anywhere = true;
  const Laid laid = run("ABCDEFGHIJ", 2.5F * kEm, config);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"ABCDE", "FGHIJ"}));
  for (const Line& line : laid.breaks.lines) {
    EXPECT_FALSE(line.overflows);
  }
}

TEST(LineBreakOverflow, BreakAnywhereStillKeepsProhibitions) {
  // クラスタ境界で割るときも、行頭禁則を守れる位置があればそちらを選ぶ。
  // "ABC,DEF" には分割可能位置が 1 つもない。3 文字ぶんの幅なら "ABC" で割れるが、
  // それだと次の行が "," で始まってしまうので 1 文字短い "AB" を選ぶ。
  Config config;
  config.break_anywhere = true;
  const Laid laid = run("ABC,DEF", 1.5F * kEm, config);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"AB", "C,D", "EF"}));
  for (const Line& line : laid.breaks.lines) {
    EXPECT_FALSE(line.overflows);
  }
}

TEST(LineBreakOverflow, BreakAnywhereOnlyWhenNothingFits) {
  // 収まる分割可能位置があるときは発動しない（Config::break_anywhere のコメント）。
  Config config;
  config.break_anywhere = true;
  const Laid laid = run("あいうえお。かき", 5 * kEm, config);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"あいうえ", "お。かき"}));
}

TEST(LineBreakOverflow, BreakAnywhereBreaksProhibitionAsLastResort) {
  // 守れる位置が 1 つもなければ破る（line_breaker.hpp の Config::break_anywhere）。
  // "A。BC" を 0.5 文字ぶんの幅で流すと、1 アイテムずつに割るしかない。
  Config config;
  config.break_anywhere = true;
  const Laid laid = run("A。BC", 0.5F * kEm, config);
  EXPECT_EQ(laid.texts(), (std::vector<std::string>{"A", "。", "B", "C"}));
}

}  // namespace
}  // namespace shashoku::linebreak
