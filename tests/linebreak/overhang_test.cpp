#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// はみ出してよい量（Item::overhang_before / overhang_after）。ルビの掛け（JLREQ 3.3.8）を
// 行分割器の側から見たもの。行分割器は「掛けてよい相手か」を知らず、**量だけ**を受け取る。
//
// 規則（line_breaker.hpp の Item の説明）:
//   * 行の幅は advance − overhang_before − overhang_after で測る
//   * 行頭に来たアイテムの overhang_before と、行末に来たアイテムの overhang_after は落とす
//   * 効いた量は Spacing に負の値で返る
namespace shashoku::linebreak {
namespace {

constexpr float kEm = test::kEm;  // 全角 1 文字 = 16px

// 添字 at のアイテムに掛けの量を与える。
std::vector<Item> with_overhang(std::string_view utf8, std::size_t at, float before, float after) {
  std::vector<Item> items = test::items_of(utf8);
  items[at].overhang_before = before;
  items[at].overhang_after = after;
  return items;
}

// ----------------------------------------------------- 幅（行の送り）

TEST(LineBreakOverhang, NarrowsTheLine) {
  // 「あ[掛け 4/4]う」: 真ん中のアイテムが前後に 4px ずつ掛かるので、行は 8px 縮む。
  const std::vector<Item> items = with_overhang("あいう", 1, 4.0F, 4.0F);
  const Breaks breaks = LineBreaker().break_lines(items, 1000.0F);
  ASSERT_EQ(breaks.lines.size(), 1U);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, (3 * kEm) - 8.0F);
  EXPECT_EQ(breaks.spacing[1], (Spacing{-4.0F, -4.0F}));
  EXPECT_EQ(breaks.spacing[0], Spacing{});
  EXPECT_EQ(breaks.spacing[2], Spacing{});
}

TEST(LineBreakOverhang, DroppedAtTheLineStartAndEnd) {
  // 行頭のアイテムは前に掛けない（版面の外に出さない）。
  const std::vector<Item> head = with_overhang("あいう", 0, 4.0F, 4.0F);
  const Breaks head_breaks = LineBreaker().break_lines(head, 1000.0F);
  EXPECT_FLOAT_EQ(head_breaks.lines[0].width, (3 * kEm) - 4.0F);  // 後ろだけ効く
  EXPECT_EQ(head_breaks.spacing[0], (Spacing{0.0F, -4.0F}));

  // 行末のアイテムは後ろに掛けない。
  const std::vector<Item> tail = with_overhang("あいう", 2, 4.0F, 4.0F);
  const Breaks tail_breaks = LineBreaker().break_lines(tail, 1000.0F);
  EXPECT_FLOAT_EQ(tail_breaks.lines[0].width, (3 * kEm) - 4.0F);  // 前だけ効く
  EXPECT_EQ(tail_breaks.spacing[2], (Spacing{-4.0F, 0.0F}));

  // 1 アイテムだけの行では両方落ちる。
  const std::vector<Item> only = with_overhang("あ", 0, 4.0F, 4.0F);
  const Breaks only_breaks = LineBreaker().break_lines(only, 1000.0F);
  EXPECT_FLOAT_EQ(only_breaks.lines[0].width, kEm);
  EXPECT_EQ(only_breaks.spacing[0], Spacing{});
}

TEST(LineBreakOverhang, DroppedWhenTheItemMovesToTheLineEdge) {
  // 同じアイテムでも、行の中では効き、行頭・行末に来たら落ちる。
  // 幅 32 に「あいうえ」を流すと 2 文字ずつ 2 行になる。
  const std::vector<Item> items = with_overhang("あいうえ", 2, 4.0F, 4.0F);
  const Breaks breaks = LineBreaker().break_lines(items, 2 * kEm);
  ASSERT_EQ(breaks.lines.size(), 2U);
  // 2 行目の先頭に来たので前の掛けは落ち、後ろだけ効く
  EXPECT_EQ(breaks.spacing[2], (Spacing{0.0F, -4.0F}));
  EXPECT_FLOAT_EQ(breaks.lines[1].width, (2 * kEm) - 4.0F);
}

TEST(LineBreakOverhang, TrailingSpaceDoesNotKeepTheOverhang) {
  // 行末の空白は content_end の外。その手前のアイテムは「行末」なので後ろに掛けない。
  std::vector<Item> items = test::items_of("あい ");
  items[1].overhang_after = 4.0F;
  items[1].overhang_before = 4.0F;
  const Breaks breaks = LineBreaker().break_lines(items, 1000.0F);
  ASSERT_EQ(breaks.lines.size(), 1U);
  EXPECT_EQ(breaks.spacing[1], (Spacing{-4.0F, 0.0F}));
}

// ----------------------------------------------------- 改行位置

TEST(LineBreakOverhang, ChangesWhereTheLineBreaks) {
  // 幅ぴったりの行: 掛けが無ければ 4 文字目が次の行に落ちるが、掛けで 8px 縮むと収まる。
  const std::vector<Item> plain = test::items_of("あいうえ");
  const float width = (4 * kEm) - 8.0F;
  EXPECT_EQ(test::line_texts(plain, LineBreaker().break_lines(plain, width)),
            (std::vector<std::string>{"あいう", "え"}));

  const std::vector<Item> hung = with_overhang("あいうえ", 1, 4.0F, 4.0F);
  EXPECT_EQ(test::line_texts(hung, LineBreaker().break_lines(hung, width)),
            (std::vector<std::string>{"あいうえ"}));
}

// ----------------------------------------------------- 契約の丸め

TEST(LineBreakOverhang, ClampedToTheAdvance) {
  // 送りより大きい掛けは送りに丸める（行の幅が負にならない = アイテムを足すと幅が増える、
  // という前提を壊さない）。
  const std::vector<Item> items = with_overhang("あいう", 1, 100.0F, 100.0F);
  const Breaks breaks = LineBreaker().break_lines(items, 1000.0F);
  ASSERT_EQ(breaks.lines.size(), 1U);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, 2 * kEm);  // 真ん中のアイテムが幅 0 になる
  EXPECT_EQ(breaks.spacing[1], (Spacing{-kEm, 0.0F}));
}

