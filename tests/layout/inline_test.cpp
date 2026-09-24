#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "layout/counters.hpp"
#include "layout/engine.hpp"  // layout_without_memo（メモの有無で警告が変わらないことの検査）
#include "layout/test_support.hpp"
#include "linebreak/line_breaker.hpp"

// インライン整形文脈（ARCHITECTURE.md §3.8 の (a)〜(e)）。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

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

// ---- (a) 空白の畳み込み（white-space: normal 固定）---------------------------------

TEST(LayoutInline, CollapsesWhiteSpaceRuns) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("A  \t B")}, 400), (std::vector<std::string>{"A B"}));
}

TEST(LayoutInline, DropsWhiteSpaceAtTheStartOfTheContext) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("   A")}, 400), (std::vector<std::string>{"A"}));
  EXPECT_EQ(flow(measurer, {inline_box({text("  ")}), text("A")}, 400),
            (std::vector<std::string>{"A"}));
}

// 畳み込みの状態はテキストノードや span の境界をまたいで引き継ぐ。
TEST(LayoutInline, CollapseCrossesNodeBoundaries) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("A "), inline_box({text(" B")})}, 400),
            (std::vector<std::string>{"A B"}));
  EXPECT_EQ(flow(measurer, {text("A "), text(" "), inline_box({text(" B")})}, 400),
            (std::vector<std::string>{"A B"}));
}

// A14: 改行を含む空白の並びは、前後がどちらも全角なら消える。
TEST(LayoutInline, SourceLineBreakBetweenWideCharactersDisappears) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("あ\nい")}, 400), (std::vector<std::string>{"あい"}));
  EXPECT_EQ(flow(measurer, {text("あ  \n  い")}, 400), (std::vector<std::string>{"あい"}));
  // ノード境界をまたいでも同じ
  EXPECT_EQ(flow(measurer, {inline_box({text("あ\n")}), text("い")}, 400),
            (std::vector<std::string>{"あい"}));
  // BMP 外の和文の文字でも同じ（U+1B155 小書きカタカナ「コ」。issue #24）。
  // 全角判定の表の網羅は east_asian_width_test.cpp
  EXPECT_EQ(flow(measurer, {text("あ\n\U0001B155い")}, 400),
            (std::vector<std::string>{"あ\U0001B155い"}));
}

TEST(LayoutInline, SourceLineBreakBetweenLatinBecomesASpace) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("A\nB")}, 400), (std::vector<std::string>{"A B"}));
  // 和欧の境界は空白（片方だけ全角では消さない）
  EXPECT_EQ(flow(measurer, {text("あ\nA")}, 400), (std::vector<std::string>{"あ A"}));
  EXPECT_EQ(flow(measurer, {text("A\nあ")}, 400), (std::vector<std::string>{"A あ"}));
  // 改行を含まない空白は全角どうしでも残る
  EXPECT_EQ(flow(measurer, {text("あ い")}, 400), (std::vector<std::string>{"あ い"}));
}

// ---- <br> -------------------------------------------------------------------------

TEST(LayoutInline, ForcedBreakSplitsLines) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("あ"), br(), text("い")}, 400),
            (std::vector<std::string>{"あ", "い"}));
  // 先頭の <br> は空行を作る
  EXPECT_EQ(flow(measurer, {br(), text("あ")}, 400), (std::vector<std::string>{"", "あ"}));
  // 連続する <br> は間に空行を作る
  EXPECT_EQ(flow(measurer, {text("あ"), br(), br(), text("い")}, 400),
            (std::vector<std::string>{"あ", "", "い"}));
  // 末尾の <br> は行を増やさない（行分割器が行末の ForcedBreak を落とす。§3.4 (3)）
  EXPECT_EQ(flow(measurer, {text("あ"), br()}, 400), (std::vector<std::string>{"あ"}));
}

TEST(LayoutInline, WhiteSpaceAroundForcedBreakIsDropped) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("A "), br(), text(" B")}, 400),
            (std::vector<std::string>{"A", "B"}));
}

// <br> だけの空行は支柱（strut）の高さになる。
TEST(LayoutInline, EmptyLineUsesStrutHeight) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), br(), br(), text("い")},
                                 [](ComputedStyle& style) { style.font_size = 20; })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 3U);
  for (const LineBox* line : lines) {
    EXPECT_FLOAT_EQ(line->rect.block_size, fake_line_height(20));
  }
  EXPECT_TRUE(lines[1]->fragments.empty());
}

// ---- (b)(c) シェーピングと断片 -------------------------------------------------------

// A6: シェーピングは段落全体で 1 回。行ごとに測り直さない。
TEST(LayoutInline, ShapesOncePerStyleRunNotPerLine) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいうえおかきくけこさしす")})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(all_lines(*tree).size(), 2U);
  EXPECT_EQ(measurer.shape_calls, 1);
}

