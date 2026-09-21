#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "layout/counters.hpp"
#include "layout/test_support.hpp"

// ルビ（DESIGN.md Phase 7 / §6-4、ARCHITECTURE.md §3.8）。
// 「親文字 + <rt>」1 組が linebreak::Item 1 個（Atomic）になり、組の内部では改行しない。
// ルビ文字はベースラインの違う TextFragment として出す（variant は増やさない）。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Display;

// 親文字 16px / ルビ 8px（UA スタイルの 50%）。
constexpr float kBase = 16;
constexpr float kRuby = 8;
// 「ちょうど行の端に接する」を許すための許容差（float の積み上げ誤差ぶん）。
constexpr float kTolerance = 1.0F / 1024.0F;

// 注意: 共有ヘルパーの line_texts() は行の全 TextFragment を描画順に連結するので、
// ルビ文字もそこに入る（「親文字 → ルビ」の順）。親文字だけを見たいときは
// base_fragments() を使う。
//
// 行内の TextFragment を「ベースラインが行のものと同じか」で分ける。
std::vector<const TextFragment*> ruby_fragments(const LineBox& line) {
  std::vector<const TextFragment*> out;
  for (const TextFragment* fragment : text_fragments(line)) {
    if (fragment->baseline != line.baseline) {
      out.push_back(fragment);
    }
  }
  return out;
}

std::vector<const TextFragment*> base_fragments(const LineBox& line) {
  std::vector<const TextFragment*> out;
  for (const TextFragment* fragment : text_fragments(line)) {
    if (fragment->baseline == line.baseline) {
      out.push_back(fragment);
    }
  }
  return out;
}

std::vector<float> glyph_positions_of(const std::vector<const TextFragment*>& fragments) {
  std::vector<float> out;
  for (const TextFragment* fragment : fragments) {
    for (const PositionedGlyph& glyph : fragment->glyphs) {
      out.push_back(glyph.inline_position);
    }
  }
  return out;
}

// 親文字のグリフのペン位置（ルビ文字はベースラインが違うので入らない）。
std::vector<float> base_glyph_positions(const LineBox& line) {
  return glyph_positions_of(base_fragments(line));
}

std::vector<float> ruby_glyph_positions(const LineBox& line) {
  return glyph_positions_of(ruby_fragments(line));
}

// ---- 1 組のルビ -------------------------------------------------------------------

TEST(LayoutRuby, SinglePair) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);

  const std::vector<const TextFragment*> base = base_fragments(*lines[0]);
  const std::vector<const TextFragment*> ruby = ruby_fragments(*lines[0]);
  ASSERT_EQ(base.size(), 1U);
  ASSERT_EQ(ruby.size(), 1U);
  EXPECT_EQ(base[0]->text, "漢");
  EXPECT_EQ(ruby[0]->text, "かん");
  EXPECT_FLOAT_EQ(base[0]->font_size, kBase);
  EXPECT_FLOAT_EQ(ruby[0]->font_size, kRuby);
  // 親文字 16 とルビ 16（8 × 2 文字）は同じ幅なので、どちらも先頭から
  EXPECT_FLOAT_EQ(base[0]->inline_start, 0);
  EXPECT_FLOAT_EQ(ruby[0]->inline_start, 0);
  // ルビのベースライン = 親文字の内容領域の上端 − ルビの descent
  EXPECT_FLOAT_EQ(ruby[0]->baseline, lines[0]->baseline - fake_ascent(kBase) - fake_descent(kRuby));
  // 行はルビのぶん上に広がる
  EXPECT_FLOAT_EQ(lines[0]->baseline,
                  fake_ascent(kBase) + fake_ascent(kRuby) + fake_descent(kRuby));
}

// ルビが親文字より長い: 親文字を中央に置く。
TEST(LayoutRuby, LongerRubyCentresTheBase) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とうきょう")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // 組の送り = max(16, 8 × 5) = 40
  EXPECT_FLOAT_EQ(base_fragments(line)[0]->inline_start, 12);  // (40 − 16) / 2
  EXPECT_FLOAT_EQ(ruby_fragments(line)[0]->inline_start, 0);
  // 行内の送りは 40（次の文字がそこから始まる）
  const auto next = build({block({ruby({text("東"), rt("とうきょう")}), text("都")})});
  const auto tree2 = run_layout(next, 400, measurer);
  ASSERT_TRUE(tree2.has_value());
  const LineBox& line2 = *all_lines(*tree2)[0];
  const std::vector<const TextFragment*> base = base_fragments(line2);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_EQ(base[1]->text, "都");
  EXPECT_FLOAT_EQ(base[1]->inline_start, 40);
}

