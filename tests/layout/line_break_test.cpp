#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"
#include "linebreak/line_breaker.hpp"

// DESIGN.md Phase 4 の受け入れ条件をレイアウト越しに確認する。
// 行分割器そのもののテーブル駆動テストは tests/linebreak/ にある。ここで見るのは
// 「行分割器の判断がボックスツリーの座標に正しく載るか」。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

struct Flowed {
  std::vector<std::string> lines;
  float block_size = 0;
  std::vector<float> first_line_glyphs;
  std::vector<float> last_line_glyphs;
};

Flowed flow(FakeMeasurer& measurer, const std::string& content, float width,
            linebreak::OverflowPolicy policy) {
  const auto root = build({block({text(content)})});
  Options opts = make_options(width);
  opts.line_break.overflow = policy;
  const auto tree = run_layout(root, opts, measurer);
  EXPECT_TRUE(tree.has_value());
  if (!tree) {
    return {};
  }
  Flowed out;
  out.lines = line_texts(*tree);
  out.block_size = tree->root.rect.block_size;
  const std::vector<const LineBox*> lines = all_lines(*tree);
  if (!lines.empty()) {
    out.first_line_glyphs = glyph_positions(*lines.front());
    out.last_line_glyphs = glyph_positions(*lines.back());
  }
  return out;
}

// 「行頭に句読点が絶対に出ない」（DESIGN.md Phase 4）。
TEST(LayoutLineBreak, PunctuationNeverStartsALine) {
  FakeMeasurer measurer;
  // 幅 80px（全角 5 文字）。素朴に詰めると 6 文字目の「。」が行頭に来てしまう
  const Flowed flowed =
      flow(measurer, "あいうえお。かきくけこ", 80, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえ", "お。かきく", "けこ"}));
}

TEST(LayoutLineBreak, ClosingBracketAndSmallKanaNeverStartALine) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, "あいうえお」", 80, linebreak::OverflowPolicy::Oidashi).lines,
            (std::vector<std::string>{"あいうえ", "お」"}));
  EXPECT_EQ(flow(measurer, "あいうえおぁ", 80, linebreak::OverflowPolicy::Oidashi).lines,
            (std::vector<std::string>{"あいうえ", "おぁ"}));
}

// 追い出し: 禁則に掛かる文字は手前の文字を道連れにして次の行へ。
TEST(LayoutLineBreak, OidashiPushesTheLastCharacterToTheNextLine) {
  FakeMeasurer measurer;
  const Flowed flowed = flow(measurer, "あいうえお。", 84, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえ", "お。"}));
  EXPECT_FLOAT_EQ(flowed.block_size, 2 * fake_line_height(16));
}

// ぶら下げ: 行末の句読点 1 文字を行の外に出す。行数が減り、親の高さが変わる（DESIGN.md §6-2）。
TEST(LayoutLineBreak, BurasageHangsThePeriodAndChangesTheHeight) {
  FakeMeasurer measurer;
  const Flowed flowed = flow(measurer, "あいうえお。", 84, linebreak::OverflowPolicy::Burasage);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえお。"}));
  EXPECT_FLOAT_EQ(flowed.block_size, fake_line_height(16));
  // ぶら下げた「。」は content の端（84px）の外に出る
  ASSERT_EQ(flowed.first_line_glyphs.size(), 6U);
  EXPECT_FLOAT_EQ(flowed.first_line_glyphs.back(), 80);
  EXPECT_GT(flowed.first_line_glyphs.back() + 16, 84);
}

// 追い込み: 行内の約物のアキを詰めて次の分割可能位置まで引き込む。
TEST(LayoutLineBreak, OikomiTightensPunctuationToPullInTheNextCharacter) {
  FakeMeasurer measurer;
  const Flowed oidashi = flow(measurer, "あ、いうえおか", 104, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(oidashi.lines, (std::vector<std::string>{"あ、いうえお", "か"}));

  const Flowed oikomi = flow(measurer, "あ、いうえおか", 104, linebreak::OverflowPolicy::Oikomi);
  EXPECT_EQ(oikomi.lines, (std::vector<std::string>{"あ、いうえおか"}));
  EXPECT_FLOAT_EQ(oikomi.block_size, fake_line_height(16));
  // 「、」の後ろの半角アキ（8px）を詰めたぶん、以降のグリフが左に寄る
  EXPECT_EQ(oikomi.first_line_glyphs, (std::vector<float>{0, 16, 24, 40, 56, 72, 88}));
}

// A4「禁則 > 幅」: 収まらなくても禁則は破らず、行がはみ出す。
TEST(LayoutLineBreak, ProhibitionWinsOverWidth) {
  FakeMeasurer measurer;
  const Flowed flowed = flow(measurer, "あ。", 16, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あ。"}));
  // クリップしない（A4）。行ボックスの矩形は content 幅のまま、グリフははみ出す
  EXPECT_EQ(flowed.first_line_glyphs, (std::vector<float>{0, 16}));
}

// ぶら下げの対象は句読点だけ。閉じ括弧は追い出しになる。
TEST(LayoutLineBreak, BurasageDoesNotHangBrackets) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, "あいうえお」", 84, linebreak::OverflowPolicy::Burasage).lines,
            (std::vector<std::string>{"あいうえ", "お」"}));
}

// 禁則で行数が変わると、親ブロックの高さもそれに追随する。
TEST(LayoutLineBreak, ParentHeightFollowsTheLineCount) {
  FakeMeasurer measurer;
  const auto root = build(
      {block({text("あいうえお。")}, [](ComputedStyle& style) { style.padding = {4, 0, 4, 0}; })});
  Options opts = make_options(84);
  opts.line_break.overflow = linebreak::OverflowPolicy::Burasage;
  const auto hanging = run_layout(root, opts, measurer);
  ASSERT_TRUE(hanging.has_value());
  opts.line_break.overflow = linebreak::OverflowPolicy::Oidashi;
  const auto pushed = run_layout(root, opts, measurer);
  ASSERT_TRUE(pushed.has_value());
  EXPECT_FLOAT_EQ(hanging->root.rect.block_size, fake_line_height(16) + 8);
  EXPECT_FLOAT_EQ(pushed->root.rect.block_size, 2 * fake_line_height(16) + 8);
}

}  // namespace
}  // namespace shashoku::layout::test