// フォールバックでフォントが変わる箇所で断片が分かれる。
TEST(LayoutInline, FallbackSplitsFragments) {
  FakeMeasurer measurer;
  measurer.fallback_chars = U"い";
  const auto root = build({block({text("あいう")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 3U);
  EXPECT_EQ(fragments[0]->font, 0U);
  EXPECT_EQ(fragments[1]->font, 1U);
  EXPECT_EQ(fragments[2]->font, 0U);
  EXPECT_EQ(fragments[0]->text, "あ");
  EXPECT_EQ(fragments[1]->text, "い");
  EXPECT_FLOAT_EQ(fragments[1]->inline_start, 16);
  EXPECT_FLOAT_EQ(fragments[1]->inline_size, 16);
}

// span による色・サイズの切り替えで断片が分かれ、ベースラインは揃う。
TEST(LayoutInline, SpanSplitsFragmentsAndSharesBaseline) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), inline_box({text("い")}, [](ComputedStyle& style) {
                                    style.font_size = 32;
                                    style.color = Color{255, 0, 0, 255};
                                  })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 2U);
  EXPECT_FLOAT_EQ(fragments[0]->font_size, 16);
  EXPECT_FLOAT_EQ(fragments[1]->font_size, 32);
  EXPECT_EQ(fragments[1]->color, (Color{255, 0, 0, 255}));
  // 行の高さは大きい方に合わせ、ベースラインは 32px の ascent の位置
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, fake_line_height(32));
  EXPECT_FLOAT_EQ(lines[0]->baseline, fake_ascent(32));
  EXPECT_FLOAT_EQ(fragments[0]->baseline, lines[0]->baseline);
  EXPECT_FLOAT_EQ(fragments[1]->baseline, lines[0]->baseline);
  EXPECT_FLOAT_EQ(fragments[1]->inline_start, 16);
  EXPECT_FLOAT_EQ(fragments[1]->inline_size, 32);
}

TEST(LayoutInline, LetterSpacingIsAddedToEveryCluster) {
  FakeMeasurer measurer;
  const auto root =
      build({block({text("あいう")}, [](ComputedStyle& style) { style.letter_spacing = 4; })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 20, 40}));
}

// ---- (e) 行の高さ（CSS 2.1 §10.8）-----------------------------------------------------

TEST(LayoutInline, LineHeightNormalUsesFontMetrics) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, fake_line_height(16));
  EXPECT_FLOAT_EQ(lines[0]->baseline, fake_ascent(16));
}

TEST(LayoutInline, LineHeightNumberAndPx) {
  FakeMeasurer measurer;
  const auto number = build({block({text("あ")}, [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Number, 2};
  })});
  const auto tree = run_layout(number, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, 32);
  // 半行間はベースラインの上下に等分される
  EXPECT_FLOAT_EQ(lines[0]->baseline, fake_ascent(16) + (32 - fake_line_height(16)) / 2);

  const auto pixels = build({block({text("あ")}, [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Px, 24};
  })});
  const auto tree2 = run_layout(pixels, 400, measurer);
  ASSERT_TRUE(tree2.has_value());
  EXPECT_FLOAT_EQ(all_lines(*tree2)[0]->rect.block_size, 24);
}

// 行内で line-height が違えば、上下それぞれの最大で行が決まる。
TEST(LayoutInline, LineHeightIsTheMaxOfTheFragments) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), inline_box({text("い")}, [](ComputedStyle& style) {
                                    style.line_height =
                                        style::LineHeight{style::LineHeight::Kind::Px, 40};
                                  })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(all_lines(*tree)[0]->rect.block_size, 40);
}

// ---- (e) text-align ----------------------------------------------------------------

float first_glyph(const LineBox& line) { return glyph_positions(line).front(); }

TEST(LayoutInline, TextAlignStartEndAndCenter) {
  FakeMeasurer measurer;
  const auto make = [](style::TextAlign align) {
    return build(
        {block({text("あいうえお")}, [align](ComputedStyle& style) { style.text_align = align; })});
  };
  for (const style::TextAlign align : {style::TextAlign::Start, style::TextAlign::Left}) {
    const auto tree = run_layout(make(align), 200, measurer);
    ASSERT_TRUE(tree.has_value());
    EXPECT_FLOAT_EQ(first_glyph(*all_lines(*tree)[0]), 0);
  }
  for (const style::TextAlign align : {style::TextAlign::End, style::TextAlign::Right}) {
    const auto tree = run_layout(make(align), 200, measurer);
    ASSERT_TRUE(tree.has_value());
    EXPECT_FLOAT_EQ(first_glyph(*all_lines(*tree)[0]), 120);
  }
  const auto tree = run_layout(make(style::TextAlign::Center), 200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(first_glyph(*all_lines(*tree)[0]), 60);
}

// A13: 両端揃え。最終行は揃えず、揃えた行の末尾は content の端にそろう。
TEST(LayoutInline, JustifySpreadsTheSlackOverBreakOpportunities) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいうえおかきくけこさしす")}, [](ComputedStyle& style) {
    style.text_align = style::TextAlign::Justify;
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  const std::vector<float> first = glyph_positions(*lines[0]);
  ASSERT_EQ(first.size(), 12U);
  EXPECT_FLOAT_EQ(first[0], 0);
  // 11 か所に 8px を均等配分する
  EXPECT_NEAR(first[1], 16 + (8.0F / 11), 1e-4);
  EXPECT_NEAR(first.back() + 16, 200, 1e-3);  // 行末が content の端にそろう
  // 最終行は揃えない
  EXPECT_FLOAT_EQ(glyph_positions(*lines[1])[0], 0);
}

// 強制改行で終わった行も最終行扱い（両端揃えにしない）。
TEST(LayoutInline, JustifySkipsForcedLines) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいう"), br(), text("えお")}, [](ComputedStyle& style) {
    style.text_align = style::TextAlign::Justify;
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 16, 32}));
}

// 禁則で割れない位置（読点の前）には空きを入れない。
TEST(LayoutInline, JustifyDoesNotOpenProhibitedGaps) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ、うえおかきくけこさしす")}, [](ComputedStyle& style) {
    style.text_align = style::TextAlign::Justify;
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<float> positions = glyph_positions(*all_lines(*tree)[0]);
  ASSERT_EQ(positions.size(), 12U);
  EXPECT_FLOAT_EQ(positions[0], 0);
  EXPECT_FLOAT_EQ(positions[1], 16);  // 「、」の前は割れないので空きを入れない
  EXPECT_NEAR(positions[2], 32 + 0.8F, 1e-4);  // 残り 10 か所に 8px
  EXPECT_NEAR(positions.back() + 16, 200, 1e-3);
}