// ルビが親文字より短い: ルビを中央に置く。
TEST(LayoutRuby, ShorterRubyIsCentred) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東京"), rt("とう")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_FLOAT_EQ(base_fragments(line)[0]->inline_start, 0);
  EXPECT_FLOAT_EQ(ruby_fragments(line)[0]->inline_start, 8);  // (32 − 16) / 2
}

// 1 つの <ruby> に複数組。
TEST(LayoutRuby, MultiplePairsInOneRubyElement) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とう"), text("京"), rt("きょう")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const TextFragment*> ruby = ruby_fragments(line);
  ASSERT_EQ(ruby.size(), 2U);
  EXPECT_EQ(ruby[0]->text, "とう");
  EXPECT_EQ(ruby[1]->text, "きょう");
  // 1 組目は max(16, 16) = 16、2 組目は max(16, 24) = 24
  EXPECT_FLOAT_EQ(ruby[0]->inline_start, 0);
  EXPECT_FLOAT_EQ(ruby[1]->inline_start, 16);
  const std::vector<const TextFragment*> base = base_fragments(line);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_FLOAT_EQ(base[1]->inline_start, 16 + 4);  // (24 − 16) / 2
}

// <rt> が続かない親文字はルビなしの普通のテキスト。
TEST(LayoutRuby, BaseWithoutAnnotationStaysPlainText) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とう"), text("都")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_EQ(ruby_fragments(line).size(), 1U);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東とう都"}));
}

