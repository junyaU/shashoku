#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// 行の決定（ARCHITECTURE.md §3.4 (3)）のテーブル駆動テスト。
// 字送りは test_support の既定（全角 1em = 16px、ASCII 0.5em = 8px）。
// 期待値は「行ごとの内容（[begin, content_end)）」で書く。
namespace shashoku::linebreak {
namespace {

struct LineCase {
  const char* input;
  float width;
  std::vector<std::string> expected;
  const char* source;
};

void expect_lines(const std::vector<LineCase>& cases, const Config& config = {}) {
  const LineBreaker breaker(config);
  for (const LineCase& c : cases) {
    SCOPED_TRACE(std::string(c.source) + " : \"" + c.input + "\" @" + std::to_string(c.width) +
                 "px");
    const std::vector<Item> items = test::items_of(c.input);
    const Breaks breaks = breaker.break_lines(items, c.width);
    EXPECT_EQ(test::line_texts(items, breaks), c.expected);
  }
}

// ------------------------------------------------------------ 基本（貪欲法）

TEST(LineBreakLines, GreedyFill) {
  expect_lines({
      // DESIGN.md Phase 3 の受け入れ条件: 幅 200px に 16px の全角 13 文字 → 2 行
      {"あいうえおかきくけこさしす",
       200.0F,
       {"あいうえおかきくけこさし", "す"},
       "DESIGN.md Phase 3 受け入れ条件"},
      {"あいうえおかきくけこさしす", 208.0F, {"あいうえおかきくけこさしす"}, "ちょうど収まる"},
      {"あいうえ", 1000.0F, {"あいうえ"}, "十分広ければ 1 行"},
      {"あいうえおか", 32.0F, {"あい", "うえ", "おか"}, "ARCHITECTURE.md §3.4 (3) 貪欲法"},
  });
}

TEST(LineBreakLines, EmptyInput) {
  const LineBreaker breaker;
  const Breaks breaks = breaker.break_lines({}, 100.0F);
  EXPECT_TRUE(breaks.lines.empty());
  EXPECT_TRUE(breaks.spacing.empty());
}

TEST(LineBreakLines, Unbounded) {
  // kUnbounded（max-content の計測）では ForcedBreak でしか改行しない。
  const LineBreaker breaker;
  const std::vector<Item> items = test::items_of("あいうえお。かきくけこ\nさしす");
  const Breaks breaks = breaker.break_lines(items, kUnbounded);
  EXPECT_EQ(test::line_texts(items, breaks),
            (std::vector<std::string>{"あいうえお。かきくけこ", "さしす"}));
  ASSERT_EQ(breaks.lines.size(), 2U);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, 11 * 16.0F);
  EXPECT_TRUE(breaks.lines[0].forced);
  EXPECT_FALSE(breaks.lines[0].overflows);
}

// ------------------------------------------------------ 行頭禁則（追い出し）

TEST(LineBreakLines, LineStartProhibitedPushesOut) {
  expect_lines({
      // 「。」が行頭に来る位置では割れないので、直前の「お」を道連れに次の行へ送る。
      {"あいうえお。かき",
       80.0F,
       {"あいうえ", "お。かき"},
       "JLREQ 3.1.1 行頭禁則 + ARCHITECTURE.md §3.4 (5) 追い出し"},
      {"あいうえお、かき", 80.0F, {"あいうえ", "お、かき"}, "読点の追い出し"},
      {"あいうえお」かき", 80.0F, {"あいうえ", "お」かき"}, "終わり括弧の追い出し"},
      {"あいうえお！かき", 80.0F, {"あいうえ", "お！かき"}, "感嘆符の追い出し"},
      {"あいうえおっかき", 80.0F, {"あいうえ", "おっかき"}, "小書き仮名の追い出し（strict）"},
      {"あいうえおーかき", 80.0F, {"あいうえ", "おーかき"}, "長音記号の追い出し（strict）"},
      {"あいうえお々かき", 80.0F, {"あいうえ", "お々かき"}, "繰り返し記号の追い出し"},
      {"あいうえお・かき", 80.0F, {"あいうえ", "お・かき"}, "中点の追い出し"},
      {"あいうえお……き", 80.0F, {"あいうえ", "お……き"}, "LB22 × IN: …… は 2 文字まとめて次の行へ"},
      // 収まるなら追い出さない
      {"あいうえお。かき", 96.0F, {"あいうえお。", "かき"}, "収まるなら句点は行末に残す"},
  });
}

TEST(LineBreakLines, ChainedProhibitedPushesOutTogether) {
  expect_lines({
      {"あいうえお。」かき",
       80.0F,
       {"あいうえ", "お。」かき"},
       "JLREQ 3.1.1: 連続する禁則文字はまとめて追い出す"},
      {"あいうえお！？かき", 96.0F, {"あいうえ", "お！？かき"}, "感嘆符と疑問符の連鎖"},
  });
}

// ---------------------------------------------------------------- 行末禁則

TEST(LineBreakLines, LineEndProhibited) {
  expect_lines({
      // 幅 80px には 5 文字ぶん入るが、5 文字目の始め括弧は行末に置けないので 4 文字で割る。
      {"あいうえ「おか」き",
       80.0F,
       {"あいうえ", "「おか」き"},
       "JLREQ 3.1.1 行末禁則（始め括弧）/ LB14 OP SP* ×"},
      {"あいうえ（おか）き", 80.0F, {"あいうえ", "（おか）き"}, "行末禁則（全角丸括弧）"},
      {"あいうえ【おか】き", 80.0F, {"あいうえ", "【おか】き"}, "行末禁則（隅付き括弧）"},
  });
}

// ---------------------------------------------------------------- 欧文・混植

TEST(LineBreakLines, LatinWords) {
  expect_lines({
      // 欧文は空白でしか割れない。"Hello" = 40px、"world" = 40px、空白 8px。
      {"Hello world", 48.0F, {"Hello", "world"}, "LB28 / LB18: 語の途中では割らない"},
      {"Hello world", 88.0F, {"Hello world"}, "1 行に収まる"},
      {"Hello world", 80.0F, {"Hello", "world"}, "行末の空白は幅に数えない"},
      {"e-mail address", 48.0F, {"e-mail", "address"}, "LB21 × HY: ハイフンの前では割らない"},
      {"日本語とEnglishの本",
       80.0F,
       {"日本語と", "Englishの", "本"},
       "和欧混植: 欧文の語はひとかたまり"},
  });
}

TEST(LineBreakLines, NumbersStayTogether) {
  expect_lines({
      // 幅 64px は「代金は1,」で埋まるが、数値の途中では割れないので数値ごと次の行へ。
      {"代金は1,234円です",
       64.0F,
       {"代金は", "1,234円", "です"},
       "JLREQ 3.1.1 分離禁則 / LB25 NU (SY|IS)* × NU"},
      {"価格は￥1,000から",
       64.0F,
       {"価格は", "￥1,000", "から"},
       "LB25 PR × NU: 通貨記号と数値を分離しない"},
      {"気温は20℃です",
       64.0F,
       {"気温は", "20℃です"},
       "LB25 NU (SY|IS)* × PO: 数値と単位を分離しない"},
  });
}

// ------------------------------------------------ Strictness（CSS Text 3 §5.3）

TEST(LineBreakLines, StrictnessChangesLineBreaks) {
  // strict では行頭に置けない文字が、normal / loose では置けるようになり行が伸びる。
  expect_lines({
      {"あいうえおっかき", 80.0F, {"あいうえ", "おっかき"}, "strict: 小書き仮名は行頭に置かない"},
      {"あいうえおーかき", 80.0F, {"あいうえ", "おーかき"}, "strict: 長音記号は行頭に置かない"},
  });
  expect_lines(
      {
          {"あいうえおっかき", 80.0F, {"あいうえお", "っかき"}, "normal: 小書き仮名は行頭に置ける"},
          {"あいうえおーかき", 80.0F, {"あいうえお", "ーかき"}, "normal: 長音記号は行頭に置ける"},
          {"あいうえお々かき",
           80.0F,
           {"あいうえ", "お々かき"},
           "normal: 繰り返し記号は行頭に置かない"},
      },
      test::with_strictness(Strictness::Normal));
  expect_lines(
      {
          {"あいうえお々かき",
           80.0F,
           {"あいうえお", "々かき"},
           "loose: 繰り返し記号は行頭に置ける"},
          {"あいうえお！かき", 80.0F, {"あいうえお", "！かき"}, "loose: 感嘆符は行頭に置ける"},
          {"あいうえお・かき", 80.0F, {"あいうえお", "・かき"}, "loose: 中点は行頭に置ける"},
      },
      test::with_strictness(Strictness::Loose));
  // どの Strictness でも句読点と終わり括弧は行頭に出ない（製品の存在理由）。
  for (const Strictness strictness : {Strictness::Strict, Strictness::Normal, Strictness::Loose}) {
    expect_lines({{"あいうえお。かき", 80.0F, {"あいうえ", "お。かき"}, "行頭禁則は共通"}},
                 test::with_strictness(strictness));
  }
}

// ------------------------------------------------------------ 空白と強制改行

TEST(LineBreakLines, TrailingSpaces) {
  // 行末の空白は幅に数えず、content_end から外すが、行（[begin, end)）には残す。
  const LineBreaker breaker;
  const std::vector<Item> items = test::items_of("あい うえ");
  const Breaks breaks = breaker.break_lines(items, 48.0F);
  EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あい", "うえ"}));
  EXPECT_EQ(test::line_texts_full(items, breaks), (std::vector<std::string>{"あい ", "うえ"}));
  ASSERT_EQ(breaks.lines.size(), 2U);
  EXPECT_EQ(breaks.lines[0].end, 3U);
  EXPECT_EQ(breaks.lines[0].content_end, 2U);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, 32.0F);
}