// ---- (e) 約物の空き（Spacing）がグリフ位置に出る ---------------------------------------

// JLREQ 3.1.4: 始め括弧が続くときは間のアキを詰める。
TEST(LayoutInline, ConsecutivePunctuationIsTightened) {
  FakeMeasurer measurer;
  const auto root = build({block({text("「「あ")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 8, 24}));
}

TEST(LayoutInline, ClosingThenOpeningIsTightened) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ」「い")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  // JLREQ 3.1.4: 」の後ろの半角アキ（8px）を詰めるので、「 は 32 ではなく 24 に来る
  EXPECT_EQ(glyph_positions(*all_lines(*tree)[0]), (std::vector<float>{0, 16, 24, 40}));
}

// ---- インライン背景 ------------------------------------------------------------------

TEST(LayoutInline, SpanBackgroundCoversItsFragmentsPerLine) {
  FakeMeasurer measurer;
  const auto root = build({block(
      {text("あ"),
       inline_box({text("いう")},
                  [](ComputedStyle& style) { style.background_color = Color{0, 0, 255, 255}; }),
       text("え")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const InlineBackground*> rects = backgrounds(*lines[0]);
  ASSERT_EQ(rects.size(), 1U);
  EXPECT_EQ(rects[0]->color, (Color{0, 0, 255, 255}));
  EXPECT_FLOAT_EQ(rects[0]->rect.inline_start, 16);
  EXPECT_FLOAT_EQ(rects[0]->rect.inline_size, 32);
  EXPECT_FLOAT_EQ(rects[0]->rect.block_start, lines[0]->baseline - fake_ascent(16));
  EXPECT_FLOAT_EQ(rects[0]->rect.block_size, fake_line_height(16));
  // 描画順は背景 → 文字
  EXPECT_EQ(lines[0]->fragments.front().index(), 1U);
  EXPECT_EQ(lines[0]->fragments.back().index(), 0U);
}

// 入れ子と複数行が混ざっても、背景は「外側の span が先」の順で、行ごとに交差する範囲だけ出る。
TEST(LayoutInline, NestedBackgroundsKeepOuterFirstOnEveryLine) {
  FakeMeasurer measurer;
  const auto root = build({block(
      {inline_box({text("あい"),
                   inline_box({text("うえお")},
                              [](ComputedStyle& style) {
                                style.background_color = Color{0, 255, 0, 255};
                              }),
                   text("かき")},
                  [](ComputedStyle& style) { style.background_color = Color{0, 0, 255, 255}; }),
       inline_box({text("くけ")},
                  [](ComputedStyle& style) { style.background_color = Color{255, 0, 0, 255}; })})});
  const auto tree = run_layout(root, 64, measurer);  // 1 行 4 文字
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 3U);  // あいうえ / おかきく / け

  // 1 行目: 外側（あいうえ）→ 内側（うえ）
  const std::vector<const InlineBackground*> first = backgrounds(*lines[0]);
  ASSERT_EQ(first.size(), 2U);
  EXPECT_EQ(first[0]->color, (Color{0, 0, 255, 255}));
  EXPECT_FLOAT_EQ(first[0]->rect.inline_start, 0);
  EXPECT_FLOAT_EQ(first[0]->rect.inline_size, 64);
  EXPECT_EQ(first[1]->color, (Color{0, 255, 0, 255}));
  EXPECT_FLOAT_EQ(first[1]->rect.inline_start, 32);
  EXPECT_FLOAT_EQ(first[1]->rect.inline_size, 32);

  // 2 行目: 外側（おかき）→ 内側（お）→ 次の span（く）
  const std::vector<const InlineBackground*> second = backgrounds(*lines[1]);
  ASSERT_EQ(second.size(), 3U);
  EXPECT_EQ(second[0]->color, (Color{0, 0, 255, 255}));
  EXPECT_FLOAT_EQ(second[0]->rect.inline_size, 48);
  EXPECT_EQ(second[1]->color, (Color{0, 255, 0, 255}));
  EXPECT_FLOAT_EQ(second[1]->rect.inline_size, 16);
  EXPECT_EQ(second[2]->color, (Color{255, 0, 0, 255}));
  EXPECT_FLOAT_EQ(second[2]->rect.inline_start, 48);

  // 3 行目: もう外側の span は終わっている
  const std::vector<const InlineBackground*> third = backgrounds(*lines[2]);
  ASSERT_EQ(third.size(), 1U);
  EXPECT_EQ(third[0]->color, (Color{255, 0, 0, 255}));
  EXPECT_FLOAT_EQ(third[0]->rect.inline_start, 0);
  EXPECT_FLOAT_EQ(third[0]->rect.inline_size, 16);
}

TEST(LayoutInline, SpanBackgroundIsSplitPerLine) {
  FakeMeasurer measurer;
  const auto root =
      build({block({inline_box({text("あいうえおかきくけこさしす")}, [](ComputedStyle& style) {
        style.background_color = Color{0, 255, 0, 255};
      })})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  ASSERT_EQ(backgrounds(*lines[0]).size(), 1U);
  ASSERT_EQ(backgrounds(*lines[1]).size(), 1U);
  EXPECT_FLOAT_EQ(backgrounds(*lines[0])[0]->rect.inline_size, 192);
  EXPECT_FLOAT_EQ(backgrounds(*lines[1])[0]->rect.inline_size, 16);
}

// ---- overflow-wrap / line-break ------------------------------------------------------

TEST(LayoutInline, OverflowWrapAnywhereBreaksLongLatinWords) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("aaaaaaaaaa")}, 40),
            (std::vector<std::string>{"aaaaaaaaaa"}));  // normal: 割れずにはみ出す（A4）
  EXPECT_EQ(flow(measurer, {text("aaaaaaaaaa")}, 40,
                 [](ComputedStyle& style) { style.overflow_wrap = style::OverflowWrap::Anywhere; }),
            (std::vector<std::string>{"aaaaa", "aaaaa"}));
}

// CSS Text 3 §5.3: strict は小書き仮名の前で割らない。normal は割ってよい。
TEST(LayoutInline, LineBreakStrictnessComesFromTheBlockStyle) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {text("あっい")}, 32), (std::vector<std::string>{"あっ", "い"}));
  EXPECT_EQ(flow(measurer, {text("あっい")}, 16),
            (std::vector<std::string>{"あっ", "い"}));  // 禁則 > 幅（A4）
  EXPECT_EQ(flow(measurer, {text("あっい")}, 16,
                 [](ComputedStyle& style) { style.line_break = style::LineBreak::Normal; }),
            (std::vector<std::string>{"あ", "っ", "い"}));
  // loose では ・ の前でも割れる
  EXPECT_EQ(flow(measurer, {text("あ・い")}, 16,
                 [](ComputedStyle& style) { style.line_break = style::LineBreak::Loose; }),
            (std::vector<std::string>{"あ", "・", "い"}));
}

