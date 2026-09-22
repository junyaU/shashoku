#include "layout/east_asian_width.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"

// East Asian Width の表（src/layout/east_asian_width_table.inc）が Unicode 18.0.0 の W / F と
// 一致していること（A20）と、その判定が空白の畳み込み（A14: ソース中の改行の前後がどちらも
// 全角なら改行を消す。CSS Text 3 §4.1.3 segment break transformation）に正しく効くことの検査。
//
// 表は scripts/gen_unicode_tables.py が UCD から生成する。以前は生成の最後に
// LEGACY_WIDE_DEVIATIONS 32 件を当てて Unicode 15.1 相当に戻していた（issue #24）。
// ここのケースはその 32 区間の代表なので、据え置きが復活すると落ちる。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

std::string code_point_text(char32_t cp) {
  return std::format("U+{:04X}", static_cast<std::uint32_t>(cp));
}

// ---- (1) 表そのもの ---------------------------------------------------------------

// 出典: UAX #11 EastAsianWidth.txt 18.0.0（W / F を全角とする）。
// 分類は issue #24 と同じ:
//   (a) 16.0〜18.0 で N -> W になった / 新しく追加された（25 件）
//   (b) 15.1 の時点で W なのに手起こしの表が写し漏れていた（3 件）
//   (c) 手起こしの表が未割り当ての穴をまたいで範囲をつなぎ、余分に全角にしていた（4 件）
struct WidthCase {
  char32_t lo;
  char32_t hi;
  bool wide;
  std::string_view note;
};

constexpr auto kDeviationCases = std::to_array<WidthCase>({
    // (a)
    {0x2630, 0x2637, true, "八卦。16.0 で N -> W"},
    {0x268A, 0x268F, true, "太玄経の単/重記号。16.0 で N -> W"},
    {0x31E4, 0x31E5, true, "CJK の筆画。16.0 で N -> W"},
    {0x4DC0, 0x4DFF, true, "易経の六十四卦。16.0 で N -> W"},
    {0x16FF2, 0x16FF6, true, "表意文字記号。17.0 で追加"},
    {0x187F8, 0x187FF, true, "西夏文字。17.0 で追加"},
    {0x18CD6, 0x18CDA, true, "契丹小字。18.0 で追加"},
    {0x18CFF, 0x18CFF, true, "契丹小字。16.0 で追加"},
    {0x18D09, 0x18D20, true, "西夏文字補助。17.0 で追加"},
    {0x18D80, 0x18DF2, true, "西夏文字部品補助。17.0 で追加"},
    {0x18E00, 0x19191, true, "女真文字。18.0 で追加"},
    {0x191A0, 0x191D2, true, "女真文字部首。18.0 で追加"},
    {0x1B168, 0x1B168, true, "小書きカタカナ ヰ・ヱ・古いエ。18.0 で追加"},
    {0x1D300, 0x1D356, true, "太玄経。16.0 で N -> W"},
    {0x1D360, 0x1D376, true, "算木。16.0 で N -> W"},
    {0x1F1AE, 0x1F1AE, true, "六曜「友引」。18.0 で追加"},
    {0x1F6D8, 0x1F6D9, true, "絵文字。17.0 で追加"},
    {0x1F7DA, 0x1F7DA, true, "幾何学記号。18.0 で追加"},
    {0x1FA8A, 0x1FA8E, true, "絵文字。17.0 で追加"},
    {0x1FAC8, 0x1FAC8, true, "絵文字。17.0 で追加"},
    {0x1FACC, 0x1FACD, true, "絵文字。18.0 で追加"},
    {0x1FADD, 0x1FADD, true, "絵文字。18.0 で追加"},
    {0x1FAEA, 0x1FAEB, true, "絵文字。17.0 で追加"},
    {0x1FAEF, 0x1FAEF, true, "絵文字。17.0 で追加"},
    {0x1FAF9, 0x1FAFA, true, "絵文字。18.0 で追加"},
    // (b)
    {0x2FFC, 0x2FFF, true, "漢字構成記述文字。15.1 の時点で W"},
    {0x31EF, 0x31EF, true, "漢字構成記述文字。15.1 の時点で W"},
    {0x1B155, 0x1B155, true, "小書きカタカナ「コ」。15.1 の時点で W（和文の文字）"},
    // (c)
    {0x1AFF4, 0x1AFF4, false, "かな拡張 B の未割り当て（N）"},
    {0x1AFFC, 0x1AFFC, false, "かな拡張 B の未割り当て（N）"},
    {0x1B129, 0x1B131, false, "かな補助の未割り当て（N）"},
    {0x1B133, 0x1B14F, false, "かな補助の未割り当て（N）"},
});

// 据え置きの影響を受けなかった対照。(c) の隣（実在する文字）が全角のままであることを含む。
constexpr auto kControlCases = std::to_array<WidthCase>({
    {0x0041, 0x005A, false, "ラテン大文字（Na）"},
    {0x0020, 0x0020, false, "空白（Na）"},
    {0x3042, 0x3042, true, "あ（W）"},
    {0x4E00, 0x4E00, true, "一（W）"},
    {0xFF01, 0xFF01, true, "！（F）"},
    {0x1AFF3, 0x1AFF3, true, "カタカナ閩南語声調（W）。(c) U+1AFF4 の手前"},
    {0x1AFF5, 0x1AFF5, true, "カタカナ閩南語声調（W）。(c) U+1AFF4 の直後"},
    {0x1B132, 0x1B132, true, "ひらがな小書き「こ」（W）。(c) U+1B133.. の手前"},
    {0x1B150, 0x1B150, true, "ひらがな小書きゐ（W）。(c) ..U+1B14F の直後"},
    {0x1B164, 0x1B164, true, "カタカナ小書きヰ（W）"},
});

