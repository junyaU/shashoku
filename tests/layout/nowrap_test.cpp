#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"
#include "style/computed_style.hpp"

// `white-space: nowrap`（CSS Text Level 3 §5.1。ARCHITECTURE.md A58）。
//
// 仕様（§3.8 の (c) と A58）:
//   * 2 つのクラスタの間のソフトな分割機会は、その 2 つの**最も近い共通の祖先**の
//     `white-space` が nowrap なら無い。実装は「最も外側の nowrap 祖先」の識別子で近似し、
//     両方が同じ非 0 の識別子なら `linebreak::Item::no_break_before` を立てる
//   * 空白の畳み込みは normal と同じ（nowrap は「畳む + 折らない」）
//   * `<br>`（強制改行）は nowrap の中でも効く
//   * min-content は nowrap の並び全体の幅になる（= `flex: 1 1 0` の項目はそれより縮まない。A52）
//   * 縦書きでも同じ（論理軸）
//
// 偽の TextMeasurer（全角 1em / 半角 0.5em）なので、font-size 16px の全角 1 文字は 16px。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

// nowrap を付けるラムダ。
StyleFn nowrap() {
  return [](ComputedStyle& style) { style.white_space = style::WhiteSpace::Nowrap; };
}

std::vector<std::string> flow(FakeMeasurer& measurer, std::vector<Tree> children,
                              float viewport_width, StyleFn style = nullptr) {
  const auto root = build({block(std::move(children), std::move(style))}, nullptr);
  const auto tree = run_layout(root, viewport_width, measurer);
  EXPECT_TRUE(tree.has_value());
  if (!tree) {
    return {};
  }
  return line_texts(*tree);
}

// 縦書き（論理軸は同じ。行の進む向きが上から下になるだけ）。
std::vector<std::string> flow_vertical(FakeMeasurer& measurer, std::vector<Tree> children,
                                       float viewport_height, StyleFn style = nullptr) {
  const auto root = build_vertical({block(std::move(children), std::move(style))}, nullptr);
  const auto tree = run_layout(root, vertical_options(200, viewport_height), measurer);
  EXPECT_TRUE(tree.has_value());
  if (!tree) {
    return {};
  }
  return line_texts(*tree);
}

// ---- 分割機会の抑制 ---------------------------------------------------------------

// span に書いた nowrap は、その span の中だけで分割機会を消す。外側の文は今までどおり折れる。
TEST(LayoutNowrap, SpanKeepsItsOwnTextOnOneLine) {
  FakeMeasurer measurer;
  // 幅 48px = 全角 3 文字。「うえ」の間では割らない
  const std::vector<Tree> children = {text("あい"), inline_box({text("うえ")}, nowrap()),
                                      text("おか")};
  EXPECT_EQ(flow(measurer, {text("あい"), inline_box({text("うえ")}), text("おか")}, 48),
            (std::vector<std::string>{"あいう", "えおか"}));
  EXPECT_EQ(flow(measurer, children, 48), (std::vector<std::string>{"あい", "うえお", "か"}));
}

// ブロックに書けば段落全体が 1 行（幅を超えてもそのまま伸びる。はみ出しは別の診断）。
TEST(LayoutNowrap, BlockLevelNowrapKeepsTheWholeParagraphOnOneLine) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("あいうえおかきく")}, 48, nowrap()),
            (std::vector<std::string>{"あいうえおかきく"}));
  // 指定がなければ今までどおり折れる（対照）
  EXPECT_EQ(flow(measurer, {text("あいうえおかきく")}, 48).size(), 3U);
}

// 継承する（CSS Text 3）。親ブロックの nowrap は中の span のテキストにも効く。
TEST(LayoutNowrap, IsInherited) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("あい"), inline_box({text("うえ")}), text("おか")}, 48, nowrap()),
            (std::vector<std::string>{"あいうえおか"}));
}

// **簡略化（A58）**: nowrap の中で `white-space: normal` に戻しても分割は再開しない。
// 「最も外側の nowrap 祖先」だけを見るので、内側の normal は無視される。
// CSS Text 3 §5.1 の厳密な規則（最も近い共通の祖先）とはここだけ違う。
TEST(LayoutNowrap, InnerNormalDoesNotResumeBreakingBySimplification) {
  FakeMeasurer measurer;
  const StyleFn normal = [](ComputedStyle& style) {
    style.white_space = style::WhiteSpace::Normal;
  };
  const std::vector<Tree> children = {
      inline_box({text("あい"), inline_box({text("うえおか")}, normal)}, nowrap())};
  EXPECT_EQ(flow(measurer, children, 48), (std::vector<std::string>{"あいうえおか"}));
}

// 別々の nowrap の並びの**間**では割れる（共通の祖先は nowrap ではない）。
TEST(LayoutNowrap, TwoSiblingNowrapRunsCanBreakBetweenThem) {
  FakeMeasurer measurer;
  const std::vector<Tree> children = {inline_box({text("あいう")}, nowrap()),
                                      inline_box({text("えおか")}, nowrap())};
  EXPECT_EQ(flow(measurer, children, 48), (std::vector<std::string>{"あいう", "えおか"}));
}

// 入れ子の nowrap は外側の並びに畳まれる（識別子は最も外側のもの）。
TEST(LayoutNowrap, NestedNowrapJoinsTheOutermostRun) {
  FakeMeasurer measurer;
  const std::vector<Tree> children = {
      inline_box({text("あい"), inline_box({text("うえ")}, nowrap()), text("おか")}, nowrap())};
  EXPECT_EQ(flow(measurer, children, 48), (std::vector<std::string>{"あいうえおか"}));
}

