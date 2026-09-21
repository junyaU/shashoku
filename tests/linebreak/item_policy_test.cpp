#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// アイテムごとの行分割ポリシー（Item::strictness / Item::wrap）。
// CSS の line-break / overflow-wrap はどちらもテキスト（インラインボックス）に適用される
// 継承プロパティなので、span で上書きできる。その上書きを段の境界で落とさないための口
// （issue #2 / ARCHITECTURE.md A23・§3.4 (7)）。
//
// 境界の規則（A23）:
//   * 分割クラスの解決はアイテム自身の strictness で行う。ペア表・文脈規則は解決済みの
//     クラスに対して従来どおり働く。例外は「loose: ID の直後のハイフンの前で割ってよい」で、
//     これだけは 2 アイテムにまたがるので行頭に来る側（後ろのアイテム）で決める
//   * overflow-wrap の緊急分割は、位置の両側のアイテムがともに Normal 以外のときだけ。
//     min_content_width() の区間を切るのは、両側がともに Anywhere のときだけ（A-new）
// 出典: CSS Text Level 3 「Line Breaking Details」（要素の境界にまたがる分割位置で
// どの要素の line-break / overflow-wrap が効くかは "undefined in this level"）。
namespace shashoku::linebreak {
namespace {

constexpr float kEm = test::kEm;
constexpr float kAscii = 0.5F * kEm;  // test_support の既定: ASCII 1 文字 = 0.5em

using test::mark_opportunities;

// [begin, end) のアイテムにポリシーを付ける（その範囲を囲む span に相当）。
void set_strictness(std::vector<Item>& items, std::size_t begin, std::size_t end,
                    Strictness strictness) {
  for (std::size_t i = begin; i < end; ++i) {
    items[i].strictness = strictness;
  }
}

void set_wrap(std::vector<Item>& items, std::size_t begin, std::size_t end, Wrap wrap) {
  for (std::size_t i = begin; i < end; ++i) {
    items[i].wrap = wrap;
  }
}

std::vector<std::string> lines_of(std::span<const Item> items, float width,
                                  const Config& config = {}) {
  return test::line_texts(items, LineBreaker(config).break_lines(items, width));
}

// ---------------------------------------------------- 既定値（nullopt）は従来どおり

TEST(LineBreakItemPolicy, ExplicitPolicyEqualToConfigChangesNothing) {
  // すべてのアイテムに Config と同じ値を明示しても、出力はビット単位で同じであること。
  // （nullopt のままなら従来と同じ、を裏返した形で担保する。）
  const char* text = "シャツと人々の値段は￥1,200です……ABCDEFGH「あ」。";
  for (const Strictness strictness : {Strictness::Strict, Strictness::Normal, Strictness::Loose}) {
    for (const Wrap wrap : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
      for (const OverflowPolicy overflow :
           {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
        Config config;
        config.strictness = strictness;
        config.wrap = wrap;
        config.overflow = overflow;
        SCOPED_TRACE("strictness " + std::to_string(static_cast<int>(strictness)) + " wrap " +
                     std::to_string(static_cast<int>(wrap)) + " overflow " +
                     std::to_string(static_cast<int>(overflow)));

        const std::vector<Item> plain = test::items_of(text);
        std::vector<Item> marked = plain;
        set_strictness(marked, 0, marked.size(), strictness);
        set_wrap(marked, 0, marked.size(), wrap);

        const LineBreaker breaker(config);
        for (const float width : {2.0F * kEm, 5.0F * kEm, 40.0F * kEm}) {
          EXPECT_TRUE(breaker.break_lines(plain, width) == breaker.break_lines(marked, width))
              << "width " << width;
        }
        EXPECT_EQ(breaker.break_opportunities(plain), breaker.break_opportunities(marked));
        EXPECT_FLOAT_EQ(breaker.min_content_width(plain), breaker.min_content_width(marked));
      }
    }
  }
}

// ------------------------------------------------------------------ strictness

TEST(LineBreakItemPolicy, StrictnessIsResolvedPerItem) {
  // CSS Text 3 の line-break: normal は小書き仮名（UAX #14 の CJ）を ID として扱う。
  // その解決は「その文字自身」の指定で決まる（A23）。
  std::vector<Item> items = test::items_of("シャツ");
  EXPECT_EQ(mark_opportunities(items), "シャ|ツ");  // Config 既定の strict

  items[1].strictness = Strictness::Normal;  // <span style="line-break:normal">ャ</span>
  EXPECT_EQ(mark_opportunities(items), "シ|ャ|ツ");

  items[1].strictness = Strictness::Loose;
  EXPECT_EQ(mark_opportunities(items), "シ|ャ|ツ");

  items[1].strictness = Strictness::Strict;  // 明示の strict は既定と同じ
  EXPECT_EQ(mark_opportunities(items), "シャ|ツ");
}

TEST(LineBreakItemPolicy, StrictnessOfNeighborsDoesNotMatter) {
  // 前後のアイテムだけを normal にしても、「ャ」のクラスは strict のまま（= NS）。
  // 境界の曖昧さを作らないための規則（A23）。
  std::vector<Item> items = test::items_of("シャツ");
  items[0].strictness = Strictness::Normal;
  items[2].strictness = Strictness::Normal;
  EXPECT_EQ(mark_opportunities(items), "シャ|ツ");
}

TEST(LineBreakItemPolicy, LooseIterationMarkIsResolvedPerItem) {
  // CSS Text 3 line-break: loose「breaks before iteration marks」。々 U+3005。
  std::vector<Item> items = test::items_of("人々の");
  EXPECT_EQ(mark_opportunities(items), "人々|の");

  items[1].strictness = Strictness::Loose;
  EXPECT_EQ(mark_opportunities(items), "人|々|の");

  items[1].strictness.reset();
  items[0].strictness = Strictness::Loose;
  items[2].strictness = Strictness::Loose;
  EXPECT_EQ(mark_opportunities(items), "人々|の");  // 々 自身は strict のまま
}

TEST(LineBreakItemPolicy, NestedOverrides) {
  // 入れ子（div: strict → span: loose → span: strict）。
  // 人々(strict) 人々(loose) 人々(strict) の 3 組。
  std::vector<Item> items = test::items_of("人々人々人々");
  set_strictness(items, 2, 4, Strictness::Loose);
  set_strictness(items, 4, 6, Strictness::Strict);  // 内側の span で strict に戻す
  EXPECT_EQ(mark_opportunities(items), "人々|人|々|人々");

  // 一番外側（Config）が loose でも、内側の strict の指定が勝つ。
  const Config loose = test::with_strictness(Strictness::Loose);
  std::vector<Item> outer_loose = test::items_of("人々人々人々");
  set_strictness(outer_loose, 2, 4, Strictness::Strict);
  EXPECT_EQ(mark_opportunities(outer_loose, loose), "人|々|人々|人|々");
}

TEST(LineBreakItemPolicy, LooseHyphenUsesTheFollowingItem) {
  // 「loose では ID の直後のハイフン（‐ – ）の前で割ってよい」（CSS Text 3 line-break: loose）は
  // 2 アイテムにまたがる唯一の loose 規則。A23 の取り決めにより、行頭に来る側
  // （= 後ろのアイテム、ここでは ‐）の strictness で決める。
  std::vector<Item> items = test::items_of("あ‐い");
  EXPECT_EQ(mark_opportunities(items), "あ‐|い");

  items[1].strictness = Strictness::Loose;  // ハイフン自身が loose
  EXPECT_EQ(mark_opportunities(items), "あ|‐|い");

  items[1].strictness.reset();
  items[0].strictness = Strictness::Loose;  // 前のアイテムだけ loose では効かない
  EXPECT_EQ(mark_opportunities(items), "あ‐|い");
}

TEST(LineBreakItemPolicy, StrictnessChangesLineBreaking) {
  // 幅 1.5 文字ぶん。strict では「人々」が割れず全行あふれるが、loose の span の中だけは割れる。
  std::vector<Item> items = test::items_of("人々人々人々");
  EXPECT_EQ(lines_of(items, 1.5F * kEm), (std::vector<std::string>{"人々", "人々", "人々"}));

  set_strictness(items, 2, 4, Strictness::Loose);
  EXPECT_EQ(lines_of(items, 1.5F * kEm), (std::vector<std::string>{"人々", "人", "々", "人々"}));
}

TEST(LineBreakItemPolicy, StrictnessChangesMinContentWidth) {
  // min-content は分割不能な最長区間。strict では "シャ" = 2em、
  // 「ャ」だけ normal にすると 1em まで縮む。
  const LineBreaker breaker;
  std::vector<Item> items = test::items_of("シャツ");
  EXPECT_FLOAT_EQ(breaker.min_content_width(items), 2.0F * kEm);

  items[1].strictness = Strictness::Normal;
  EXPECT_FLOAT_EQ(breaker.min_content_width(items), kEm);
}

// ------------------------------------------------------------------ overflow-wrap

TEST(LineBreakItemPolicy, BreakAnywhereOnEveryItem) {
  // issue #2 の表 2 行目（span に overflow-wrap: anywhere）に相当。幅は 3 文字ぶん。
  // 全アイテムが anywhere なら、Config::wrap = Anywhere と同じ 3 行になる。
  std::vector<Item> items = test::items_of("ABCDEFGH");
  EXPECT_EQ(lines_of(items, 3.0F * kAscii), (std::vector<std::string>{"ABCDEFGH"}));  // 指定なし

  set_wrap(items, 0, items.size(), Wrap::Anywhere);
  EXPECT_EQ(lines_of(items, 3.0F * kAscii), (std::vector<std::string>{"ABC", "DEF", "GH"}));

  Config config;
  config.wrap = Wrap::Anywhere;
  EXPECT_EQ(lines_of(test::items_of("ABCDEFGH"), 3.0F * kAscii, config),
            (std::vector<std::string>{"ABC", "DEF", "GH"}));
}

TEST(LineBreakItemPolicy, BreakAnywhereOnlyInsideTheRun) {
  // 中央の "CDEF" だけ anywhere。割れるのは C|D、D|E、E|F だけで、
  // 要素の境界（B|C と F|G）では割らない（A23）。
  std::vector<Item> items = test::items_of("ABCDEFGH");
  set_wrap(items, 2, 6, Wrap::Anywhere);

  // 幅 3 文字: "ABC" までは 1 行に入るが、B|C で割れないので C|D まで伸ばす。
  // 次の行は E|F で割れるが F|G では割れないので "DE" で終わり、残りは "FGH"。
  const Breaks breaks = LineBreaker().break_lines(items, 3.0F * kAscii);
  EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"ABC", "DE", "FGH"}));
  for (const Line& line : breaks.lines) {
    EXPECT_FALSE(line.overflows);
  }
}