void check_cases(std::span<const WidthCase> cases) {
  for (const WidthCase& item : cases) {
    EXPECT_EQ(is_fullwidth(item.lo), item.wide) << code_point_text(item.lo) << " " << item.note;
    EXPECT_EQ(is_fullwidth(item.hi), item.wide) << code_point_text(item.hi) << " " << item.note;
  }
}

TEST(LayoutEastAsianWidth, TableMatchesUnicode18) {
  check_cases(kDeviationCases);
  check_cases(kControlCases);
}

// ---- (2) 空白の畳み込みへの効き方（A14）---------------------------------------------

// 20px の段落。全角は 1em、半角（空白を含む）は 0.5em（FakeMeasurer の約束）。
constexpr float kFontSize = 20;

void set_font_size(ComputedStyle& style) { style.font_size = kFontSize; }

std::vector<std::string> flow(FakeMeasurer& measurer, std::vector<Tree> children) {
  const auto root = build({block(std::move(children), set_font_size)});
  const auto tree = run_layout(root, 400, measurer);
  EXPECT_TRUE(tree.has_value());
  if (!tree) {
    return {};
  }
  return line_texts(*tree);
}

struct CollapseCase {
  std::string_view input;
  std::string_view expected;
  std::string_view note;
};

// 分類ごとの代表。改行の前後がどちらも全角なら改行は消え、片方でも全角でなければ空白 1 個になる。
constexpr auto kCollapseCases = std::to_array<CollapseCase>({
    // (b) U+1B155 小書きカタカナ「コ」。和文の本文に出る
    {"あ\n\U0001B155い", "あ\U0001B155い", "(b) U+1B155"},
    // (b) U+2FFC 漢字構成記述文字
    {"あ\n⿼い", "あ⿼い", "(b) U+2FFC"},
    // (a) U+4DC0 易経の六十四卦。16.0 で N -> W になったので改行が消える
    {"あ\n䷀い", "あ䷀い", "(a) U+4DC0"},
    // (a) U+1B168 小書きカタカナ ヰ
    {"あ\n\U0001B168い", "あ\U0001B168い", "(a) U+1B168"},
    // (a) U+1F1AE 六曜「友引」
    {"あ\n\U0001F1AEい", "あ\U0001F1AEい", "(a) U+1F1AE"},
    // (c) U+1B133 未割り当て。全角扱いをやめたので空白が残る
    {"あ\n\U0001B133い", "あ \U0001B133い", "(c) U+1B133"},
    // (c) U+1AFF4 未割り当て
    {"あ\n\U0001AFF4い", "あ \U0001AFF4い", "(c) U+1AFF4"},
    // 対照: どちらの表でも全角 / 半角
    {"あ\nい", "あい", "対照（かな）"},
    {"A\nB", "A B", "対照（ラテン）"},
});

TEST(LayoutEastAsianWidth, SourceLineBreakFollowsUnicode18) {
  for (const CollapseCase& item : kCollapseCases) {
    FakeMeasurer measurer;
    EXPECT_EQ(flow(measurer, {text(item.input)}),
              (std::vector<std::string>{std::string(item.expected)}))
        << item.note;
  }
}

// ノード境界をまたいでも、片側だけが全角でも、判定は同じ材料で行う。
TEST(LayoutEastAsianWidth, SourceLineBreakAcrossNodeBoundaries) {
  FakeMeasurer measurer;
  // <span>あ\n</span>𛅕 -> 改行は消える
  EXPECT_EQ(flow(measurer, {inline_box({text("あ\n")}), text("\U0001B155")}),
            (std::vector<std::string>{"あ\U0001B155"}));
  // 𛅕\n<span>い</span> も同じ
  EXPECT_EQ(flow(measurer, {text("\U0001B155\n"), inline_box({text("い")})}),
            (std::vector<std::string>{"\U0001B155い"}));
  // 片側だけが全角なら空白 1 個（A14）
  EXPECT_EQ(flow(measurer, {text("A\n\U0001B155")}), (std::vector<std::string>{"A \U0001B155"}));
  EXPECT_EQ(flow(measurer, {text("\U0001B155\nA")}), (std::vector<std::string>{"\U0001B155 A"}));
  // ノード境界をまたぐ片側だけの全角も同じ
  EXPECT_EQ(flow(measurer, {inline_box({text("A\n")}), text("\U0001B155")}),
            (std::vector<std::string>{"A \U0001B155"}));
}

// ---- (3) 断片の寸法（横書き・縦書き）------------------------------------------------

// issue #24 の再現そのもの: <p style="font-size:20px">あ\n𛅕い</p>。
// 改行が消えて 1 断片・inline_size 60（20px × 3 文字）になる。
TEST(LayoutEastAsianWidth, SmallKatakanaKoKeepsOneFragmentInHorizontal) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ\n\U0001B155い")}, set_font_size)});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_EQ(fragments[0]->text, "あ\U0001B155い");
  EXPECT_FLOAT_EQ(fragments[0]->inline_size, 3 * kFontSize);
}

// 縦書きでは、消えなかった空白が sideways の断片として独立し、1 本の断片が 3 本に割れていた。
TEST(LayoutEastAsianWidth, SmallKatakanaKoKeepsOneFragmentInVertical) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あ\n\U0001B155い")}, set_font_size)});
  const auto tree = run_layout(root, vertical_options(400, 300), measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_EQ(fragments[0]->text, "あ\U0001B155い");
  EXPECT_FALSE(fragments[0]->sideways);
  EXPECT_FLOAT_EQ(fragments[0]->inline_size, 3 * kFontSize);
}

}  // namespace
}  // namespace shashoku::layout::test