// `line-break: auto` は「エンジンの既定に従う」= Options::line_break.strictness を使う。
TEST(LayoutInline, LineBreakAutoFollowsTheEngineDefault) {
  FakeMeasurer measurer;
  Options options = make_options(16);
  options.line_break.strictness = linebreak::Strictness::Normal;
  const auto automatic = build({block({text("あっい")})});
  const auto tree = run_layout(automatic, options, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あ", "っ", "い"}));

  // CSS が明示していればそちらが勝つ
  const auto strict = build({block({text("あっい")}, [](ComputedStyle& style) {
    style.line_break = style::LineBreak::Strict;
  })});
  const auto strict_tree = run_layout(strict, options, measurer);
  ASSERT_TRUE(strict_tree.has_value());
  EXPECT_EQ(line_texts(*strict_tree), (std::vector<std::string>{"あっ", "い"}));
}

// ---- シェーピング境界と装飾境界の分離（issue #8 / A27）---------------------------------
//
// シェーピング属性（font-family / font-weight / font-size / direction）が等しい連続は
// 1 回の shape() にまとめ、色・letter-spacing・line-height の境界では切らない。
// 切るとその位置のカーニングと合字が消える（#8）。**偽の TextMeasurer はカーニングを
// 持たないので、位置が戻ったことは tests/integration/shaping_test.cpp（本物の Shaper）で見る。**
// ここで見るのは「切っていない」こと自体と、断片の分かれ方が変わっていないこと。

constexpr Color kRed{255, 0, 0, 255};
constexpr Color kBlue{0, 0, 255, 255};

// 色だけが違う隣接 span では shape() を 1 回しか呼ばない。
TEST(LayoutInline, ColorOnlySpansDoNotSplitShaping) {
  FakeMeasurer measurer;
  const auto root = build({block({
      text("A"),
      inline_box({text("V")}, [](ComputedStyle& style) { style.color = kRed; }),
      text("T"),
      inline_box({text("o")}, [](ComputedStyle& style) { style.color = kBlue; }),
  })});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  EXPECT_EQ(counters.shape_calls, 1U) << "色の境界でシェーピングが切れている（#8）";
  EXPECT_EQ(counters.shaped_chars, 4U);

  // 断片は色の境界で分かれたまま（描き分けは従来どおり）。位置は 1 回のシェーピングのもの
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 4U);
  EXPECT_EQ(fragments[0]->color, kBlack);
  EXPECT_EQ(fragments[1]->color, kRed);
  EXPECT_EQ(fragments[2]->color, kBlack);
  EXPECT_EQ(fragments[3]->color, kBlue);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 8, 16, 24}));
}

// letter-spacing・line-height・background-color も送り / 行の高さ / 背景に効くだけで、
// シェーピングの結果を変えない。
TEST(LayoutInline, OtherDecorationSpansDoNotSplitShaping) {
  FakeMeasurer measurer;
  const auto root = build({block({
      text("あ"),
      inline_box({text("い")}, [](ComputedStyle& style) { style.letter_spacing = 4; }),
      inline_box({text("う")},
                 [](ComputedStyle& style) {
                   style.line_height = style::LineHeight{style::LineHeight::Kind::Px, 40};
                 }),
      inline_box({text("え")}, [](ComputedStyle& style) { style.background_color = kRed; }),
  })});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  EXPECT_EQ(counters.shape_calls, 1U);
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  // letter-spacing は「い」の送りにだけ足される（クラスタ単位で効く）
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 16, 36, 52}));
  // line-height: 40px の span が行の高さを決める
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, 40);
  ASSERT_EQ(backgrounds(*lines[0]).size(), 1U);
}

