#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// min_content_width()（分割不能な最長区間の幅）と overflow-wrap の関係。
//
// 出典: CSS Text Level 3 §5.4 *Overflow Wrapping: the overflow-wrap property*
//   anywhere : "An otherwise unbreakable sequence of characters may be broken at an arbitrary
//              point if there are no otherwise-acceptable break points in the line. …
//              grapheme clusters must stay together as one unit."
//              この分割位置は **min-content intrinsic size の計算でも考える**
//   break-word: "As for anywhere except that soft wrap opportunities introduced by break-word
//              are **not** considered when calculating min-content intrinsic sizes."
//
// この区別が無いと、flex アイテムの自動最小サイズ（CSS Flexbox Level 1 §4.5）が
// 「1 行ぶんの幅」のままになり、子が親からはみ出す（issue #18 / ARCHITECTURE.md A35）。
//
// 禁則との関係: 区間の切れ目は緊急分割と同じ「クラスタ境界」で判定する（禁則は見ない）。
// 緊急分割には「守れる位置が 1 つもなければ破る」という最後の逃げ場があるので、
// ここで出した幅は必ず達成できる。それを表の全ケースで検査する（NeverOverflows）。
namespace shashoku::linebreak {
namespace {

constexpr float kEm = test::kEm;
constexpr float kAscii = 0.5F * kEm;  // test_support の既定: ASCII 1 文字 = 0.5em

Config config_of(Wrap wrap) {
  Config config;
  config.wrap = wrap;
  return config;
}

// [begin, end) を囲む <span style="overflow-wrap: …"> に相当。
void set_policy(std::vector<Item>& items, std::size_t begin, std::size_t end, Wrap wrap) {
  for (std::size_t i = begin; i < end; ++i) {
    items[i].wrap = wrap;
  }
}

std::string_view name_of(Wrap wrap) {
  switch (wrap) {
    case Wrap::Normal:
      return "normal";
    case Wrap::BreakWord:
      return "break-word";
    case Wrap::Anywhere:
      return "anywhere";
  }
  return "?";
}

// ---- 段落全体に 1 つの overflow-wrap を指定した表 -------------------------------------

struct Case {
  std::string_view text;
  Wrap policy;
  float min_content;
  std::string_view why;
};

constexpr std::array<Case, 14> kCases{{
    // 分割可能位置が 1 つもない欧文（LB28 AL × AL）。
    {"ABCDEFGH", Wrap::Normal, 8 * kAscii, "割れないので 8 文字ぶん"},
    {"ABCDEFGH", Wrap::BreakWord, 8 * kAscii, "§5.4: break-word は min-content に効かない"},
    {"ABCDEFGH", Wrap::Anywhere, kAscii, "§5.4: anywhere はクラスタ境界で切る = 1 文字"},

    // もともと 1 文字ずつ割れる和文は、どの値でも変わらない。
    {"あいうえお", Wrap::Normal, kEm, "和文は 1 文字ずつ割れる"},
    {"あいうえお", Wrap::BreakWord, kEm, "同上"},
    {"あいうえお", Wrap::Anywhere, kEm, "同上"},

    // 分離禁則（…… / LB22 × IN）。anywhere は min-content では守らない
    // （緊急分割の最後の逃げ場があるので、この幅でも必ず組める）。
    {"は……本", Wrap::Normal, 3 * kEm, "「は……」を割らない（LB22 × IN）"},
    {"は……本", Wrap::BreakWord, 3 * kEm, "break-word は min-content を変えない"},
    {"は……本", Wrap::Anywhere, kEm, "anywhere はクラスタ境界で切る"},

    // 行頭禁則（LB13 × CL）。「、」は行頭に出せないので "ABC、" で 1 区間。
    {"ABC、D", Wrap::Normal, 3 * kAscii + kEm, "「、」の前で割れない"},
    {"ABC、D", Wrap::BreakWord, 3 * kAscii + kEm, "break-word は min-content を変えない"},
    {"ABC、D", Wrap::Anywhere, kEm, "いちばん広いクラスタ =「、」"},

    // クラスタの内部（結合文字）では割らない。"A" + U+0301（test_support では 1em）。
    {"A\u0301BC", Wrap::Anywhere, kAscii + kEm, "結合文字は基底文字と 1 クラスタ"},
    {"A\u0301BC", Wrap::Normal, 3 * kAscii + kEm, "割れないので全体 4 アイテム分"},
}};

TEST(LineBreakMinContent, OverflowWrapTable) {
  for (const Case& test_case : kCases) {
    SCOPED_TRACE(std::string(test_case.text) + " / " + std::string(name_of(test_case.policy)) +
                 " / " + std::string(test_case.why));
    const std::vector<Item> items = test::items_of(test_case.text);
    // Config で指定した場合と、全アイテムに指定した場合（span で囲んだ場合）は同じ。
    std::vector<Item> marked = items;
    set_policy(marked, 0, marked.size(), test_case.policy);
    EXPECT_FLOAT_EQ(LineBreaker(config_of(test_case.policy)).min_content_width(items),
                    test_case.min_content);
    EXPECT_FLOAT_EQ(LineBreaker().min_content_width(marked), test_case.min_content);
  }
}

// min_content_width() の不変条件: この幅なら必ず収まる（どの行も overflows にならない）。
TEST(LineBreakMinContent, TableWidthsAreAchievable) {
  for (const Case& test_case : kCases) {
    SCOPED_TRACE(std::string(test_case.text) + " / " + std::string(name_of(test_case.policy)));
    const std::vector<Item> items = test::items_of(test_case.text);
    const LineBreaker breaker(config_of(test_case.policy));
    for (const Line& line : breaker.break_lines(items, test_case.min_content).lines) {
      EXPECT_FALSE(line.overflows);
      EXPECT_LE(line.width, test_case.min_content + 1.0F / 512.0F);
    }
  }
}

// ---- アイテムごとの指定（span。A23 の境界の規則）--------------------------------------

TEST(LineBreakMinContent, AnywhereOnlyInsideTheSpan) {
  // AB<span style="overflow-wrap:anywhere">CDEFGH</span>。
  // 両側がともに anywhere の位置（C|D、D|E、…）でだけ切れるので、min-content は "ABC"。
  std::vector<Item> items = test::items_of("ABCDEFGH");
  set_policy(items, 2, items.size(), Wrap::Anywhere);
  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(items), 3 * kAscii);
}

TEST(LineBreakMinContent, BreakWordNextToAnywhereDoesNotCut) {
  // 片側が break-word の位置は「弱い方」= break-word なので min-content では切らない。
  // AB(break-word) CD(anywhere) EF(break-word): 切れるのは C|D だけ。
  std::vector<Item> items = test::items_of("ABCDEF");
  set_policy(items, 0, 2, Wrap::BreakWord);
  set_policy(items, 2, 4, Wrap::Anywhere);
  set_policy(items, 4, 6, Wrap::BreakWord);
  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(items), 3 * kAscii);
}

