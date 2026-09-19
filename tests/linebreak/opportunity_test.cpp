#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// 分割可能位置（幅を考えない純粋な UAX #14 + 禁則）のテーブル駆動テスト。
// 期待値は「割ってよい位置に | を入れた文字列」で書く。読めば禁則の仕様が分かる形。
// 出典は各ケースのコメント（UAX #14 の規則番号 / JLREQ の節 / CSS Text 3 の節）。
namespace shashoku::linebreak {
namespace {

using test::mark_opportunities;

struct OppCase {
  const char* input;
  const char* expected;
  const char* source;
};

void expect_opportunities(const std::vector<OppCase>& cases, const Config& config = {}) {
  for (const OppCase& c : cases) {
    SCOPED_TRACE(std::string(c.source) + " : " + c.input);
    EXPECT_EQ(mark_opportunities(c.input, config), c.expected);
  }
}

// ---------------------------------------------------------------- 行頭禁則

TEST(LineBreakOpportunity, LineStartProhibited) {
  expect_opportunities({
      {"あい。うえ", "あ|い。|う|え", "LB13 × CL / JLREQ 3.1.1 句点は行頭に置かない"},
      {"あい、うえ", "あ|い、|う|え", "LB13 × CL / JLREQ 3.1.1 読点"},
      {"あ」い", "あ」|い", "LB13 × CL / JLREQ 3.1.1 終わり括弧"},
      {"あ』い", "あ』|い", "LB13 × CL"},
      {"あ）い", "あ）|い", "LB13 × CL"},
      {"あ〕い", "あ〕|い", "LB13 × CL"},
      {"あ】い", "あ】|い", "LB13 × CL"},
      {"あ！い", "あ！|い", "LB13 × EX / JLREQ 3.1.1 感嘆符"},
      {"あ？い", "あ？|い", "LB13 × EX / JLREQ 3.1.1 疑問符"},
      {"あ・い", "あ・|い", "LB21 × NS / JLREQ 3.1.1 中点"},
      {"あ：い", "あ：|い", "LB21 × NS"},
      {"人々の", "人々|の", "LB21 × NS / JLREQ 3.1.1 繰り返し記号"},
      {"ゆゝし", "ゆゝ|し", "LB21 × NS 繰り返し記号"},
      {"あっち", "あっ|ち", "LB21 × NS（strict: CJ → NS）/ JLREQ 3.1.1 小書き仮名"},
      {"カード", "カー|ド", "LB21 × NS（strict: CJ → NS）/ JLREQ 3.1.1 長音記号"},
      {"シャツ", "シャ|ツ", "LB21 × NS（strict: CJ → NS）小書き仮名"},
      {"あ)い", "あ)|い", "LB13 × CP（半角の終わり括弧も行頭に置かない）"},
      {"あ,い", "あ,|い", "LB15d × IS"},
      {"あ/い", "あ/|い", "LB13 × SY"},
  });
}

TEST(LineBreakOpportunity, ChainedProhibitedCharacters) {
  // 禁則文字が連鎖しても、その先頭より前でしか割れない。
  expect_opportunities({
      {"あ。」い", "あ。」|い", "LB13 × CL の連鎖 / JLREQ 3.1.1"},
      {"あ」。い", "あ」。|い", "LB13 × CL の連鎖"},
      {"あ）」』い", "あ）」』|い", "LB13 × CL の連鎖"},
      {"あ！？い", "あ！？|い", "LB13 × EX の連鎖"},
      {"あっ！」い", "あっ！」|い", "LB21 × NS と LB13 × EX × CL の連鎖"},
  });
}

// ---------------------------------------------------------------- 行末禁則

TEST(LineBreakOpportunity, LineEndProhibited) {
  expect_opportunities({
      {"あ「い」う", "あ|「い」|う", "LB14 OP SP* × / JLREQ 3.1.1 始め括弧は行末に置かない"},
      {"あ『い』う", "あ|『い』|う", "LB14 OP SP* ×"},
      {"あ（い）う", "あ|（い）|う", "LB14 OP SP* ×"},
      {"あ〔い〕う", "あ|〔い〕|う", "LB14 OP SP* ×"},
      {"あ［い］う", "あ|［い］|う", "LB14 OP SP* ×"},
      {"あ｛い｝う", "あ|｛い｝|う", "LB14 OP SP* ×"},
      {"あ〈い〉う", "あ|〈い〉|う", "LB14 OP SP* ×"},
      {"あ《い》う", "あ|《い》|う", "LB14 OP SP* ×"},
      {"あ【い】う", "あ|【い】|う", "LB14 OP SP* ×"},
      {"あ〖い〗う", "あ|〖い〗|う", "LB14 OP SP* ×"},
      {"あ〘い〙う", "あ|〘い〙|う", "LB14 OP SP* ×"},
      {"あ〝い〟う", "あ|〝い〟|う", "LB14 OP SP* ×"},
      {"あ「「い", "あ|「「い", "LB14 始め括弧の連鎖"},
      {"a ( b", "a |( b", "LB14 OP SP* ×: 空白を挟んでも始め括弧の後ろでは割らない"},
  });
}

// ---------------------------------------------------------------- 分離禁則

TEST(LineBreakOpportunity, Inseparable) {
  expect_opportunities({
      {"あ……い", "あ……|い", "LB22 × IN / JLREQ 3.1.1 分離禁則（三点リーダ）"},
      {"あ‥‥い", "あ‥‥|い", "LB22 × IN / JLREQ 3.1.1 分離禁則（二点リーダ）"},
      {"あ——い", "あ|——|い", "LB17 B2 SP* × B2 / JLREQ 3.1.1 分離禁則（ダッシュ）"},
      {"あ———い", "あ|———|い", "LB17 B2 SP* × B2"},
  });
}

TEST(LineBreakOpportunity, NumbersAndUnits) {
  expect_opportunities({
      {"1,234", "1,234", "LB25 NU (SY|IS)* × NU / JLREQ 3.1.1 数値の分離禁則"},
      {"12.5", "12.5", "LB25 NU (SY|IS)* × NU"},
      {"100%", "100%", "LB25 NU (SY|IS)* × PO（単位）"},
      {"100％", "100％", "LB25 NU (SY|IS)* × PO（全角の単位）"},
      {"20℃", "20℃", "LB25 NU (SY|IS)* × PO（単位）"},
      {"$100", "$100", "LB25 PR × NU（通貨記号）"},
      {"￥1,000", "￥1,000", "LB25 PR × NU（通貨記号）"},
      {"1,234円", "1,234|円", "LB31: 漢字の単位の前では割ってよい"},
      {"a1b", "a1b", "LB23 AL × NU / NU × AL"},
      {"3.14です", "3.14|で|す", "LB25 の数値は割らず、かなの前では割る"},
  });
}

// ------------------------------------------------------------ 欧文と和欧混植

TEST(LineBreakOpportunity, LatinWords) {
  expect_opportunities({
      {"Hello world", "Hello |world", "LB28 AL × AL / LB18 SP ÷: 欧文は空白でのみ割る"},
      {"Hello  world", "Hello  |world", "LB7 × SP / LB18 SP ÷: 空白が続いても割るのは最後"},
      {"e-mail", "e-|mail", "LB21 × HY / LB31: ハイフンの後ろでは割ってよい"},
      {"-mail", "-mail", "LB20a 語頭のハイフンの後ろでは割らない"},
      {"person(s)", "person(s)", "LB30 (AL|NU) × [OP-$EastAsian] / LB13 × CP"},
      {"e.g. foo", "e.g. |foo", "LB29 IS × AL / LB15d × IS"},
      {"\"quoted\"", "\"quoted\"", "LB19 × QU / QU ×"},
      {"a\u00A0b", "a\u00A0b", "LB12 GL × / LB12a [^SP HY] × GL（NBSP）"},
      {"a\u2060b", "a\u2060b", "LB11 × WJ / WJ ×"},
      {"a\u200Bb", "a\u200B|b", "LB8 ZW SP* ÷"},
  });
}

TEST(LineBreakOpportunity, MixedJapaneseAndLatin) {
  expect_opportunities({
      {"日本語とEnglishの混植", "日|本|語|と|English|の|混|植",
       "LB31 / LB28: 和文は毎文字、欧文は語単位"},
      {"G「ユニコード」", "G|「ユ|ニ|コー|ド」",
       "LB30 の $EastAsian 除外: 全角始め括弧の前では割ってよい（UAX #14 の例）"},
      {"ABC😀", "ABC|😀", "LB31: 絵文字は ID 扱い"},
  });
}

// ------------------------------------------------------- クラスタ・絵文字・制御

TEST(LineBreakOpportunity, ClustersAndEmoji) {
  expect_opportunities({
      {"あ\u0301い", "あ\u0301|い",
       "LB9 X (CM|ZWJ)*: 結合文字の前では割らない（基底の文字の類で判定する）"},
      {"e\u0301e\u0301", "e\u0301e\u0301",
       "LB9 + LB28 AL × AL: 結合文字つきのラテン文字も 1 語として扱う"},
      {"\U0001F468\u200D\U0001F469", "👨\u200D👩", "LB8a ZWJ ×: 絵文字 ZWJ 列の途中で割らない"},
      {"\U0001F44D\U0001F3FB", "👍🏻", "LB30b EB × EM: 肌色修飾子の前では割らない"},
      {"\U0001F1EF\U0001F1F5\U0001F1FA\U0001F1F8", "🇯🇵|🇺🇸", "LB30a: 地域表示記号は 2 個で 1 組"},
      {"\U0001F1EF\U0001F1F5\U0001F1FA", "🇯🇵|🇺", "LB30a: 3 個目は新しい組の始まり"},
  });
}

TEST(LineBreakOpportunity, ForcedBreak) {
  const std::vector<Item> items = test::items_of("あ\nい");
  const LineBreaker breaker;
  const std::vector<bool> opportunities = breaker.break_opportunities(items);
  ASSERT_EQ(opportunities.size(), 3U);
  EXPECT_FALSE(opportunities[0]);  // LB2 sot ×
  EXPECT_FALSE(opportunities[1]);  // LB6 × BK: ForcedBreak の前では割らない
  EXPECT_TRUE(opportunities[2]);   // LB4 BK !: ForcedBreak の直後で必ず割る
}

TEST(LineBreakOpportunity, EmptyInput) {
  const LineBreaker breaker;
  EXPECT_TRUE(breaker.break_opportunities({}).empty());
}

// ---------------------------------------------------- Strictness（CSS Text 3 §5.3）

TEST(LineBreakOpportunity, StrictnessSmallKana) {
  const Config normal = test::with_strictness(Strictness::Normal);
  const Config loose = test::with_strictness(Strictness::Loose);
  // 契約ヘッダ（line_breaker.hpp）の定義: strict は CJ を NS、normal / loose は ID として扱う。
  EXPECT_EQ(mark_opportunities("あっち"), "あっ|ち");
  EXPECT_EQ(mark_opportunities("あっち", normal), "あ|っ|ち");
  EXPECT_EQ(mark_opportunities("あっち", loose), "あ|っ|ち");
  EXPECT_EQ(mark_opportunities("カード"), "カー|ド");
  EXPECT_EQ(mark_opportunities("カード", normal), "カ|ー|ド");
  EXPECT_EQ(mark_opportunities("シャツ", loose), "シ|ャ|ツ");
}

TEST(LineBreakOpportunity, StrictnessLooseAdditions) {
  const Config normal = test::with_strictness(Strictness::Normal);
  const Config loose = test::with_strictness(Strictness::Loose);
  // CSS Text 3 §5.3「The following breaks are forbidden for normal and strict
  // line breaking and allowed in loose」
  EXPECT_EQ(mark_opportunities("人々の", normal), "人々|の");
  EXPECT_EQ(mark_opportunities("人々の", loose), "人|々|の");
  EXPECT_EQ(mark_opportunities("あ……い", normal), "あ……|い");
  EXPECT_EQ(mark_opportunities("あ……い", loose), "あ|…|…|い");
  EXPECT_EQ(mark_opportunities("あ・い", normal), "あ・|い");
  EXPECT_EQ(mark_opportunities("あ・い", loose), "あ|・|い");
  EXPECT_EQ(mark_opportunities("あ！い", normal), "あ！|い");
  EXPECT_EQ(mark_opportunities("あ！い", loose), "あ|！|い");
  EXPECT_EQ(mark_opportunities("100％", normal), "100％");
  EXPECT_EQ(mark_opportunities("100％", loose), "100|％");
  EXPECT_EQ(mark_opportunities("￥100", normal), "￥100");
  EXPECT_EQ(mark_opportunities("￥100", loose), "￥|100");
  // 「breaks before certain CJK hyphen-like characters」は normal からの差。
  EXPECT_EQ(mark_opportunities("あ〜い"), "あ〜|い");
  EXPECT_EQ(mark_opportunities("あ〜い", normal), "あ|〜|い");
  // 「breaks before hyphens if the preceding character belongs to class ID」は loose のみ。
  EXPECT_EQ(mark_opportunities("あ‐い", normal), "あ‐|い");
  EXPECT_EQ(mark_opportunities("あ‐い", loose), "あ|‐|い");
  // 直前が ID でなければ loose でもハイフンの前では割らない（後ろは LB31 で割ってよい）。
  EXPECT_EQ(mark_opportunities("a‐b", loose), "a‐|b");
  // strict / normal / loose のどれでも句読点と終わり括弧は行頭に来ない。
  for (const Config& config : {Config{}, normal, loose}) {
    EXPECT_EQ(mark_opportunities("あ。い", config), "あ。|い");
    EXPECT_EQ(mark_opportunities("あ」い", config), "あ」|い");
    EXPECT_EQ(mark_opportunities("あ「い", config), "あ|「い");
  }
}

// -------------------------------------------------------- Config と Item の追加指定

TEST(LineBreakOpportunity, ExtraProhibited) {
  Config config;
  config.extra_line_start_prohibited = U"ぬ";
  config.extra_line_end_prohibited = U"ね";
  EXPECT_EQ(mark_opportunities("あぬい", config), "あぬ|い");  // NS 相当に格上げ
  EXPECT_EQ(mark_opportunities("あねい", config), "あ|ねい");  // OP 相当に格上げ
  EXPECT_EQ(mark_opportunities("あぬい"), "あ|ぬ|い");         // 既定では普通の ID
}

TEST(LineBreakOpportunity, ExtraProhibitedBothSides) {
  Config config;
  config.extra_line_start_prohibited = U"の";
  config.extra_line_end_prohibited = U"の";
  // 行頭にも行末にも置けない文字は、前後どちらでも割れない。
  EXPECT_EQ(mark_opportunities("あのい", config), "あのい");
}

TEST(LineBreakOpportunity, NoBreakBefore) {
  std::vector<Item> items = test::items_of("あいう");
  items[1].no_break_before = true;
  const LineBreaker breaker;
  const std::vector<bool> opportunities = breaker.break_opportunities(items);
  EXPECT_FALSE(opportunities[1]);  // 呼び出し側が禁止した位置
  EXPECT_TRUE(opportunities[2]);
}

TEST(LineBreakOpportunity, ForcedBreakBeatsNoBreakBefore) {
  // LB4 は非適合化できない。no_break_before より強制改行が優先する。
  std::vector<Item> items = test::items_of("あ\nい");
  items[2].no_break_before = true;
  const LineBreaker breaker;
  EXPECT_TRUE(breaker.break_opportunities(items)[2]);
}

TEST(LineBreakOpportunity, AtomicIsIdeographic) {
  // 画像やルビのまとまりは ID 扱い（ARCHITECTURE.md §3.4 (2)）。
  std::vector<Item> items = test::items_of("あXい");
  items[1].kind = ItemKind::Atomic;
  const LineBreaker breaker;
  const std::vector<bool> opportunities = breaker.break_opportunities(items);
  EXPECT_TRUE(opportunities[1]);  // ID × ID ではなく LB31 で割ってよい
  EXPECT_TRUE(opportunities[2]);

  // 直後が句点なら行頭禁則が効く。
  std::vector<Item> with_period = test::items_of("あX。");
  with_period[1].kind = ItemKind::Atomic;
  EXPECT_FALSE(breaker.break_opportunities(with_period)[2]);
}

}  // namespace
}  // namespace shashoku::linebreak