// シェーピング属性の境界では従来どおり分かれる。
TEST(LayoutInline, ShapingAttributesStillSplitRuns) {
  const auto count_shape_calls = [](const StyleFn& span_style) {
    FakeMeasurer measurer;
    const auto root =
        build({block({text("あ"), inline_box({text("い")}, span_style), text("う")})});
    Counters counters;
    const auto tree = run_layout(root, make_options(400), measurer, counters);
    EXPECT_TRUE(tree.has_value());
    return counters.shape_calls;
  };

  EXPECT_EQ(count_shape_calls([](ComputedStyle& style) { style.font_size = 32; }), 3U);
  EXPECT_EQ(count_shape_calls([](ComputedStyle& style) { style.font_weight = 700; }), 3U);
  EXPECT_EQ(count_shape_calls([](ComputedStyle& style) { style.font_family = {"Other"}; }), 3U);
  // 比較: 色だけなら分かれない
  EXPECT_EQ(count_shape_calls([](ComputedStyle& style) { style.color = kRed; }), 1U);
}

// 1 つのクラスタ（結合文字・合字）が装飾の境界をまたぐときは、**クラスタ先頭の文字**の
// 装飾を使う（A27）。偽の TextMeasurer では U+3099 が直前のクラスタに吸収される。
TEST(LayoutInline, ClusterAcrossADecorationBoundaryTakesTheFirstCharsDecoration) {
  FakeMeasurer measurer;
  const auto root = build({block({
      text("か"),
      inline_box({text("゙")}, [](ComputedStyle& style) { style.color = kRed; }),
      text("き"),
  })});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  EXPECT_EQ(counters.shape_calls, 1U);
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  // 「か」+ 濁点 は 1 クラスタなので 1 つの断片にまとまり、色はクラスタ先頭の「か」のもの
  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_EQ(fragments[0]->color, kBlack);
  EXPECT_EQ(fragments[0]->glyphs.size(), 3U);
  EXPECT_EQ(glyph_positions(*lines[0]), (std::vector<float>{0, 16, 16}));
}

// ---- インライン要素の行分割ポリシー（issue #2 / A23・A28）-------------------------------
//
// `line-break` / `overflow-wrap` は CSS ではテキスト（インラインボックス）に適用される
// 継承プロパティなので、段落の途中の <span> で値が変わりうる。layout はその値を
// **クラスタ先頭の文字**の計算値として `linebreak::Item` に写す。
// 偽の TextMeasurer は全角 = 16px / 半角 = 8px。

StyleFn anywhere() {
  return [](ComputedStyle& style) { style.overflow_wrap = style::OverflowWrap::Anywhere; };
}
StyleFn line_break(style::LineBreak value) {
  return [value](ComputedStyle& style) { style.line_break = value; };
}
StyleFn loose() { return line_break(style::LineBreak::Loose); }
StyleFn normal() { return line_break(style::LineBreak::Normal); }
StyleFn strict() { return line_break(style::LineBreak::Strict); }

// issue #2 の表の 2 行目。span に書いた overflow-wrap が効く（黙って無視されない）。
TEST(LayoutInline, OverflowWrapOnASpanBreaksInsideIt) {
  FakeMeasurer measurer;
  // 幅 24px = 半角 3 文字。指定なしでは割れずにはみ出す（A4）
  EXPECT_EQ(flow(measurer, {text("ABCDEFGH")}, 24), (std::vector<std::string>{"ABCDEFGH"}));
  // ブロックに書いた場合（従来から動いていた）
  EXPECT_EQ(flow(measurer, {text("ABCDEFGH")}, 24, anywhere()),
            (std::vector<std::string>{"ABC", "DEF", "GH"}));
  // span に書いた場合（#2 で直したところ）。同じ結果になる
  EXPECT_EQ(flow(measurer, {inline_box({text("ABCDEFGH")}, anywhere())}, 24),
            (std::vector<std::string>{"ABC", "DEF", "GH"}));
}

// span に書いた line-break が効く（loose: 小書き仮名・中点などの前で割ってよい）。
TEST(LayoutInline, LineBreakOnASpanChangesTheClassOfItsOwnCharacters) {
  FakeMeasurer measurer;
  // 既定は strict（Options）。「・」の前では割らない
  EXPECT_EQ(flow(measurer, {text("あ・い")}, 16), (std::vector<std::string>{"あ・", "い"}));
  EXPECT_EQ(flow(measurer, {inline_box({text("あ・い")}, loose())}, 16),
            (std::vector<std::string>{"あ", "・", "い"}));
  // normal: 小書きの仮名の前で割ってよい
  EXPECT_EQ(flow(measurer, {text("あっい")}, 16), (std::vector<std::string>{"あっ", "い"}));
  EXPECT_EQ(flow(measurer, {inline_box({text("あっい")}, normal())}, 16),
            (std::vector<std::string>{"あ", "っ", "い"}));
}

// 入れ子（div: strict → span: loose → span: strict）。値はその文字が属する要素のもの。
TEST(LayoutInline, NestedSpansEachUseTheirOwnPolicy) {
  FakeMeasurer measurer;
  // 「・」を 3 つ、外側 strict / 中 loose / 内 strict に置く。幅 16px = 全角 1 文字。
  // strict の「・」は NS なので前で割れず、「あ・」「う・」は禁則を守ってはみ出す（A4）。
  // loose の「・」だけが ID に格下げされ、その前で割れる
  const std::vector<std::string> lines = flow(
      measurer,
      {text("あ・"), inline_box({text("い・"), inline_box({text("う・え")}, strict())}, loose())},
      16, strict());
  EXPECT_EQ(lines, (std::vector<std::string>{"あ・", "い", "・", "う・", "え"}));
}