// 親文字の span（色・背景・フォールバック）は普通のテキストと同じに効く。
TEST(LayoutRuby, BaseKeepsSpanStyling) {
  FakeMeasurer measurer;
  measurer.fallback_chars = U"京";
  const auto root = build({block(
      {inline_box({ruby({text("東"), text("京"), rt("とうきょう")})},
                  [](ComputedStyle& style) { style.background_color = Color{0, 255, 0, 255}; })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // フォールバックで親文字の断片が 2 つに分かれる
  const std::vector<const TextFragment*> base = base_fragments(line);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_EQ(base[0]->font, 0U);
  EXPECT_EQ(base[1]->font, 1U);
  // span の背景は組ぜんぶを覆う
  ASSERT_EQ(backgrounds(line).size(), 1U);
  EXPECT_FLOAT_EQ(backgrounds(line)[0]->rect.inline_size, 40);
}

// ---- 行の高さ ---------------------------------------------------------------------

// line-height に余裕があれば、ルビが付いても行の高さは変わらない（DESIGN.md §6-4）。
TEST(LayoutRuby, DoesNotGrowTheLineWhenLineHeightHasRoom) {
  FakeMeasurer measurer;
  const auto tall = [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Number, 3};
  };
  const auto plain = build({block({text("漢")}, tall)});
  const auto annotated = build({block({ruby({text("漢"), rt("かん")})}, tall)});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(annotated, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_FLOAT_EQ(all_lines(*a)[0]->rect.block_size, 48);
  EXPECT_FLOAT_EQ(all_lines(*b)[0]->rect.block_size, 48);
  EXPECT_FLOAT_EQ(all_lines(*a)[0]->baseline, all_lines(*b)[0]->baseline);
}

// line-height が小さければ、ルビのぶんだけ block-start 側に広がる。
TEST(LayoutRuby, GrowsTheLineWhenLineHeightIsTight) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const float above = fake_ascent(kBase) + fake_ascent(kRuby) + fake_descent(kRuby);
  EXPECT_FLOAT_EQ(line.baseline, above);
  EXPECT_FLOAT_EQ(line.rect.block_size, above + fake_descent(kBase));
}

// ---- 行分割 -----------------------------------------------------------------------

// 組の内部では割らない（Atomic）。収まらなければはみ出す（A4）。
TEST(LayoutRuby, DoesNotBreakInsideAPair) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東京"), rt("とうきょう")})})});
  const auto tree = run_layout(root, 20, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(all_lines(*tree).size(), 1U);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東京とうきょう"}));
}

// 組どうしの間では割れる。
TEST(LayoutRuby, BreaksBetweenPairs) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とう"), text("京"), rt("きょう")})})});
  const auto tree = run_layout(root, 20, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東とう", "京きょう"}));
}

// ルビ組の直後の句読点は行頭に来ない（Atomic の後ろでも禁則が効く）。
TEST(LayoutRuby, PunctuationAfterAPairNeverStartsALine) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あい"), ruby({text("漢"), rt("かん")}), text("、うえ")})});
  const auto tree = run_layout(root, 48, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<std::string> lines = line_texts(*tree);
  ASSERT_GE(lines.size(), 2U);
  for (const std::string& line : lines) {
    EXPECT_NE(line.rfind("、", 0), 0U) << line;
  }
}

// ルビを含む段落の両端揃え（A13）。組は 1 アイテムなので、その前後だけが広がる。
TEST(LayoutRuby, JustifyTreatsThePairAsOneItem) {
  FakeMeasurer measurer;
  const auto root = build(
      {block({text("あいうえお"), ruby({text("漢"), rt("かん")}), text("かきくけこさしすせそ")},
             [](ComputedStyle& style) { style.text_align = style::TextAlign::Justify; })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_GE(lines.size(), 2U);
  // 最初の行は content の端までそろう
  const std::vector<float> positions = glyph_positions(*lines[0]);
  ASSERT_FALSE(positions.empty());
  EXPECT_FLOAT_EQ(positions.front(), 0);
}

// ---- 縦書きのルビ ------------------------------------------------------------------

// 縦書きではルビは親文字の右（block-start 側）に来る。
// block 座標は右端からの距離なので、block-start 側 = 値が小さい側（横書きの「上」と同じ向き）。
TEST(LayoutRuby, VerticalRubyGoesToTheBlockStartSide) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, vertical_options(400, 200), measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const TextFragment*> ruby = ruby_fragments(line);
  ASSERT_EQ(ruby.size(), 1U);
  // 中心軸から親文字の半分 + ルビの半分ぶん block-start 側（= 紙面の右）へ
  EXPECT_FLOAT_EQ(ruby[0]->baseline, line.baseline - (kBase / 2) - (kRuby / 2));
  EXPECT_LT(ruby[0]->baseline, line.baseline);
  // 行は中心軸から block-start 側に「親文字の半分 + ルビ」ぶん広がる
  EXPECT_FLOAT_EQ(line.baseline, (kBase / 2) + kRuby);
  // 空けた側と置いた側が同じ: ルビは行の block-start 端の内側に収まる
  EXPECT_GE(ruby[0]->baseline - (kRuby / 2), line.rect.block_start - kTolerance);
}

// 横書きのルビも block-start 側（= 上 = block 座標が小さい側）。
TEST(LayoutRuby, HorizontalRubyIsOnTheBlockStartSide) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const TextFragment*> ruby = ruby_fragments(line);
  ASSERT_EQ(ruby.size(), 1U);
  EXPECT_LT(ruby[0]->baseline, line.baseline);
  EXPECT_GE(ruby[0]->baseline - fake_ascent(kRuby), line.rect.block_start - kTolerance);
}

// line-height が小さいとき、行の block-start 端はルビを含むところまで広がる。
TEST(LayoutRuby, VerticalLineGrowsOnTheBlockStartSideForRuby) {
  FakeMeasurer measurer;
  const auto tight = [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Px, 16};
  };
  const auto plain = build_vertical({block({text("漢")}, tight)});
  const auto annotated = build_vertical({block({ruby({text("漢"), rt("かん")})}, tight)});
  const auto a = run_layout(plain, vertical_options(400, 200), measurer);
  const auto b = run_layout(annotated, vertical_options(400, 200), measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  const LineBox& without = *all_lines(*a)[0];
  const LineBox& with = *all_lines(*b)[0];
  // ルビのぶん行が広がり、広がったのは block-start 側（中心軸が block-end 寄りに動く）
  EXPECT_GT(with.rect.block_size, without.rect.block_size);
  EXPECT_GT(with.baseline, without.baseline);
  EXPECT_FLOAT_EQ(with.rect.block_end() - with.baseline,
                  without.rect.block_end() - without.baseline);
}

// 前の行（block-start 側 = 右隣）とルビが重ならない。
TEST(LayoutRuby, VerticalRubyDoesNotReachIntoThePreviousLine) {
  FakeMeasurer measurer;
  // 1 行目は普通の文字、2 行目にルビ。行の長さ 32（全角 2 文字）で折り返す
  const auto root = build_vertical({block({text("あい"), br(), ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, vertical_options(400, 32), measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  // 行は block 方向に隙間なく積まれる
  EXPECT_FLOAT_EQ(lines[0]->rect.block_end(), lines[1]->rect.block_start);
  const std::vector<const TextFragment*> ruby = ruby_fragments(*lines[1]);
  ASSERT_EQ(ruby.size(), 1U);
  // ルビの block-start 端が 1 行目に食い込まない
  EXPECT_GE(ruby[0]->baseline - (kRuby / 2), lines[1]->rect.block_start - kTolerance);
  EXPECT_GE(ruby[0]->baseline - (kRuby / 2), lines[0]->rect.block_end() - kTolerance);
}

// ---- 字間（letter-spacing）。issue #16 ------------------------------------------------
//
// CSS Ruby 1 §2: "ruby bases … are treated as inline boxes, and all properties that apply to
// inline boxes … also apply to them"。親文字は通常のインライン内容と同じに組む。
// つまり「計測（行分割器に渡す送り）」と「配置」が同じ 1 本の道を通る。

// 親文字のグリフ位置が、同じ指定の通常テキストと一致する。
TEST(LayoutRuby, BaseWithLetterSpacingIsPlacedLikePlainText) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto plain = build({block({text("東京都")}, spaced)});
  const auto annotated = build({block({ruby({text("東京都"), rt("と")})}, spaced)});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(annotated, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  // 親文字 3 × (16 + 8) = 72 はルビ 8 より長いので、組の中でのずらしは 0
  EXPECT_EQ(base_glyph_positions(*all_lines(*b)[0]), (std::vector<float>{0, 24, 48}));
  EXPECT_EQ(base_glyph_positions(*all_lines(*b)[0]), glyph_positions(*all_lines(*a)[0]));
}

// ルビ文字は「実際に配置された親文字」の中央に来る。
TEST(LayoutRuby, RubyIsCentredOverThePlacedBase) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto root = build({block({ruby({text("東京都"), rt("と")})}, spaced)});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<float> base = base_glyph_positions(line);
  const std::vector<float> ruby = ruby_glyph_positions(line);
  ASSERT_EQ(base.size(), 3U);
  ASSERT_EQ(ruby.size(), 1U);
  // 配置後の親文字が占める範囲は [最初のグリフ, 最後のグリフ + その送り]。
  // 送りは字間込み（通常テキストと同じ）なので、末尾の字間も範囲に入る
  const float base_centre = (base.front() + base.back() + kBase + 8) / 2;
  EXPECT_FLOAT_EQ(ruby.front() + (kRuby / 2), base_centre);
}

// 性質テスト: 行分割器に渡した送り（= 組の直後の文字が始まる位置）と、配置後の親文字の
// 範囲が一致する。計測と配置が別の式になっていると、ここが食い違う（#16 の本体）。
TEST(LayoutRuby, MeasuredAdvanceMatchesThePlacedBase) {
  for (const bool vertical : {false, true}) {
    for (const float spacing : {0.0F, 8.0F, -4.0F}) {
      FakeMeasurer measurer;
      const auto spaced = [spacing](ComputedStyle& style) { style.letter_spacing = spacing; };
      const auto children = [&spaced] {
        return std::vector<Tree>{block({ruby({text("東京"), rt("と")}), text("次")}, spaced)};
      };
      const auto root = vertical ? build_vertical(children()) : build(children());
      const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                                 : run_layout(root, make_options(400), measurer);
      ASSERT_TRUE(tree.has_value()) << vertical << " " << spacing;
      const LineBox& line = *all_lines(*tree)[0];
      const std::vector<float> base = base_glyph_positions(line);
      ASSERT_EQ(base.size(), 3U) << vertical << " " << spacing;  // 東 京 次
      // 親文字 2 × (16 + 字間) はルビ 8 より長いので、組の送り = 親文字の送り
      const float advance = 2 * (kBase + spacing);
      EXPECT_FLOAT_EQ(base[0], 0) << vertical << " " << spacing;
      EXPECT_FLOAT_EQ(base[1], kBase + spacing) << vertical << " " << spacing;
      // 配置後の親文字の終わり（最後のクラスタの送りまで）= 組の直後の文字の開始位置
      EXPECT_FLOAT_EQ(base[1] + kBase + spacing, advance) << vertical << " " << spacing;
      EXPECT_FLOAT_EQ(base[2], advance) << vertical << " " << spacing;
    }
  }
}

// 字間のあるルビでも、色だけの span はグリフ位置を動かさない（#8 / A27 を字間ありで）。
TEST(LayoutRuby, ColorSpanInTheBaseDoesNotMoveGlyphsWithLetterSpacing) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto red = [](ComputedStyle& style) { style.color = Color{255, 0, 0, 255}; };
  const auto plain = build({block({ruby({text("東京都"), rt("と")})}, spaced)});
  const auto split = build(
      {block({ruby({text("東"), inline_box({text("京")}, red), text("都"), rt("と")})}, spaced)});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(split, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(base_fragments(*all_lines(*b)[0]).size(), 3U);  // 色の境界で断片は分かれる
  EXPECT_EQ(base_glyph_positions(*all_lines(*b)[0]), base_glyph_positions(*all_lines(*a)[0]));
}

// 親文字の一部だけを覆う background-color は、通常テキストと同じ矩形になる（#16 の決定 4）。
TEST(LayoutRuby, BackgroundOnPartOfTheBaseMatchesPlainText) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto filled = [](ComputedStyle& style) { style.background_color = Color{0, 255, 0, 255}; };
  struct Case {
    std::string_view name;
    bool wrap_first;  // true: 先頭 2 文字を包む / false: 真ん中の 1 文字だけ包む
  };
  for (const Case& test_case : {Case{"middle", false}, Case{"leading", true}}) {
    const auto pieces = [&](bool as_ruby) {
      std::vector<Tree> children;
      if (test_case.wrap_first) {
        children.push_back(inline_box({text("東"), text("京")}, filled));
        children.push_back(text("都"));
      } else {
        children.push_back(text("東"));
        children.push_back(inline_box({text("京")}, filled));
        children.push_back(text("都"));
      }
      if (as_ruby) {
        children.push_back(rt("と"));
        return std::vector<Tree>{block({ruby(std::move(children))}, spaced)};
      }
      return std::vector<Tree>{block(std::move(children), spaced)};
    };
    const auto a = run_layout(build(pieces(false)), 400, measurer);
    const auto b = run_layout(build(pieces(true)), 400, measurer);
    ASSERT_TRUE(a.has_value()) << test_case.name;
    ASSERT_TRUE(b.has_value()) << test_case.name;
    const std::vector<const InlineBackground*> plain = backgrounds(*all_lines(*a)[0]);
    const std::vector<const InlineBackground*> ruby = backgrounds(*all_lines(*b)[0]);
    ASSERT_EQ(plain.size(), 1U) << test_case.name;
    ASSERT_EQ(ruby.size(), 1U) << test_case.name;
    EXPECT_FLOAT_EQ(ruby[0]->rect.inline_start, plain[0]->rect.inline_start) << test_case.name;
    EXPECT_FLOAT_EQ(ruby[0]->rect.inline_size, plain[0]->rect.inline_size) << test_case.name;
  }
}

// <rt> の letter-spacing は適用しない（#16 の決定 3 / ARCHITECTURE.md §3.8）。
// 和文ルビで親文字の字間がルビ側にも掛かると、ルビが親文字より広がって不自然になる。
TEST(LayoutRuby, LetterSpacingInsideRtIsNotApplied) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 10; };
  const auto root = build({block({ruby({text("東京"), rt("とうきょう", spaced)}), text("次")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // ルビ 5 文字 × 8 = 40（字間 10 が掛かれば 90 になる）
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{0, 8, 16, 24, 32}));
  // 組の送りも 40 のまま（親文字 32 より長い側が決める）
  const std::vector<float> base = base_glyph_positions(line);
  ASSERT_EQ(base.size(), 3U);
  EXPECT_FLOAT_EQ(base[2], 40);
}

// 組み合わせの回帰テストの 1 ケース。親文字は全角だけにしてあるので、送りは
// 「文字数 × (16 + 字間)」で手計算できる（横書きと縦書きで同じ値になる）。
struct SpacingCase {
  std::string_view name;
  std::vector<std::string> base;  // 1 要素 = 全角 1 文字
  std::string annotation;
  std::size_t annotation_chars = 0;
  bool split_span = false;  // 2 文字目を色だけの span で包む
};

std::vector<SpacingCase> spacing_cases() {
  return {
      {.name = "single-base", .base = {"東"}, .annotation = "とう", .annotation_chars = 2},
      {.name = "multi-base", .base = {"東", "京", "都"}, .annotation = "と", .annotation_chars = 1},
      {.name = "span-in-base",
       .base = {"東", "京", "都"},
       .annotation = "と",
       .annotation_chars = 1,
       .split_span = true},
      {.name = "longer-ruby", .base = {"東"}, .annotation = "とうきょう", .annotation_chars = 5},
  };
}

std::vector<Tree> ruby_children(const SpacingCase& test_case, float spacing) {
  const auto red = [](ComputedStyle& style) { style.color = Color{255, 0, 0, 255}; };
  std::vector<Tree> parts;
  for (std::size_t i = 0; i < test_case.base.size(); ++i) {
    if (test_case.split_span && i == 1) {
      parts.push_back(inline_box({text(test_case.base[i])}, red));
    } else {
      parts.push_back(text(test_case.base[i]));
    }
  }
  parts.push_back(rt(test_case.annotation));
  return {block({ruby(std::move(parts))},
                [spacing](ComputedStyle& style) { style.letter_spacing = spacing; })};
}

void check_letter_spacing(const SpacingCase& test_case, bool vertical, float spacing) {
  const std::string label = std::string(test_case.name) +
                            " vertical=" + std::to_string(static_cast<int>(vertical)) +
                            " spacing=" + std::to_string(spacing);
  FakeMeasurer measurer;
  std::vector<Tree> children = ruby_children(test_case, spacing);
  const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
  const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                             : run_layout(root, make_options(400), measurer);
  ASSERT_TRUE(tree.has_value()) << label;
  const LineBox& line = *all_lines(*tree)[0];

  const float step = kBase + spacing;
  const float base_width = static_cast<float>(test_case.base.size()) * step;
  const float rt_width = static_cast<float>(test_case.annotation_chars) * kRuby;
  const float advance = std::max(base_width, rt_width);
  const float offset = (advance - base_width) / 2;  // 短い方を中央に置く

  std::vector<float> expected_base;
  for (std::size_t i = 0; i < test_case.base.size(); ++i) {
    expected_base.push_back(offset + (static_cast<float>(i) * step));
  }
  std::vector<float> expected_ruby;
  for (std::size_t i = 0; i < test_case.annotation_chars; ++i) {
    expected_ruby.push_back(((advance - rt_width) / 2) + (static_cast<float>(i) * kRuby));
  }
  EXPECT_EQ(base_glyph_positions(line), expected_base) << label;
  EXPECT_EQ(ruby_glyph_positions(line), expected_ruby) << label;
  // ルビの中心と、配置後の親文字（送り基準）の中心が一致する
  EXPECT_FLOAT_EQ(expected_ruby.front() + (rt_width / 2), offset + (base_width / 2)) << label;
}

// 組み合わせの回帰テスト: {横書き, 縦書き} × {字間 0, 正, 負} ×
// {親文字 1 文字, 複数文字, 装飾 span 入り, ルビの方が長い}。
TEST(LayoutRuby, LetterSpacingCombinations) {
  for (const SpacingCase& test_case : spacing_cases()) {
    for (const bool vertical : {false, true}) {
      for (const float spacing : {0.0F, 8.0F, -4.0F}) {
        check_letter_spacing(test_case, vertical, spacing);
      }
    }
  }
}

// ---- flex / 固有寸法 ----------------------------------------------------------------

TEST(LayoutRuby, WorksInsideAFlexItem) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({ruby({text("東"), rt("とうきょう")})})}, [](ComputedStyle& style) {
        style.flex_direction = style::FlexDirection::Column;
        style.align_items = style::AlignItems::FlexStart;
      })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 1U);
  // 固有寸法は組の送り = max(16, 40)
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 40);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東とうきょう"}));
}

