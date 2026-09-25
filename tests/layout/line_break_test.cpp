#include <string>
#include <string_view>
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

// ---- 行末の全角スペース（U+3000）のぶら下げ（A59） --------------------------------
// 出典: CSS Text 3 §4.1.3 Phase II の 4「行末に残った空白・その他の space separator
// （Unicode の Zs から U+0020 と U+00A0 を除いたもの。U+3000 はここに入る）は、
// white-space が normal / nowrap ならぶら下がる（= 行の幅に数えない）」。
// 行の途中では今までどおり全角 1 字分の送りとして効く。

// 全角スペース U+3000（UTF-8）。ソースでは半角スペースと見分けが付かないので、
// 文字を直接書かず、必ずこの定数から組み立てる。
constexpr std::string_view kIdeographicSpace = "\xE3\x80\x80";

std::string with_space(std::string_view before, std::string_view after, int count = 1) {
  std::string out(before);
  for (int i = 0; i < count; ++i) {
    out += kIdeographicSpace;
  }
  out += after;
  return out;
}

// 行末に来た全角スペースは幅に数えない。数えると「あいうえお」と全角スペースが
// 離せない（LB21 × BA）ぶん、1 文字手前の「お」まで次の行へ送られてしまう
// （再測定 2026-09-25 の case10「立ちつくし」の「し」が孤立した原因）。
TEST(LayoutLineBreak, IdeographicSpaceAtLineEndDoesNotCountTowardTheWidth) {
  FakeMeasurer measurer;
  // 幅 84px = 全角 5.25 文字。「あいうえお」（80px）＋ 行末の全角スペースで収まる
  const Flowed flowed = flow(measurer, with_space("あいうえお", "かきくけこ"), 84,
                             linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえお", "かきくけこ"}));
  // ぶら下げた全角スペースは描かない（行末の半角スペースと同じ扱い）
  EXPECT_EQ(flowed.first_line_glyphs, (std::vector<float>{0, 16, 32, 48, 64}));
  EXPECT_EQ(flowed.last_line_glyphs, (std::vector<float>{0, 16, 32, 48, 64}));
  EXPECT_FLOAT_EQ(flowed.block_size, 2 * fake_line_height(16));
}

// 連続していても同じ（CSS Text 3 は「行末に残った並び」全体をぶら下げる）。
TEST(LayoutLineBreak, ConsecutiveIdeographicSpacesAtLineEndAllHang) {
  FakeMeasurer measurer;
  const Flowed flowed = flow(measurer, with_space("あいうえお", "かきくけこ", 2), 84,
                             linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえお", "かきくけこ"}));
  EXPECT_EQ(flowed.first_line_glyphs, (std::vector<float>{0, 16, 32, 48, 64}));
}

// 行の途中の全角スペースは今までどおり全角 1 字分を占める。
TEST(LayoutLineBreak, IdeographicSpaceInsideALineKeepsItsAdvance) {
  FakeMeasurer measurer;
  // 幅 200px に 1 行で収まる: あ(0) 全角スペース(16) い(32)
  const Flowed wide =
      flow(measurer, with_space("あ", "い"), 200, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(wide.lines, (std::vector<std::string>{with_space("あ", "い")}));
  EXPECT_EQ(wide.first_line_glyphs, (std::vector<float>{0, 16, 32}));

  // 幅 84px = 全角 5.25 文字。途中の全角スペースは 1 文字ぶんの席を取るので「お」が溢れる
  const Flowed narrow =
      flow(measurer, with_space("あ", "いうえお"), 84, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(narrow.lines, (std::vector<std::string>{with_space("あ", "いうえ"), "お"}));
}

// 行頭に来た全角スペースは残る（CSS Text 3 §4.1.3 Phase II の 1 が消すのは
// **畳み込みの対象になる**空白だけで、U+3000 はその対象ではない）。
TEST(LayoutLineBreak, IdeographicSpaceAtLineStartKeepsItsAdvance) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), br(), text(with_space("", "いうえお"))})});
  const auto tree = run_layout(root, 84, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  // 行頭の全角スペースはグリフも送りもそのまま（い は 16px から）
  EXPECT_EQ(glyph_positions(*lines[1]), (std::vector<float>{0, 16, 32, 48, 64}));
}