// A23 の境界の規則: 緊急分割は位置の両側が anywhere のときだけ。
// anywhere の span の内部でだけ割れ、span の境界では割れない。
TEST(LayoutInline, EmergencyBreaksStayInsideTheAnywhereSpan) {
  FakeMeasurer measurer;
  // 前半 4 文字だけ anywhere。後半は割れないので、そのまま次の行へ出てはみ出す
  EXPECT_EQ(flow(measurer, {inline_box({text("ABCD")}, anywhere()), text("EFGH")}, 24),
            (std::vector<std::string>{"ABC", "DEFGH"}));
  // 逆向き: 後半だけ anywhere
  EXPECT_EQ(flow(measurer, {text("ABCD"), inline_box({text("EFGH")}, anywhere())}, 24),
            (std::vector<std::string>{"ABCDE", "FGH"}));
  // 隣り合う 2 つの anywhere の span。境界（D と E の間）も**両側が anywhere** なので割れる
  // = 1 つの anywhere の範囲として振る舞う（A23 の「両側が true のときだけ許す」の裏返し）
  EXPECT_EQ(
      flow(measurer,
           {inline_box({text("ABCD")}, anywhere()), inline_box({text("EFGH")}, anywhere())}, 24),
      (std::vector<std::string>{"ABC", "DEF", "GH"}));
}

// 性質: ブロックに書いた場合と、全文を包む span に書いた場合で行分割の結果が同じ
// （issue #10 の 2「段の境界で情報を落としていないか」の一般形）。
TEST(LayoutInline, BlockLevelAndSpanLevelPoliciesAgree) {
  struct Case {
    std::string text;
    float width = 0;
    StyleFn style;
  };
  const std::vector<Case> cases = {
      {"ABCDEFGH", 24, anywhere()},       {"あ・い・う", 16, loose()},
      {"あっいっう", 16, normal()},       {"あっいっう", 16, strict()},
      {"ab cdef ghij", 40, anywhere()},   {"あ、い。う", 32, loose()},
      {"わたしの写植です", 48, normal()},
  };
  for (const Case& test_case : cases) {
    FakeMeasurer measurer;
    const std::vector<std::string> on_block =
        flow(measurer, {text(test_case.text)}, test_case.width, test_case.style);
    const std::vector<std::string> on_span =
        flow(measurer, {inline_box({text(test_case.text)}, test_case.style)}, test_case.width);
    EXPECT_EQ(on_span, on_block) << test_case.text << " / 幅 " << test_case.width;
  }
}

// <br> と <img> は自分自身の計算値を使う（A28）。<br> は必ず改行するので結果には効かない。
TEST(LayoutInline, ForcedBreakInsideAnAnywhereSpanStillBreaks) {
  FakeMeasurer measurer;
  EXPECT_EQ(flow(measurer, {inline_box({text("ABC"), br(), text("DEF")}, anywhere())}, 400),
            (std::vector<std::string>{"ABC", "DEF"}));
}

// 固有寸法（min-content / max-content）にもアイテムごとの strictness が効く。
// flex の column + align-items: flex-start で shrink-to-fit を通して測る。
TEST(LayoutInline, IntrinsicSizesFollowSpanLevelPolicies) {
  const auto measure_min_content = [](const StyleFn& span_style) {
    FakeMeasurer measurer;
    const auto root =
        build({flex({block({inline_box({text("あっい")}, span_style)})}, [](ComputedStyle& style) {
          style.flex_direction = style::FlexDirection::Column;
          style.align_items = style::AlignItems::FlexStart;
        })});
    const auto tree = run_layout(root, 8, measurer);  // 利用可能幅を min-content より狭く
    EXPECT_TRUE(tree.has_value());
    if (!tree) {
      return 0.0F;
    }
    return tree->root.blocks()->front().blocks()->front().rect.inline_size;
  };

  // 既定は strict: 「あっ」は分離できないので min-content は 2 文字ぶん
  EXPECT_FLOAT_EQ(measure_min_content(nullptr), 32);
  // span に normal を書けば小書きの仮名の前で割れるので、1 文字ぶんまで下がる
  EXPECT_FLOAT_EQ(measure_min_content(normal()), 16);
}

// #8 の再発防止: 行分割ポリシーは見た目にもシェーピングにも効かない層なので、
// その境界で shape() も TextFragment も切れない（A27 の用途の表）。
TEST(LayoutInline, BreakingPolicySpansDoNotSplitShapingOrFragments) {
  FakeMeasurer measurer;
  const auto root = build({block({
      text("AV"),
      inline_box({text("To")}, anywhere()),
      inline_box({text("AV")}, loose()),
  })});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  EXPECT_EQ(counters.shape_calls, 1U) << "行分割ポリシーの境界でシェーピングが切れている";
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  // 見た目は全部同じなので断片も 1 つ（= paint の DrawGlyphs も 1 つ）
  EXPECT_EQ(text_fragments(*lines[0]).size(), 1U);

  // 色だけの span と組み合わせても、シェーピングは 1 回のまま
  const auto mixed = build({block({
      text("AV"),
      inline_box({text("To")}, [](ComputedStyle& style) { style.color = kRed; }),
      inline_box({text("AV")}, anywhere()),
  })});
  Counters mixed_counters;
  const auto mixed_tree = run_layout(mixed, make_options(400), measurer, mixed_counters);
  ASSERT_TRUE(mixed_tree.has_value());
  EXPECT_EQ(mixed_counters.shape_calls, 1U);
  // 断片が分かれるのは色の境界だけ（2 つ: 黒 "AV" → 赤 "To" → 黒 "AV" で 3 つ）
  EXPECT_EQ(text_fragments(*all_lines(*mixed_tree)[0]).size(), 3U);
}

// ---- 豆腐の記録と元ノードの位置（A31 / issue #9）------------------------------------