TEST(LineBreakLines, ForcedBreaks) {
  const LineBreaker breaker;
  {  // 途中・連続・先頭・末尾
    const std::vector<Item> items = test::items_of("あ\nい");
    const Breaks breaks = breaker.break_lines(items, 1000.0F);
    EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あ", "い"}));
    EXPECT_TRUE(breaks.lines[0].forced);
    EXPECT_FALSE(breaks.lines[1].forced);
  }
  {  // 連続した ForcedBreak は空行を作る
    const std::vector<Item> items = test::items_of("あ\n\nい");
    const Breaks breaks = breaker.break_lines(items, 1000.0F);
    EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あ", "", "い"}));
    EXPECT_EQ(breaks.lines[1].begin, 2U);
    EXPECT_EQ(breaks.lines[1].end, 3U);
    EXPECT_EQ(breaks.lines[1].content_end, 2U);
    EXPECT_FLOAT_EQ(breaks.lines[1].width, 0.0F);
  }
  {  // 先頭の ForcedBreak
    const std::vector<Item> items = test::items_of("\nあ");
    const Breaks breaks = breaker.break_lines(items, 1000.0F);
    EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"", "あ"}));
  }
  {  // 末尾の ForcedBreak: 空の行は作らない（Line は必ず 1 個以上のアイテムを持つ）
    const std::vector<Item> items = test::items_of("あ\n");
    const Breaks breaks = breaker.break_lines(items, 1000.0F);
    EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あ"}));
    ASSERT_EQ(breaks.lines.size(), 1U);
    EXPECT_EQ(breaks.lines[0].end, 2U);
    EXPECT_TRUE(breaks.lines[0].forced);
  }
  {  // ForcedBreak は幅より優先する
    const std::vector<Item> items = test::items_of("あい\nうえ");
    const Breaks breaks = breaker.break_lines(items, 1000.0F);
    EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あい", "うえ"}));
  }
}

