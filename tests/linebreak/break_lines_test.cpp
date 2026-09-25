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

// -------------------------------------- 行末の全角スペース（U+3000 の tailoring。A59）
//
// U+3000 は LineBreak.txt では BA だが、shashoku は SP に tailoring している
// （break_class.cpp の tailor()）。ここで固定するのはその結果の 2 つ:
//   * 行末に来た並びは幅に数えず content_end から落ちる = ぶら下がる
//     （CSS Text 3 §4.1.3「Phase II: Trimming and Positioning」の 4:
//      行末に残った white space と other space separators（Zs から U+0020 と U+00A0 を
//      除いたもの。U+3000 はここに入る）は white-space: normal / nowrap で無条件にぶら下げる）
//   * LB14「OP SP* ×」に乗るので、始め括弧 + 全角スペースの後ろでは割らない
//     （JIS X 4051 の行末禁則「始め括弧類は行末に置かない」。JLREQ の該当節番号は未確認）

TEST(LineBreakLines, IdeographicSpaceAtLineEndHangs) {
  const std::string sp = test::to_utf8(0x3000);
  // 幅 84px = 全角 5.25 文字。全角スペースは直前の文字から離せない（tailoring 前は
  // LB21 × BA、いまは LB7 × SP）ので、行末でも幅に数えると「お」まで道連れになって
  // 次の行へ送られてしまう（再測定 2026-09-25 の case10 で「し」が孤立した原因）
  const std::string one = "あいうえお" + sp + "かきくけこ";
  const std::string two = "あいうえお" + sp + sp + "かきくけこ";
  const std::string middle = "あ" + sp + "いうえお";
  const std::string leading = sp + "いうえおか";
  const std::string bracket = "あいうえ（" + sp + "かきく";
  expect_lines({
      {one.c_str(),
       84.0F,
       {"あいうえお", "かきくけこ"},
       "CSS Text 3 §4.1.3 Phase II-4: 行末の U+3000 はぶら下がる"},
      {two.c_str(), 84.0F, {"あいうえお", "かきくけこ"}, "連続していても並びごとぶら下がる"},
      {middle.c_str(),
       84.0F,
       {"あ" + sp + "いうえ", "お"},
       "行の途中の U+3000 は全角 1 字分を幅に数える"},
      {leading.c_str(),
       84.0F,
       {sp + "いうえお", "か"},
       "行頭の U+3000 は残して幅に数える（Phase II-1 が消すのは畳み込む空白だけ）"},
      {bracket.c_str(),
       100.0F,
       {"あいうえ", "（" + sp + "かきく"},
       "UAX #14 LB14 OP SP* × / JIS X 4051 行末禁則: 始め括弧は行末に残さない"},
  });
}

TEST(LineBreakLines, TrailingIdeographicSpaceIsNotMeasured) {
  // 行末の半角スペース（TrailingSpaces）と同じ扱い: 幅に数えず content_end から外すが、
  // 行（[begin, end)）には残す。約物のぶら下げ（Line::hang）とは別の経路。
  const LineBreaker breaker;
  const std::string sp = test::to_utf8(0x3000);
  const std::vector<Item> items = test::items_of("あいうえお" + sp + "かきくけこ");
  const Breaks breaks = breaker.break_lines(items, 84.0F);
  ASSERT_EQ(breaks.lines.size(), 2U);
  EXPECT_EQ(breaks.lines[0].end, 6U);
  EXPECT_EQ(breaks.lines[0].content_end, 5U);
  EXPECT_FLOAT_EQ(breaks.lines[0].width, 80.0F);
  EXPECT_FLOAT_EQ(breaks.lines[0].hang, 0.0F);
  EXPECT_EQ(test::line_texts_full(items, breaks)[0], "あいうえお" + sp);
}

TEST(LineBreakLines, OtherSpaceSeparatorsAreNotTailored) {
  // tailoring の対象は U+3000 だけ。U+2002 EN SPACE は BA のまま（行末で幅に数える）、
  // U+202F NARROW NO-BREAK SPACE は GL のまま（前後で割らない）。
  const LineBreaker breaker;
  {
    const std::vector<Item> items = test::items_of("あいうえお" + test::to_utf8(0x2002) + "かき");
    const Breaks breaks = breaker.break_lines(items, 84.0F);
    // BA は幅に数えるので、直前の「お」ごと次の行へ送られる（tailoring 前の U+3000 と同じ）
    EXPECT_EQ(test::line_texts(items, breaks),
              (std::vector<std::string>{"あいうえ", "お" + test::to_utf8(0x2002) + "かき"}));
  }
  {
    const std::vector<Item> items = test::items_of("あいうえ" + test::to_utf8(0x202F) + "おかき");
    const Breaks breaks = breaker.break_lines(items, 80.0F);
    // GL は LB12a（× GL）で前でも LB12（GL ×）で後ろでも割れないので、直前の「え」ごと
    // 次の行へ行く。SP に tailoring していたら「あいうえ」で切れて幅も数えなくなってしまう
    EXPECT_EQ(test::line_texts(items, breaks),
              (std::vector<std::string>{"あいう", "え" + test::to_utf8(0x202F) + "おかき"}));
  }
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
  // 全角スペースも同じ（A59 の tailoring。区間の末尾に来るので数えない）
  EXPECT_FLOAT_EQ(breaker.min_content_width(test::items_of("あ" + test::to_utf8(0x3000) + "い")),
                  16.0F);
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