// **位置の層は見た目にもシェーピングにも効かない。** 位置だけが違う隣り合うテキストは
// 1 回で組み、断片も分けない（分けると issue #8 と同じでカーニングと合字が消える）。
TEST(LayoutMissingGlyph, LocationLayerDoesNotSplitShapingOrFragments) {
  FakeMeasurer measurer;
  const auto root = build({block({
      at(text("AV"), 10),
      at(inline_box({at(text("To"), 30)}), 20),
      at(text("AV"), 50),
  })});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(counters.shape_calls, 1U) << "位置の境界でシェーピングが切れている";
  ASSERT_EQ(all_lines(*tree).size(), 1U);
  const std::vector<const TextFragment*> fragments = text_fragments(*all_lines(*tree)[0]);
  ASSERT_EQ(fragments.size(), 1U) << "位置の境界で断片が切れている";
  // 断片の位置は先頭のグリフのもの（A31）
  EXPECT_EQ(fragments[0]->location.offset, 10U);
}

// 断片は装飾の境界で切れる。切れたら 2 つめ以降はその位置のノードを指す。
TEST(LayoutMissingGlyph, FragmentLocationFollowsTheFirstGlyphOfEachFragment) {
  FakeMeasurer measurer;
  const auto root = build({block({
      at(text("AB"), 10),
      at(inline_box({at(text("CD"), 30)}, [](ComputedStyle& style) { style.color = kRed; }), 20),
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const TextFragment*> fragments = text_fragments(*all_lines(*tree)[0]);
  ASSERT_EQ(fragments.size(), 2U);
  EXPECT_EQ(fragments[0]->location.offset, 10U);
  EXPECT_EQ(fragments[1]->location.offset, 30U);
}

// 報告の粒度は (コードポイント, テキストノード) の組ごとに 1 件。
// 並びは入力位置の昇順 → コードポイントの昇順。
TEST(LayoutMissingGlyph, OneRecordPerCodepointAndNodeSortedByPosition) {
  FakeMeasurer measurer;
  measurer.missing_chars = U"😀😃";
  const auto root = build({block({
      at(text("😃あ😀😀"), 40),  // 同じノードの同じ絵文字は 1 件
      at(text("😀"), 10),
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->missing_glyphs.size(), 3U);
  EXPECT_EQ(tree->missing_glyphs[0].location.offset, 10U);
  EXPECT_EQ(tree->missing_glyphs[0].codepoint, U'\U0001F600');
  EXPECT_EQ(tree->missing_glyphs[1].location.offset, 40U);
  EXPECT_EQ(tree->missing_glyphs[1].codepoint, U'\U0001F600');
  EXPECT_EQ(tree->missing_glyphs[2].location.offset, 40U);
  EXPECT_EQ(tree->missing_glyphs[2].codepoint, U'\U0001F603');
}

// <img> を含む段落は準備を共有しない（A29）ので、計測と配置で何度も組まれる。
// それでも警告は重複しない（LayoutEngine が (位置, コードポイント) で除いている）。
TEST(LayoutMissingGlyph, ParagraphsPreparedSeveralTimesDoNotDuplicateRecords) {
  FakeMeasurer measurer;
  measurer.missing_chars = U"😀";
  const ImageLookup images = image_table({{.src = "p", .id = 0, .width = 24, .height = 12}});
  const auto root = build({flex({block({at(text("あ😀"), 5), at(img("p"), 20)})})});
  const auto tree = run_layout(root, make_options(400), measurer, images);
  ASSERT_TRUE(tree.has_value());
  EXPECT_GT(measurer.shape_calls, 1) << "この入力では段落が何度も組まれる前提";
  ASSERT_EQ(tree->missing_glyphs.size(), 1U);
  EXPECT_EQ(tree->missing_glyphs[0].location.offset, 5U);
}

// メモ（A29）の有無で警告の中身が変わらない。
TEST(LayoutMissingGlyph, MemoDoesNotChangeTheRecords) {
  const auto root = build({flex({block({at(text("あ😀"), 5)}), block({at(text("😃"), 40)})})});
  FakeMeasurer with_memo;
  with_memo.missing_chars = U"😀😃";
  const auto memoized = run_layout(root, 400, with_memo);
  ASSERT_TRUE(memoized.has_value());
  FakeMeasurer plain;
  plain.missing_chars = U"😀😃";
  const auto without_memo = layout_without_memo(root, make_options(400), plain, ImageLookup{});
  ASSERT_TRUE(without_memo.has_value());
  EXPECT_EQ(memoized->missing_glyphs, without_memo->missing_glyphs);
  EXPECT_EQ(dump_json(*memoized), dump_json(*without_memo));
}

// ルビ: 親文字は文字の表から、<rt> は組の rt_style から位置を引く。
TEST(LayoutMissingGlyph, RubyBaseAndRubyTextAreBothRecorded) {
  FakeMeasurer measurer;
  measurer.missing_chars = U"😀😃";
  const auto root = build({block({at(ruby({at(text("😀"), 10), at(rt("😃"), 20)}), 5)})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->missing_glyphs.size(), 2U);
  EXPECT_EQ(tree->missing_glyphs[0].codepoint, U'\U0001F600');
  EXPECT_EQ(tree->missing_glyphs[0].location.offset, 10U);
  EXPECT_EQ(tree->missing_glyphs[1].codepoint, U'\U0001F603');
  EXPECT_EQ(tree->missing_glyphs[1].location.offset, 20U);
}

// ---- font-family の要求を満たせなかった記録（A57）--------------------------------------

// 満たせた（偽の計測器が family_request_unmet を立てない）なら 1 件も記録しない。
TEST(LayoutFontFallback, MetRequestsAreNotRecorded) {
  FakeMeasurer measurer;  // unmet_families は空
  const auto root = build({block({at(text("あ"), 10)}, [](ComputedStyle& style) {
    style.font_family = {"Noto Sans JP", "sans-serif"};
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_TRUE(tree->font_fallbacks.empty());
  EXPECT_EQ(dump_json(*tree).find("font_fallbacks"), std::string::npos);
}

// 記録は **font-family の並びごとに 1 件**。同じ並びなら入力順で最初のテキストノードの
// 先頭の位置を採り、並びは位置の昇順。
TEST(LayoutFontFallback, OneRecordPerFamilyStackSortedByPosition) {
  FakeMeasurer measurer;
  measurer.unmet_families = {"Mincho", "Kaisho"};
  const auto mincho = [](ComputedStyle& style) { style.font_family = {"Mincho", "serif"}; };
  const auto kaisho = [](ComputedStyle& style) { style.font_family = {"Kaisho"}; };
  const auto root = build({
      at(block({at(text("うしろ"), 60)}, mincho), 50),  // 同じ並び。位置は後ろなので採らない
      at(block({at(text("ふで"), 30)}, kaisho), 20),  // 別の並び → 別件
      at(block({at(text("まえ"), 10)}, mincho), 5),  // 同じ並びの最初のテキストノード
  });
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->font_fallbacks.size(), 2U);
  // 位置はテキストノードの先頭（ブロックの位置ではない）。昇順に並ぶ
  EXPECT_EQ(tree->font_fallbacks[0].location.offset, 10U);
  EXPECT_EQ(tree->font_fallbacks[0].families, (std::vector<std::string>{"Mincho", "serif"}));
  EXPECT_EQ(tree->font_fallbacks[1].location.offset, 30U);
  EXPECT_EQ(tree->font_fallbacks[1].families, (std::vector<std::string>{"Kaisho"}));
}

// <img> を含む段落は計測と配置で何度も組まれる（A29）。それでも記録は重複しない。
TEST(LayoutFontFallback, ParagraphsPreparedSeveralTimesDoNotDuplicateRecords) {
  FakeMeasurer measurer;
  measurer.unmet_families = {"Mincho"};
  const ImageLookup images = image_table({{.src = "p", .id = 0, .width = 24, .height = 12}});
  const auto root =
      build({flex({block({at(text("あ"), 5), at(img("p"), 20)},
                         [](ComputedStyle& style) { style.font_family = {"Mincho"}; })})});
  const auto tree = run_layout(root, make_options(400), measurer, images);
  ASSERT_TRUE(tree.has_value());
  EXPECT_GT(measurer.shape_calls, 1) << "この入力では段落が何度も組まれる前提";
  ASSERT_EQ(tree->font_fallbacks.size(), 1U);
  EXPECT_EQ(tree->font_fallbacks[0].location.offset, 5U);
}

// メモ（A29）の有無で記録が変わらない。
TEST(LayoutFontFallback, MemoDoesNotChangeTheRecords) {
  const auto mincho = [](ComputedStyle& style) { style.font_family = {"Mincho"}; };
  const auto root = build({flex({
      block({at(text("あ"), 5)}, mincho),
      block({at(text("い"), 40)}, mincho),
  })});
  FakeMeasurer with_memo;
  with_memo.unmet_families = {"Mincho"};
  const auto memoized = run_layout(root, 400, with_memo);
  ASSERT_TRUE(memoized.has_value());
  FakeMeasurer plain;
  plain.unmet_families = {"Mincho"};
  const auto without_memo = layout_without_memo(root, make_options(400), plain, ImageLookup{});
  ASSERT_TRUE(without_memo.has_value());
  EXPECT_EQ(memoized->font_fallbacks, without_memo->font_fallbacks);
  EXPECT_EQ(dump_json(*memoized), dump_json(*without_memo));
}

// ルビ: 親文字と <rt> のどちらの並びも拾う（豆腐と同じ経路）。
TEST(LayoutFontFallback, RubyBaseAndRubyTextAreBothRecorded) {
  FakeMeasurer measurer;
  measurer.unmet_families = {"Mincho", "Rt"};
  const auto rt_style = [](ComputedStyle& style) { style.font_family = {"Rt"}; };
  const auto root =
      build({block({at(ruby({at(text("漢"), 10), at(rt("かん", rt_style), 20)},
                            [](ComputedStyle& style) { style.font_family = {"Mincho"}; }),
                       5)})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->font_fallbacks.size(), 2U);
  EXPECT_EQ(tree->font_fallbacks[0].families, (std::vector<std::string>{"Mincho"}));
  EXPECT_EQ(tree->font_fallbacks[0].location.offset, 10U);
  EXPECT_EQ(tree->font_fallbacks[1].families, (std::vector<std::string>{"Rt"}));
  EXPECT_EQ(tree->font_fallbacks[1].location.offset, 20U);
}

// ダンプ（--dump-stage box）。豆腐・はみ出しと同じ流儀で、1 件も無ければキーごと省く。
TEST(LayoutFontFallback, DumpJsonListsFontFallbacks) {
  FakeMeasurer measurer;
  measurer.unmet_families = {"Mincho"};
  const auto root = build({block({at(text("あ"), 10)}, [](ComputedStyle& style) {
    style.font_family = {"Mincho", "serif"};
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_NE(dump_json(*tree).find(R"("font_fallbacks": [
    {
      "families": [
        "Mincho",
        "serif"
      ],
      "location": "1:11"
    }
  ])"),
            std::string::npos)
      << dump_json(*tree);
}

}  // namespace
}  // namespace shashoku::layout::test