TEST(LineBreakOverhang, NegativeOverhangIsIgnored) {
  // 負の値（= 幅を広げる向き）は受け取らない。契約は「非負」
  const std::vector<Item> items = with_overhang("あいう", 1, -8.0F, -8.0F);
  const Breaks breaks = LineBreaker().break_lines(items, 1000.0F);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, 3 * kEm);
  EXPECT_EQ(breaks.spacing[1], Spacing{});
}

// ----------------------------------------------------- 固有寸法

TEST(LineBreakOverhang, MinContentWidthSeesTheOverhangInsideTheRun) {
  // 分割不能な区間の中では掛けが効く。区間の端では落ちる（行と同じ規則）。
  // 欧文の語 "abc" は空白でしか割れないので 1 区間（ASCII は 0.5em = 8px × 3 = 24px）。
  std::vector<Item> items = test::items_of("abc");
  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(items), 3 * 0.5F * kEm);
  // 真ん中の文字に前後 4px ずつ掛けると、区間の中なのでどちらも効く
  items[1].overhang_before = 4.0F;
  items[1].overhang_after = 4.0F;
  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(items), (3 * 0.5F * kEm) - 8.0F);
  // 区間の端（先頭の文字の前）では落ちる
  std::vector<Item> at_edge = test::items_of("abc");
  at_edge[0].overhang_before = 4.0F;
  at_edge[0].overhang_after = 4.0F;
  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(at_edge), (3 * 0.5F * kEm) - 4.0F);
}

TEST(LineBreakOverhang, MaxContentWidthSeesTheOverhang) {
  // max-content（kUnbounded で流したときの行の幅）にも同じ規則で効く。
  const std::vector<Item> items = with_overhang("あいう", 1, 4.0F, 4.0F);
  const Breaks breaks = LineBreaker().break_lines(items, kUnbounded);
  ASSERT_EQ(breaks.lines.size(), 1U);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, (3 * kEm) - 8.0F);
}

// ----------------------------------------------------- ほかの規則との組み合わせ

TEST(LineBreakOverhang, DoesNotChangeBreakOpportunities) {
  // 掛けは幅だけの話で、分割可能位置は動かさない。
  const std::vector<Item> plain = test::items_of("あ、い");
  const std::vector<Item> hung = with_overhang("あ、い", 1, 4.0F, 4.0F);
  EXPECT_EQ(LineBreaker().break_opportunities(plain), LineBreaker().break_opportunities(hung));
}

TEST(LineBreakOverhang, CombinesWithPunctuationCollapsing) {
  // 約物のアキ詰め（JLREQ 3.1.4）と足し合わさる。「」」の後ろの二分アキを詰めたうえで、
  // 掛けのぶんも引く。
  std::vector<Item> items = test::items_of("あ」「い");
  items[2].overhang_before = 2.0F;  // 「 に前から 2px 掛ける
  const Breaks breaks = LineBreaker().break_lines(items, 1000.0F);
  ASSERT_EQ(breaks.lines.size(), 1U);
  EXPECT_EQ(breaks.spacing[1], (Spacing{0.0F, -0.5F * kEm}));
  EXPECT_EQ(breaks.spacing[2], (Spacing{-2.0F, 0.0F}));
  EXPECT_FLOAT_EQ(breaks.lines[0].width, (4 * kEm) - (0.5F * kEm) - 2.0F);
}

TEST(LineBreakOverhang, SurvivesOikomiAndBurasage) {
  // 追い込み（アキを詰める）とぶら下げ（行末の句読点を外に出す）を混ぜても、掛けは効いたまま。
  for (const OverflowPolicy policy : {OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
    SCOPED_TRACE(static_cast<int>(policy));
    const std::vector<Item> items = with_overhang("あい、うえ", 1, 4.0F, 4.0F);
    const Breaks breaks = LineBreaker(test::with_overflow(policy)).break_lines(items, 1000.0F);
    ASSERT_EQ(breaks.lines.size(), 1U);
    EXPECT_EQ(breaks.spacing[1], (Spacing{-4.0F, -4.0F}));
  }
}

}  // namespace
}  // namespace shashoku::linebreak