TEST(LineBreakItemPolicy, BreakAnywhereNeverBreaksAtTheRunBoundary) {
  // 幅 1 文字ぶん。anywhere の範囲の内部だけが 1 文字ずつになり、
  // 範囲の外（"ABC" と "FGH"）は割れずにあふれる（= 境界では割らない）。
  std::vector<Item> items = test::items_of("ABCDEFGH");
  set_wrap(items, 2, 6, Wrap::Anywhere);
  const Breaks breaks = LineBreaker().break_lines(items, 1.0F * kAscii);
  EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"ABC", "D", "E", "FGH"}));
  EXPECT_TRUE(breaks.lines[0].overflows);
  EXPECT_FALSE(breaks.lines[1].overflows);
  EXPECT_FALSE(breaks.lines[2].overflows);
  EXPECT_TRUE(breaks.lines[3].overflows);
}

TEST(LineBreakItemPolicy, BreakAnywhereItemOverridesConfigTrue) {
  // Config は true（親 div が overflow-wrap: anywhere）で、中央の span だけ normal に戻す。
  // 両側がともに true の位置でしか割れないので、A|B と G|H でしか割れない。
  Config config;
  config.wrap = Wrap::Anywhere;
  std::vector<Item> items = test::items_of("ABCDEFGH");
  set_wrap(items, 2, 6, Wrap::Normal);

  const Breaks breaks = LineBreaker(config).break_lines(items, 3.0F * kAscii);
  EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"A", "BCDEFG", "H"}));
  EXPECT_TRUE(breaks.lines[1].overflows);  // 割れる位置がないのであふれる（A4 禁則 > 幅）
}