// <ruby> が flex コンテナの直接の子でも、テキストと同じ無名アイテムにまとまる。
TEST(LayoutRuby, RubyIsGroupedIntoTheAnonymousFlexItem) {
  FakeMeasurer measurer;
  const auto root = build({flex({text("あ"), ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 1U);
  EXPECT_EQ(items[0].tag, "#anonymous");
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あ漢かん"}));
}

// ---- エラーになる構造 ---------------------------------------------------------------

TEST(LayoutRuby, RtWithoutBaseIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({rt("かん"), text("漢")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("base text"), std::string::npos);
  EXPECT_TRUE(tree.error().location.has_value());
}

TEST(LayoutRuby, ElementInsideRtIsRejected) {
  FakeMeasurer measurer;
  Tree annotation = element("rt", Display::Inline, {inline_box({text("かん")})});
  const auto root = build({block({ruby({text("漢"), std::move(annotation)})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("<rt> may only contain text"), std::string::npos);
}

TEST(LayoutRuby, NestedRubyIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({ruby({text("漢"), rt("かん")}), rt("ふりがな")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("inside <ruby>"), std::string::npos);
}

TEST(LayoutRuby, ImageOrBreakInsideRubyIsRejected) {
  FakeMeasurer measurer;
  const ImageLookup images = image_table({{.src = "p", .id = 0, .width = 8, .height = 8}});
  const auto with_image = build({block({ruby({img("p"), rt("かん")})})});
  const auto image_tree = run_layout(with_image, 400, measurer, images);
  ASSERT_FALSE(image_tree.has_value());
  EXPECT_EQ(image_tree.error().kind, ErrorKind::UnsupportedLayout);
  const auto with_break = build({block({ruby({text("漢"), br(), rt("かん")})})});
  const auto break_tree = run_layout(with_break, 400, measurer);
  ASSERT_FALSE(break_tree.has_value());
  EXPECT_EQ(break_tree.error().kind, ErrorKind::UnsupportedLayout);
}

TEST(LayoutRuby, RtOutsideRubyIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({rt("かん")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("only allowed inside <ruby>"), std::string::npos);
}

// 親文字の中の色だけの span も、普通のテキストと同じでシェーピングを切らない（#8 / A27）。
// 断片は色の境界で分かれ、位置は 1 回のシェーピング結果のまま。
TEST(LayoutRuby, ColorOnlySpanInTheBaseDoesNotSplitShaping) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({
      text("東"),
      inline_box({text("京")}, [](ComputedStyle& style) { style.color = Color{255, 0, 0, 255}; }),
      rt("とうきょう"),
  })})});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  // 親文字で 1 回、ルビ文字で 1 回
  EXPECT_EQ(counters.shape_calls, 2U);
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> base = base_fragments(*lines[0]);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_EQ(base[0]->color, kBlack);
  EXPECT_EQ(base[1]->color, (Color{255, 0, 0, 255}));
  // ルビの方が長い（8px × 5 = 40）ので、親文字 32px は中央に寄る
  constexpr float kOffset = ((kRuby * 5) - (kBase * 2)) / 2;
  EXPECT_FLOAT_EQ(base[0]->inline_start, kOffset);
  EXPECT_FLOAT_EQ(base[1]->inline_start, kOffset + kBase);
}

// ルビはインラインの仕組みなので、ブロック級の箱にはできない。
TEST(LayoutRuby, BlockLevelRubyIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({element("ruby", Display::Block, {text("漢")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("block-level"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::layout::test
