#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "layout/counters.hpp"
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

// ---- シェーピング境界と装飾境界の分離（issue #8 / A24）---------------------------------
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
// 装飾を使う（A24）。偽の TextMeasurer では U+3099 が直前のクラスタに吸収される。
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

}  // namespace
}  // namespace shashoku::layout::test