TEST(LineBreakItemPolicy, BreakAnywhereStillOnlyWhenNothingFits) {
  // §3.4 (5): 収まる分割可能位置があるときは発動しない。アイテム指定でも同じ。
  std::vector<Item> items = test::items_of("あいうえお。かき");
  set_wrap(items, 0, items.size(), Wrap::Anywhere);
  EXPECT_EQ(lines_of(items, 5.0F * kEm), (std::vector<std::string>{"あいうえ", "お。かき"}));
}

TEST(LineBreakItemPolicy, BreakAnywhereKeepsProhibitionOrder) {
  // 発動時の優先順（分離禁則 > 行頭・行末禁則）はアイテム指定でも維持する。
  // "ABC、D" を 1.5 文字ぶんの幅で: "ABC" は収まるが次の行が「、」で始まるので "AB" にする。
  std::vector<Item> items = test::items_of("ABC、D");
  set_wrap(items, 0, items.size(), Wrap::Anywhere);
  EXPECT_EQ(lines_of(items, 1.5F * kEm), (std::vector<std::string>{"AB", "C、", "D"}));

  // 分離禁則（…… / LB22 × IN）も守る。
  std::vector<Item> pair = test::items_of("は……本");
  set_wrap(pair, 0, pair.size(), Wrap::Anywhere);
  EXPECT_EQ(lines_of(pair, 2.0F * kEm), (std::vector<std::string>{"は", "……", "本"}));
}

