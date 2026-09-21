#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// 計算量の回帰テスト（issue #4 / #10-3 / ARCHITECTURE.md A21・A24）。
//
// 時間ではなく **回数** で見る: 計測カウンタ（line_breaker.hpp の Counters）が数えた作業量が、
// アイテム数 N に対して線形の範囲（定数 × N 以下）に収まることを確かめる。
// 時間で測ると環境依存でぶれるうえ、O(N×L) が「遅いが通る」状態で気づけない。
//
// 狙うのは「行ごとに段落の残り全体を舐める」形: 幅を 1 文字ぶんにすると行数 L が N に比例するので、
// 1 行あたり N の仕事をしていると N×L に膨らむ。
namespace shashoku::linebreak {
namespace {

constexpr float kEm = test::kEm;
constexpr std::size_t kItems = 20000;

// 「あ」だけの長文。幅 1em に流すと 1 行 1 文字になる。
std::vector<Item> long_text(char32_t cp, float advance, std::size_t count = kItems) {
  std::vector<Item> items(count);
  for (Item& item : items) {
    item.kind = ItemKind::Text;
    item.cp = cp;
    item.advance = advance;
    item.em = advance;
  }
  return items;
}

// 線形の目安。定数は「N に比例していれば余裕で入るが、N×L なら絶対に入らない」幅で選ぶ。
// 実測はどれも 11N 以下、直す前は 7,500N〜20,000N だったので、32N ならどちらとも間違えない
// （定数倍の増減でテストが落ちない程度に緩く、二乗を必ず捕まえる程度に厳しく）。
void expect_linear(const Counters& counters, std::size_t n) {
  const std::uint64_t budget = 32 * static_cast<std::uint64_t>(n);
  EXPECT_LE(counters.mandatory_scan, budget) << "次の強制改行を行ごとに前方走査している";
  EXPECT_LE(counters.line_scan, budget) << "行の決定で段落の残りを舐めている";
  EXPECT_LE(counters.anywhere_scan, budget) << "緊急分割の候補探しで段落の残りを舐めている";
  EXPECT_LE(counters.rule_scan, budget) << "分割可能位置の判定で前に遡りすぎている";
  EXPECT_LE(counters.width_items, budget) << "幅の計算を行ごとにやり直している";
}

TEST(LineBreakComplexity, NarrowLongParagraphIsLinear) {
  // #4 の再現そのもの: <br> のない長文を幅 1em に流す。L = N。
  const std::vector<Item> items = long_text(U'あ', kEm);
  Counters counters;
  const Breaks breaks = LineBreaker().break_lines(items, kEm, &counters);

  ASSERT_EQ(breaks.lines.size(), kItems);  // 前提: 1 行 1 文字
  EXPECT_EQ(counters.lines, kItems);
  expect_linear(counters, kItems);
}

TEST(LineBreakComplexity, OverflowPoliciesAreLinear) {
  // 追い込み・ぶら下げの経路でも同じ（句点を混ぜて約物の処理を通す）。
  std::vector<Item> items = long_text(U'あ', kEm);
  for (std::size_t i = 3; i < items.size(); i += 4) {
    items[i].cp = U'。';
  }
  for (const OverflowPolicy policy :
       {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
    SCOPED_TRACE(static_cast<int>(policy));
    Counters counters;
    const Breaks breaks =
        LineBreaker(test::with_overflow(policy)).break_lines(items, kEm, &counters);
    EXPECT_EQ(counters.lines, breaks.lines.size());
    expect_linear(counters, kItems);
  }
}

TEST(LineBreakComplexity, BreakAnywhereIsLinear) {
  // 分割可能位置が 1 つもない長い欧文 + overflow-wrap: anywhere。
  // 緊急分割の経路は、行ごとに「次の候補」を段落の末尾まで探しに行きやすい。
  const std::vector<Item> items = long_text(U'A', 0.5F * kEm);
  Config config;
  config.break_anywhere = true;
  Counters counters;
  const Breaks breaks = LineBreaker(config).break_lines(items, 0.5F * kEm, &counters);

  ASSERT_EQ(breaks.lines.size(), kItems);  // 1 行 1 文字に割れている
  expect_linear(counters, kItems);
}

TEST(LineBreakComplexity, UnbreakableRunWithoutAnywhereIsLinear) {
  // 同じ入力を anywhere なしで。1 行に収まらないまま 1 行で出す（A4 禁則 > 幅）。
  const std::vector<Item> items = long_text(U'A', 0.5F * kEm);
  Counters counters;
  const Breaks breaks = LineBreaker().break_lines(items, 0.5F * kEm, &counters);

  ASSERT_EQ(breaks.lines.size(), 1U);
  EXPECT_TRUE(breaks.lines[0].overflows);
  expect_linear(counters, kItems);
}

TEST(LineBreakComplexity, ForcedBreaksAreLinear) {
  // 1 文字ごとに <br>。強制改行の経路（mandatory_limit）を毎行通る。
  std::vector<Item> items = long_text(U'あ', kEm, 2 * kItems);
  for (std::size_t i = 1; i < items.size(); i += 2) {
    items[i].kind = ItemKind::ForcedBreak;
    items[i].cp = U'\n';
    items[i].advance = 0.0F;
  }
  Counters counters;
  const Breaks breaks = LineBreaker().break_lines(items, 100.0F * kEm, &counters);

  ASSERT_EQ(breaks.lines.size(), kItems);
  expect_linear(counters, 2 * kItems);
}

TEST(LineBreakComplexity, IntrinsicWidthsAreLinear) {
  // min-content（分割不能な最長区間）と max-content（kUnbounded）も N に線形。
  const std::vector<Item> items = long_text(U'あ', kEm);
  {
    Counters counters;
    EXPECT_FLOAT_EQ(LineBreaker().min_content_width(items, &counters), kEm);
    expect_linear(counters, kItems);
  }
  {
    Counters counters;
    const Breaks breaks = LineBreaker().break_lines(items, kUnbounded, &counters);
    EXPECT_EQ(breaks.lines.size(), 1U);
    expect_linear(counters, kItems);
  }
}

TEST(LineBreakComplexity, RegionalIndicatorRunIsLinear) {
  // LB30a は「直前に並ぶ RI の個数」を見る。位置ごとに遡ると、国旗が並んだだけで二乗になる。
  const std::vector<Item> items = long_text(U'\U0001F1EF', kEm);
  Counters counters;
  const std::vector<bool> opportunities = LineBreaker().break_opportunities(items, &counters);

  ASSERT_EQ(opportunities.size(), kItems);
  EXPECT_FALSE(opportunities[1]);  // 2 個で 1 組（🇯🇯）
  EXPECT_TRUE(opportunities[2]);   // 3 個目は新しい組の始まり
  expect_linear(counters, kItems);
}

TEST(LineBreakComplexity, SpaceRunIsLinear) {
  // 空白越しの規則（LB8 / LB14 / LB16 / LB17）は直前の空白列を遡る。
  std::vector<Item> items = long_text(U' ', 0.5F * kEm);
  items.front().cp = U'A';
  items.back().cp = U'B';
  Counters counters;
  const std::vector<bool> opportunities = LineBreaker().break_opportunities(items, &counters);

  ASSERT_EQ(opportunities.size(), kItems);
  expect_linear(counters, kItems);
}

TEST(LineBreakComplexity, CountersDoNotChangeOutput) {
  // 計測の約束（A21）: カウンタを渡しても渡さなくても出力は同じ。
  std::vector<Item> items = test::items_of("あいう「えお」。ABCDEFGH……￥1,200 かきくけこ\nさし");
  items[3].break_anywhere = true;
  items[4].strictness = Strictness::Loose;
  for (const OverflowPolicy policy :
       {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
    const LineBreaker breaker(test::with_overflow(policy));
    for (const float width : {kEm, 3.0F * kEm, 7.5F * kEm, kUnbounded}) {
      Counters counters;
      EXPECT_TRUE(breaker.break_lines(items, width) ==
                  breaker.break_lines(items, width, &counters));
      EXPECT_EQ(breaker.break_opportunities(items), breaker.break_opportunities(items, &counters));
      EXPECT_FLOAT_EQ(breaker.min_content_width(items),
                      breaker.min_content_width(items, &counters));
    }
  }
}

}  // namespace
}  // namespace shashoku::linebreak