// ---- 空白と強制改行 ---------------------------------------------------------------

// 空白の畳み込みは normal と同じ（nowrap は「畳む + 折らない」）。
TEST(LayoutNowrap, CollapsesWhiteSpaceLikeNormal) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("A  \t B")}, 400, nowrap()), (std::vector<std::string>{"A B"}));
  EXPECT_EQ(flow(measurer, {text("   A")}, 400, nowrap()), (std::vector<std::string>{"A"}));
  // 空白は分割機会を作らない（畳んだ空白が行末で落ちるのも normal と同じ扱い）
  EXPECT_EQ(flow(measurer, {text("あ い う")}, 48, nowrap()),
            (std::vector<std::string>{"あ い う"}));
}

// `<br>` は nowrap の中でも効く（LB4 は非適合化できない。行分割器の契約）。
TEST(LayoutNowrap, ForcedBreakStillWorks) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("あい"), br(), text("うえ")}, 48, nowrap()),
            (std::vector<std::string>{"あい", "うえ"}));
  // span に書いた場合も同じ
  EXPECT_EQ(flow(measurer, {inline_box({text("あい"), br(), text("うえ")}, nowrap())}, 48),
            (std::vector<std::string>{"あい", "うえ"}));
}

// 緊急分割（overflow-wrap）も nowrap の中では起きない（ブラウザと同じ優先順）。
// 行分割器は `no_break_before` を分離禁則と同じ強さで扱う（収まらない行では破る）ので、
// (c''') は並びの中のアイテムの `wrap` も Normal に落としている（A58）。
TEST(LayoutNowrap, EmergencyBreakIsSuppressedInsideNowrap) {
  FakeMeasurer measurer;
  const StyleFn both = [](ComputedStyle& style) {
    style.white_space = style::WhiteSpace::Nowrap;
    style.overflow_wrap = style::OverflowWrap::Anywhere;
  };
  // anywhere だけなら 1 文字ずつでも割る（対照）
  EXPECT_GT(flow(measurer, {text("ABCDEFGH")}, 16,
                 [](ComputedStyle& style) { style.overflow_wrap = style::OverflowWrap::Anywhere; })
                .size(),
            1U);
  EXPECT_EQ(flow(measurer, {text("ABCDEFGH")}, 16, both), (std::vector<std::string>{"ABCDEFGH"}));
  // min-content も並び全体（= 1 行ぶん）になる。`anywhere` のクラスタ境界は数えない
  const auto root = build({flex({block({text("ABCDEFGH")}, both)}, [](ComputedStyle& style) {
    style.width = style::Dimension::px(8);
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const BlockBox* item = find_block(*tree, "div", 1);
  ASSERT_NE(item, nullptr);
  EXPECT_FLOAT_EQ(item->rect.inline_size, 8 * 8.0F);  // 半角 8 文字 x 8px
}

// ---- 固有寸法（min-content）------------------------------------------------------

// nowrap の並びの min-content は並び全体の幅。`flex: 1 1 0` の項目はそれより縮まない
// （CSS Flexbox 1 §4.5 の自動最小サイズ。A52）。
TEST(LayoutNowrap, MinContentIsTheWholeRunWidth) {
  FakeMeasurer measurer;
  const auto item_width = [&measurer](bool with_nowrap) {
    const auto root =
        build({flex({block({inline_box({text("あいう")}, with_nowrap ? nowrap() : nullptr)},
                           [](ComputedStyle& style) {
                             style.flex_grow = 1;
                             style.flex_shrink = 1;
                             style.flex_basis = style::Dimension::px(0);
                           })},
                    [](ComputedStyle& style) { style.width = style::Dimension::px(16); })});
    const auto tree = run_layout(root, 200, measurer);
    EXPECT_TRUE(tree.has_value());
    if (!tree) {
      return -1.0F;
    }
    const BlockBox* item = find_block(*tree, "div", 1);
    EXPECT_NE(item, nullptr);
    return item != nullptr ? item->rect.inline_size : -1.0F;
  };
  // 指定なし: 全角 1 文字ぶん（= 分割不能な最長区間）まで縮む
  EXPECT_FLOAT_EQ(item_width(false), 16.0F);
  // nowrap: 3 文字ぶんより縮まない（親 16px からはみ出す）
  EXPECT_FLOAT_EQ(item_width(true), 48.0F);
}

// ---- 縦書き ----------------------------------------------------------------------

// 論理軸で動くので、縦書きでもまったく同じ結果になる。
TEST(LayoutNowrap, WorksTheSameInVerticalWritingMode) {
  FakeMeasurer measurer;
  const std::vector<Tree> children = {text("あい"), inline_box({text("うえ")}, nowrap()),
                                      text("おか")};
  EXPECT_EQ(flow_vertical(measurer, {text("あい"), inline_box({text("うえ")}), text("おか")}, 48),
            (std::vector<std::string>{"あいう", "えおか"}));
  EXPECT_EQ(flow_vertical(measurer, children, 48),
            (std::vector<std::string>{"あい", "うえお", "か"}));
  EXPECT_EQ(flow_vertical(measurer, {text("あいうえおかきく")}, 48, nowrap()),
            (std::vector<std::string>{"あいうえおかきく"}));
}

}  // namespace
}  // namespace shashoku::layout::test