// 幅を内容から決める箱（flex アイテムの max-content）でも、末尾の全角スペースは数えない
// （= 行末の半角スペースと同じ扱い。そこは行末なので必ずぶら下がる）。
TEST(LayoutLineBreak, IdeographicSpaceAtTheEndDoesNotWidenAShrinkToFitBox) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({text(with_space("あ", ""))}), block({text("お")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  // #root / flex コンテナ / アイテム 2 つ（前順）
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 4U);
  EXPECT_FLOAT_EQ(rects[2].rect.inline_size, 16);  // 「あ」だけの幅
  EXPECT_FLOAT_EQ(rects[3].rect.inline_start, 16);
}

// 縦書きでも同じ（行の長さは高さ 84px）。
TEST(LayoutLineBreak, IdeographicSpaceHangsInVerticalWritingToo) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text(with_space("あいうえお", "かきくけこ"))})});
  const auto tree = run_layout(root, vertical_options(400, 84), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あいうえお", "かきくけこ"}));
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 16, 32, 48, 64}));
}

// 副作用を仕様として固定する（A59）: 全角スペースを行分割器に SP として渡すので、
// UAX #14 の空白越しの規則も効くようになる。LB14「OP SP* ×」で、始め括弧 + 全角スペースの
// 後ろでは割らない = 始め括弧が行末に残らない（JLREQ の行末禁則に合う）。
// 以前は「あいうえ（」で行が終わっていた。
TEST(LayoutLineBreak, OpeningBracketBeforeAnIdeographicSpaceDoesNotEndALine) {
  FakeMeasurer measurer;
  const Flowed flowed =
      flow(measurer, with_space("あいうえ（", "かきく"), 100, linebreak::OverflowPolicy::Oidashi);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえ", with_space("（", "かきく")}));
}

// 約物のぶら下げ（Burasage）と二重にならない: 行末の全角スペースは先に落ちるので、
// ぶら下げの判定はその手前の句読点に対して行われる（line_breaker.cpp の try_hang() は
// strip_trailing() を通してから最後の文字を見る）。
TEST(LayoutLineBreak, HangingIdeographicSpaceDoesNotDisturbBurasage) {
  FakeMeasurer measurer;
  const Flowed flowed = flow(measurer, with_space("あいうえお。", "かきくけこ"), 84,
                             linebreak::OverflowPolicy::Burasage);
  EXPECT_EQ(flowed.lines, (std::vector<std::string>{"あいうえお。", "かきくけこ"}));
  // 「。」は content の端（84px）の外にぶら下がる。全角スペースはそれより後ろで、描かれない
  EXPECT_EQ(flowed.first_line_glyphs, (std::vector<float>{0, 16, 32, 48, 64, 80}));
}

// 両端揃え: ぶら下げた全角スペースは行の幅にも配分の箇所にも入らない
// （既存のぶら下げ約物と同じ扱い）。
TEST(LayoutLineBreak, JustifyIgnoresTheHangingIdeographicSpace) {
  FakeMeasurer measurer;
  const auto root =
      build({block({text(with_space("あいうえお", "かきくけこさ"))},
                   [](ComputedStyle& style) { style.text_align = style::TextAlign::Justify; })});
  const auto tree = run_layout(root, 84, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あいうえお", "かきくけこ", "さ"}));
  // 行の幅は 80px（ぶら下げた全角スペースを含まない）。余り 4px を 4 か所に 1px ずつ配る。
  // 全角スペースの位置は content_end の外なので、配る箇所には数えない
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 3U);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 17, 34, 51, 68}));
}

}  // namespace
}  // namespace shashoku::layout::test