TEST(LineBreakItemPolicy, BreakAnywhereDoesNotChangeOpportunities) {
  // 緊急分割は「分割可能位置」ではない（収まらない行でだけ発動する緊急手段）。
  // break_opportunities() の結果は Config でもアイテム指定でも変わらない。
  const std::vector<Item> plain = test::items_of("ABCDEFGH");
  std::vector<Item> anywhere = plain;
  set_wrap(anywhere, 2, 6, Wrap::Anywhere);
  Config config;
  config.wrap = Wrap::Anywhere;

  const LineBreaker breaker;
  EXPECT_EQ(breaker.break_opportunities(plain), breaker.break_opportunities(anywhere));
  EXPECT_EQ(breaker.break_opportunities(plain), LineBreaker(config).break_opportunities(plain));
  EXPECT_EQ(mark_opportunities(anywhere), "ABCDEFGH");
}

TEST(LineBreakItemPolicy, BreakWordDoesNotChangeMinContentWidth) {
  // CSS Text 3 §5.4: break-word がもたらす分割位置は min-content では考えない。
  // Config でもアイテムごとの指定でも同じ（A23 / A-new）。
  const LineBreaker breaker;
  const std::vector<Item> plain = test::items_of("ABCDEFGH");
  std::vector<Item> break_word = plain;
  set_wrap(break_word, 0, break_word.size(), Wrap::BreakWord);
  Config config;
  config.wrap = Wrap::BreakWord;

  const float expected = 4.0F * kEm;  // "ABCDEFGH" は分割不能な 1 区間（= 8 文字ぶん）
  EXPECT_FLOAT_EQ(breaker.min_content_width(plain), expected);
  EXPECT_FLOAT_EQ(breaker.min_content_width(break_word), expected);
  EXPECT_FLOAT_EQ(LineBreaker(config).min_content_width(plain), expected);
}

TEST(LineBreakItemPolicy, AnywhereChangesMinContentWidth) {
  // CSS Text 3 §5.4: anywhere の分割位置は min-content でも考える（issue #18 / A-new）。
  // アイテムごとの指定では、両側がともに anywhere の位置でだけ切る（A23 の境界の規則）。
  const std::vector<Item> plain = test::items_of("ABCDEFGH");
  std::vector<Item> anywhere = plain;
  set_wrap(anywhere, 0, anywhere.size(), Wrap::Anywhere);
  Config config;
  config.wrap = Wrap::Anywhere;

  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(anywhere), kAscii);  // 1 クラスタぶん
  EXPECT_FLOAT_EQ(LineBreaker(config).min_content_width(plain), kAscii);

  // 真ん中の "CDEF" だけ anywhere。切れるのは C|D、D|E、E|F だけなので "ABC" が最長区間。
  std::vector<Item> span = plain;
  set_wrap(span, 2, 6, Wrap::Anywhere);
  EXPECT_FLOAT_EQ(LineBreaker().min_content_width(span), 3.0F * kAscii);
}

}  // namespace
}  // namespace shashoku::linebreak
