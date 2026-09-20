#include "linebreak/break_class.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/punctuation.hpp"
#include "linebreak/test_support.hpp"

// 分割クラス表の検査（ARCHITECTURE.md §3.4 (1)）。
// 出典: UAX #14 §5「Line Breaking Properties」/ LineBreak.txt 18.0.0。
namespace shashoku::linebreak {
namespace {

struct ClassCase {
  char32_t cp;
  BreakClass expected;
  const char* note;
};

void expect_classes(const std::vector<ClassCase>& cases) {
  for (const ClassCase& c : cases) {
    SCOPED_TRACE(std::string(c.note) + " U+" + std::to_string(static_cast<unsigned>(c.cp)) + " (" +
                 test::to_utf8(c.cp) + ")");
    EXPECT_EQ(break_class_of(c.cp), c.expected);
  }
}

TEST(LineBreakClass, Japanese) {
  expect_classes({
      {U'あ', BreakClass::Id, "ひらがな"},
      {U'ン', BreakClass::Id, "カタカナ"},
      {U'漢', BreakClass::Id, "CJK 統合漢字"},
      {U'一', BreakClass::Id, "CJK 統合漢字"},
      {U'㐀', BreakClass::Id, "CJK 統合漢字拡張 A"},
      {U'\U00020000', BreakClass::Id, "CJK 統合漢字拡張 B"},
      {U'ぁ', BreakClass::Cj, "小書き仮名（Conditional Japanese Starter）"},
      {U'ゃ', BreakClass::Cj, "小書き仮名"},
      {U'っ', BreakClass::Cj, "小書き仮名"},
      {U'ー', BreakClass::Cj, "長音記号"},
      {U'々', BreakClass::Ns, "繰り返し記号"},
      {U'ゝ', BreakClass::Ns, "繰り返し記号"},
      {U'ヾ', BreakClass::Ns, "繰り返し記号"},
      {U'、', BreakClass::Cl, "読点（行頭禁則）"},
      {U'。', BreakClass::Cl, "句点（行頭禁則）"},
      {U'，', BreakClass::Cl, "全角コンマ"},
      {U'．', BreakClass::Cl, "全角ピリオド"},
      {U'「', BreakClass::Op, "始め括弧（行末禁則）"},
      {U'」', BreakClass::Cl, "終わり括弧（行頭禁則）"},
      {U'（', BreakClass::Op, "全角始め括弧"},
      {U'）', BreakClass::Cl, "全角終わり括弧"},
      {U'【', BreakClass::Op, "全角始め括弧"},
      {U'】', BreakClass::Cl, "全角終わり括弧"},
      {U'・', BreakClass::Ns, "中点"},
      {U'：', BreakClass::Ns, "全角コロン"},
      {U'！', BreakClass::Ex, "全角感嘆符（行頭禁則）"},
      {U'？', BreakClass::Ex, "全角疑問符（行頭禁則）"},
      {U'…', BreakClass::In, "三点リーダ（分離禁則）"},
      {U'‥', BreakClass::In, "二点リーダ（分離禁則）"},
      {U'—', BreakClass::B2, "EM ダッシュ（分離禁則）"},
      {U'　', BreakClass::Ba, "全角スペース"},
      {U'￥', BreakClass::Pr, "全角円記号（数値の接頭）"},
      {U'％', BreakClass::Po, "全角パーセント（数値の接尾）"},
  });
}

TEST(LineBreakClass, LatinAndSymbols) {
  expect_classes({
      {U'A', BreakClass::Al, "ラテン文字"},
      {U'z', BreakClass::Al, "ラテン文字"},
      {U'0', BreakClass::Nu, "数字"},
      {U'9', BreakClass::Nu, "数字"},
      {U' ', BreakClass::Sp, "空白"},
      {U'\t', BreakClass::Ba, "タブ"},
      {U'\n', BreakClass::Lf, "LF"},
      {U'\r', BreakClass::Cr, "CR"},
      {U'(', BreakClass::Op, "始め丸括弧"},
      {U')', BreakClass::Cp, "終わり丸括弧"},
      {U'!', BreakClass::Ex, "感嘆符"},
      {U',', BreakClass::Is, "コンマ"},
      {U'.', BreakClass::Is, "ピリオド"},
      {U':', BreakClass::Is, "コロン"},
      {U'/', BreakClass::Sy, "スラッシュ"},
      {U'-', BreakClass::Hy, "ハイフンマイナス"},
      {U'"', BreakClass::Qu, "引用符"},
      {U'$', BreakClass::Pr, "数値の接頭"},
      {U'%', BreakClass::Po, "数値の接尾"},
      {U'\u00A0', BreakClass::Gl, "NBSP"},
      {U'\u2060', BreakClass::Wj, "WORD JOINER"},
      {U'\u200B', BreakClass::Zw, "ZERO WIDTH SPACE"},
      {U'\u200D', BreakClass::Zwj, "ZERO WIDTH JOINER"},
      {U'\u0301', BreakClass::Cm, "結合文字（アキュート）"},
  });
}

TEST(LineBreakClass, EmojiAndUnknown) {
  expect_classes({
      {U'\U0001F600', BreakClass::Id, "絵文字（ID 扱い）"},
      {U'\U0001F44D', BreakClass::Eb, "絵文字ベース"},
      {U'\U0001F3FB', BreakClass::Em, "肌色修飾子"},
      {U'\U0001F1EF', BreakClass::Ri, "地域表示記号"},
      {U'가', BreakClass::Id, "ハングル音節（LB27 で ID）"},
      {U'֑', BreakClass::Cm, "ヘブライ語のアクセント"},
      {U'א', BreakClass::Al, "ヘブライ文字（HL は AL に寄せる）"},
      {U'\U000E0080', BreakClass::Al, "未割り当て（XX → AL）"},
  });
}

TEST(LineBreakClass, EastAsianBrackets) {
  // LB30 の $EastAsian 除外集合。全角の始め括弧は「前で割ってよい」側に回る。
  EXPECT_TRUE(is_east_asian_bracket(U'「'));
  EXPECT_TRUE(is_east_asian_bracket(U'（'));
  EXPECT_TRUE(is_east_asian_bracket(U'【'));
  EXPECT_FALSE(is_east_asian_bracket(U'('));
  EXPECT_FALSE(is_east_asian_bracket(U')'));
  EXPECT_FALSE(is_east_asian_bracket(U'あ'));
}

TEST(LineBreakClass, LooseSets) {
  // CSS Text 3 §5.3 が loose / normal で名指しする文字（break_class.hpp のコメント参照）。
  EXPECT_TRUE(is_iteration_mark(U'々'));
  EXPECT_TRUE(is_iteration_mark(U'ヾ'));
  EXPECT_FALSE(is_iteration_mark(U'あ'));
  EXPECT_TRUE(is_loose_centered_punctuation(U'・'));
  EXPECT_TRUE(is_loose_centered_punctuation(U'！'));
  EXPECT_TRUE(is_loose_centered_punctuation(U'？'));
  EXPECT_FALSE(is_loose_centered_punctuation(U'。'));
  EXPECT_TRUE(is_wide_numeric_affix(U'％'));
  EXPECT_TRUE(is_wide_numeric_affix(U'￥'));
  EXPECT_TRUE(is_wide_numeric_affix(U'℃'));
  EXPECT_FALSE(is_wide_numeric_affix(U'%'));
  EXPECT_TRUE(is_cjk_hyphen_like(U'〜'));
  EXPECT_TRUE(is_cjk_hyphen_like(U'゠'));
  EXPECT_TRUE(is_loose_hyphen(U'‐'));
  EXPECT_TRUE(is_loose_hyphen(U'–'));
  EXPECT_FALSE(is_loose_hyphen(U'-'));
}

TEST(LineBreakClass, PunctuationKinds) {
  // JLREQ 3.1.2〜3.1.5 の約物の分類（ARCHITECTURE.md §3.4 (4)）。
  EXPECT_EQ(punct_kind(U'「'), PunctKind::Open);
  EXPECT_EQ(punct_kind(U'〝'), PunctKind::Open);
  EXPECT_EQ(punct_kind(U'」'), PunctKind::Close);
  EXPECT_EQ(punct_kind(U'〟'), PunctKind::Close);
  EXPECT_EQ(punct_kind(U'、'), PunctKind::Comma);
  EXPECT_EQ(punct_kind(U'，'), PunctKind::Comma);
  EXPECT_EQ(punct_kind(U'。'), PunctKind::Period);
  EXPECT_EQ(punct_kind(U'．'), PunctKind::Period);
  EXPECT_EQ(punct_kind(U'・'), PunctKind::MiddleDot);
  EXPECT_EQ(punct_kind(U'：'), PunctKind::MiddleDot);
  EXPECT_EQ(punct_kind(U'あ'), PunctKind::None);
  EXPECT_EQ(punct_kind(U'.'), PunctKind::None);  // 半角には詰める空きがない

  EXPECT_TRUE(has_space_before(PunctKind::Open));
  EXPECT_TRUE(has_space_before(PunctKind::MiddleDot));
  EXPECT_FALSE(has_space_before(PunctKind::Close));
  EXPECT_TRUE(has_space_after(PunctKind::Close));
  EXPECT_TRUE(has_space_after(PunctKind::Period));
  EXPECT_FALSE(has_space_after(PunctKind::Open));

  EXPECT_TRUE(is_hanging_punctuation(U'、'));
  EXPECT_TRUE(is_hanging_punctuation(U'。'));
  EXPECT_TRUE(is_hanging_punctuation(U'，'));
  EXPECT_TRUE(is_hanging_punctuation(U'．'));
  EXPECT_FALSE(is_hanging_punctuation(U'」'));  // 終わり括弧はぶら下げない
}

}  // namespace
}  // namespace shashoku::linebreak
