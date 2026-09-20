#include <string>
#include <vector>

#include <gtest/gtest.h>

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