TEST(LineBreakMinContent, ItemPolicyOverridesConfig) {
  // 親が anywhere でも、span で normal に戻した範囲は切れない。
  std::vector<Item> items = test::items_of("ABCDEFGH");
  set_policy(items, 2, 6, Wrap::Normal);
  // 切れるのは A|B と G|H だけ。最長区間は "BCDEFG"。
  EXPECT_FLOAT_EQ(LineBreaker(config_of(Wrap::Anywhere)).min_content_width(items), 6 * kAscii);
}

// ---- max-content は変わらない -------------------------------------------------------

TEST(LineBreakMinContent, MaxContentIsNotAffected) {
  // max-content（kUnbounded）では緊急分割そのものが起きないので、どの値でも同じ 1 行。
  const std::vector<Item> plain = test::items_of("ABCDEFGH");
  const Breaks expected = LineBreaker().break_lines(plain, kUnbounded);
  ASSERT_EQ(expected.lines.size(), 1U);
  EXPECT_FLOAT_EQ(expected.lines[0].width, 8 * kAscii);

  for (const Wrap policy : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
    SCOPED_TRACE(name_of(policy));
    std::vector<Item> marked = plain;
    set_policy(marked, 0, marked.size(), policy);
    EXPECT_TRUE(LineBreaker(config_of(policy)).break_lines(plain, kUnbounded) == expected);
    EXPECT_TRUE(LineBreaker().break_lines(marked, kUnbounded) == expected);
  }
}

// ---- 実配置（break_lines）は 2 値で変わらない ------------------------------------------

TEST(LineBreakMinContent, BreakWordAndAnywhereBreakLinesTheSame) {
  // 変えたのは min_content_width() だけで、緊急分割の条件と位置選びは同じ（A35）。
  const std::vector<Item> plain = test::items_of("ABCDEFGH");
  for (const float width : {kAscii, 3.0F * kAscii, 8.0F * kAscii}) {
    SCOPED_TRACE("width " + std::to_string(width));
    const Breaks break_word = LineBreaker(config_of(Wrap::BreakWord)).break_lines(plain, width);
    const Breaks anywhere = LineBreaker(config_of(Wrap::Anywhere)).break_lines(plain, width);
    EXPECT_TRUE(break_word == anywhere);
  }
  // 前提: 幅 3 文字ぶんでは実際に割れている（両方とも緊急分割が働く）。
  EXPECT_EQ(test::line_texts(
                plain, LineBreaker(config_of(Wrap::BreakWord)).break_lines(plain, 3.0F * kAscii)),
            (std::vector<std::string>{"ABC", "DEF", "GH"}));
}

// ---- 分割可能位置は変わらない --------------------------------------------------------

TEST(LineBreakMinContent, OpportunitiesAreNotAffected) {
  // 緊急分割も min-content の区間の切れ目も「分割可能位置」ではない（§3.4 (7)）。
  const std::vector<Item> plain = test::items_of("ABCDEFGH");
  const std::vector<bool> expected = LineBreaker().break_opportunities(plain);
  for (const Wrap policy : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
    SCOPED_TRACE(name_of(policy));
    std::vector<Item> marked = plain;
    set_policy(marked, 0, marked.size(), policy);
    EXPECT_EQ(LineBreaker(config_of(policy)).break_opportunities(plain), expected);
    EXPECT_EQ(LineBreaker().break_opportunities(marked), expected);
  }
}

}  // namespace
}  // namespace shashoku::linebreak