// ---------------------------------------------------------- Atomic と no_break_before

TEST(LineBreakLines, AtomicItem) {
  // 画像やルビのまとまりは 1 個の分割不能な箱（ARCHITECTURE.md A2）。
  const LineBreaker breaker;
  std::vector<Item> items = test::items_of("あXい");
  items[1].kind = ItemKind::Atomic;
  items[1].advance = 48.0F;  // 48px の画像
  const Breaks breaks = breaker.break_lines(items, 64.0F);
  EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あX", "い"}));
  EXPECT_FLOAT_EQ(breaks.lines[0].width, 64.0F);
}

TEST(LineBreakLines, NoBreakBefore) {
  const LineBreaker breaker;
  std::vector<Item> items = test::items_of("あいうえ");
  items[2].no_break_before = true;  // 「い」と「う」の間では割らせない
  const Breaks breaks = breaker.break_lines(items, 32.0F);
  EXPECT_EQ(test::line_texts(items, breaks), (std::vector<std::string>{"あ", "いう", "え"}));
}

// ------------------------------------------------------------ min_content_width

TEST(LineBreakLines, MinContentWidth) {
  const LineBreaker breaker;
  EXPECT_FLOAT_EQ(breaker.min_content_width({}), 0.0F);
  // "あ|い。|う|え" → 分割不能な最長区間は "い。" = 32px
  EXPECT_FLOAT_EQ(breaker.min_content_width(test::items_of("あい。うえ")), 32.0F);
  // 欧文は語がまとまり。"Hello" = 40px
  EXPECT_FLOAT_EQ(breaker.min_content_width(test::items_of("Hello world")), 40.0F);
  // 分離禁則の数値: "1,234" = 40px
  EXPECT_FLOAT_EQ(breaker.min_content_width(test::items_of("あ1,234あ")), 40.0F);
  // 行末の空白は数えない
  EXPECT_FLOAT_EQ(breaker.min_content_width(test::items_of("あ   い")), 16.0F);
  // ForcedBreak は幅 0
  EXPECT_FLOAT_EQ(breaker.min_content_width(test::items_of("あ\nいう")), 16.0F);
}

// ---------------------------------------------------------- 行を覆う不変条件

TEST(LineBreakLines, LinesCoverAllItemsWithoutGaps) {
  const LineBreaker breaker;
  const std::vector<Item> items = test::items_of("あいうえお。かき「くけ」こ さしす\nせそ");
  const Breaks breaks = breaker.break_lines(items, 64.0F);
  ASSERT_FALSE(breaks.lines.empty());
  EXPECT_EQ(breaks.spacing.size(), items.size());
  std::size_t cursor = 0;
  for (const Line& line : breaks.lines) {
    EXPECT_EQ(line.begin, cursor);
    EXPECT_GT(line.end, line.begin) << "行は空でない";
    EXPECT_GE(line.content_end, line.begin);
    EXPECT_LE(line.content_end, line.end);
    cursor = line.end;
  }
  EXPECT_EQ(cursor, items.size());
}

}  // namespace
}  // namespace shashoku::linebreak
