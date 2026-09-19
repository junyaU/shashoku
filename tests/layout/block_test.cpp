#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"

// block レイアウト（ARCHITECTURE.md §3.8「block」/ CSS 2.1 §10.3.3, §10.6, §8.3.1）。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;

TEST(LayoutBlock, RootTakesViewportInlineSize) {
  FakeMeasurer measurer;
  const auto tree = run_layout(build({}), 1200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(tree->root.tag, "#root");
  EXPECT_FLOAT_EQ(tree->root.rect.inline_start, 0);
  EXPECT_FLOAT_EQ(tree->root.rect.block_start, 0);
  EXPECT_FLOAT_EQ(tree->root.rect.inline_size, 1200);
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 0);
  EXPECT_FLOAT_EQ(tree->content_block_size(), 0);
  EXPECT_EQ(tree->writing_mode, WritingMode::HorizontalTb);
  EXPECT_FLOAT_EQ(tree->viewport_width, 1200);
  EXPECT_FALSE(tree->viewport_height.has_value());
}

// 幅は親から降りる: 利用可能幅 − margin − border − padding。
TEST(LayoutBlock, WidthDescendsThroughMarginBorderPadding) {
  FakeMeasurer measurer;
  const auto root = build({block({block({})}, [](ComputedStyle& style) {
    style.margin = {Dimension::px(5), Dimension::px(5), Dimension::px(5), Dimension::px(5)};
    style.padding = {10, 10, 10, 10};
    style.border_width = 2;
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 3U);
  // 外側: border-box は margin のぶんだけ内側に寄り、幅は 200 − 5 − 5 = 190
  EXPECT_FLOAT_EQ(rects[1].rect.inline_start, 5);
  EXPECT_FLOAT_EQ(rects[1].rect.inline_size, 190);
  EXPECT_FLOAT_EQ(rects[1].rect.block_start, 5);
  EXPECT_FLOAT_EQ(rects[1].rect.block_size, 24);  // border 2 + padding 10 の上下ぶん
  // 内側: 親の content 幅 190 − 4 − 20 = 166
  EXPECT_FLOAT_EQ(rects[2].rect.inline_start, 17);
  EXPECT_FLOAT_EQ(rects[2].rect.inline_size, 166);
  EXPECT_FLOAT_EQ(rects[2].rect.block_start, 17);
  // 高さは子から戻る: 5 + 24 + 5
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 34);
}

// DESIGN.md Phase 3 の受け入れ条件: 幅 200px に 16px の全角 13 文字 → 2 行、高さが自動で決まる。
TEST(LayoutBlock, Phase3AcceptanceTwoLinesAndAutoHeight) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいうえおかきくけこさしす")})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<std::string> lines = line_texts(*tree);
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(lines[0], "あいうえおかきくけこさし");  // 12 文字 = 192px
  EXPECT_EQ(lines[1], "す");
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 2 * fake_line_height(16));
  EXPECT_FLOAT_EQ(tree->content_block_size(), 2 * fake_line_height(16));
}

TEST(LayoutBlock, PercentWidthResolvesAgainstContainingBlock) {
  FakeMeasurer measurer;
  const auto root =
      build({block({block({}, [](ComputedStyle& style) { style.width = Dimension::percent(50); })},
                   [](ComputedStyle& style) {
                     style.width = Dimension::px(160);
                     style.padding = {0, 10, 0, 10};
                   })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 3U);
  EXPECT_FLOAT_EQ(rects[1].rect.inline_size, 180);  // content 160 + padding 20
  EXPECT_FLOAT_EQ(rects[2].rect.inline_size, 80);   // 親の content 160 の 50%
}

// CSS 2.1 §10.3.3: width が auto でないとき、左右 auto は中央寄せ。
TEST(LayoutBlock, AutoMarginsCenterTheBox) {
  FakeMeasurer measurer;
  const auto root = build({block({}, [](ComputedStyle& style) {
    style.width = Dimension::px(100);
    style.margin = {Dimension::px(0), Dimension::auto_(), Dimension::px(0), Dimension::auto_()};
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(block_rects(*tree)[1].rect.inline_start, 50);
}

TEST(LayoutBlock, SingleAutoMarginTakesTheRest) {
  FakeMeasurer measurer;
  const auto root = build({block({}, [](ComputedStyle& style) {
    style.width = Dimension::px(100);
    style.margin = {Dimension::px(0), Dimension::px(0), Dimension::px(0), Dimension::auto_()};
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(block_rects(*tree)[1].rect.inline_start, 100);
}

// 過制約（width も左右 margin も指定）なら end 側の margin を無視する。
TEST(LayoutBlock, OverConstrainedIgnoresEndMargin) {
  FakeMeasurer measurer;
  const auto root = build({block({}, [](ComputedStyle& style) {
    style.width = Dimension::px(100);
    style.margin = {Dimension::px(0), Dimension::px(20), Dimension::px(0), Dimension::px(20)};
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  // start 側の 20 は生き、end 側は 200 − 100 − 20 = 80 に読み替えられる
  EXPECT_FLOAT_EQ(block_rects(*tree)[1].rect.inline_start, 20);
  EXPECT_FLOAT_EQ(block_rects(*tree)[1].rect.inline_size, 100);
}

// height: px は固定。内容がはみ出してもクリップせず、親の高さ計算には指定値を使う。
TEST(LayoutBlock, FixedHeightDoesNotFollowContent) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいうえお")},
                                 [](ComputedStyle& style) { style.height = Dimension::px(10); })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(block_rects(*tree)[1].rect.block_size, 10);
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 10);
  // 行ボックスは内容どおりの高さで残る（クリップしない。A4）
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, fake_line_height(16));
}

// ---- 兄弟間のマージン相殺（A10 / CSS 2.1 §8.3.1）----------------------------------

std::vector<BlockRect> stack_with_margins(FakeMeasurer& measurer, float first_bottom,
                                          float second_top) {
  const auto root = build({block({},
                                 [first_bottom](ComputedStyle& style) {
                                   style.height = Dimension::px(10);
                                   style.margin.bottom = Dimension::px(first_bottom);
                                 }),
                           block({}, [second_top](ComputedStyle& style) {
                             style.height = Dimension::px(10);
                             style.margin.top = Dimension::px(second_top);
                           })});
  const auto tree = run_layout(root, 200, measurer);
  EXPECT_TRUE(tree.has_value());
  return block_rects(*tree);
}

TEST(LayoutBlock, SiblingMarginsCollapse) {
  FakeMeasurer measurer;
  // 正どうしは大きい方
  EXPECT_FLOAT_EQ(stack_with_margins(measurer, 20, 30)[2].rect.block_start, 40);
  // 正と負は足し合わせる
  EXPECT_FLOAT_EQ(stack_with_margins(measurer, 20, -10)[2].rect.block_start, 20);
  // 負どうしは小さい方（絶対値が大きい方）
  EXPECT_FLOAT_EQ(stack_with_margins(measurer, -20, -30)[2].rect.block_start, -20);
}

// 高さ 0 のブロックを挟んでも、相殺は「隣り合う 2 つずつ」で行う（貫通はしない）。
TEST(LayoutBlock, CollapseDoesNotPassThroughEmptyBlock) {
  FakeMeasurer measurer;
  const auto root =
      build({block({}, [](ComputedStyle& style) { style.margin.bottom = Dimension::px(20); }),
             block({},
                   [](ComputedStyle& style) {
                     style.margin.top = Dimension::px(10);
                     style.margin.bottom = Dimension::px(10);
                   }),
             block({}, [](ComputedStyle& style) { style.margin.top = Dimension::px(30); })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 4U);
  EXPECT_FLOAT_EQ(rects[1].rect.block_start, 0);
  EXPECT_FLOAT_EQ(rects[2].rect.block_start, 20);  // max(20, 10)
  EXPECT_FLOAT_EQ(rects[3].rect.block_start, 50);  // 20 + max(10, 30)
}

// 親子間は相殺しない（A10）。
TEST(LayoutBlock, ParentAndChildMarginsDoNotCollapse) {
  FakeMeasurer measurer;
  const auto root =
      build({block({block({}, [](ComputedStyle& style) { style.margin.top = Dimension::px(20); })},
                   [](ComputedStyle& style) { style.margin.top = Dimension::px(10); })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  EXPECT_FLOAT_EQ(rects[1].rect.block_start, 10);
  EXPECT_FLOAT_EQ(rects[2].rect.block_start, 30);
  EXPECT_FLOAT_EQ(rects[1].rect.block_size, 20);  // 子の margin は親の高さに入る
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 30);
}

// ---- 無名ブロック -----------------------------------------------------------------

TEST(LayoutBlock, InlineRunsAreWrappedInAnonymousBlocks) {
  FakeMeasurer measurer;
  const auto root = build({text("あ"), block({text("い")}), text("う")});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 4U);
  EXPECT_EQ(rects[1].tag, "#anonymous");
  EXPECT_EQ(rects[2].tag, "div");
  EXPECT_EQ(rects[3].tag, "#anonymous");
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あ", "い", "う"}));
  const float line = fake_line_height(16);
  EXPECT_FLOAT_EQ(rects[3].rect.block_start, 2 * line);
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 3 * line);
}

// 空白だけのインラインの連続は無名ブロックを作らない（CSS 2.1 §9.2.1.1）。
TEST(LayoutBlock, BlankInlineRunsProduceNoAnonymousBlock) {
  FakeMeasurer measurer;
  const auto root = build({text("\n  "), block({text("い")}), text("  \n"), inline_box({text(" ")}),
                           block({text("う")})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 3U);
  EXPECT_EQ(rects[1].tag, "div");
  EXPECT_EQ(rects[2].tag, "div");
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 2 * fake_line_height(16));
}

// ブロックの子がインラインだけなら、そのブロック自身が行ボックスを持つ。
TEST(LayoutBlock, BlockWithOnlyInlineChildrenHoldsLinesDirectly) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), inline_box({text("い")})})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const BlockBox* div = find_block(*tree, "div");
  ASSERT_NE(div, nullptr);
  ASSERT_NE(div->lines(), nullptr);
  EXPECT_EQ(div->lines()->size(), 1U);
}

TEST(LayoutBlock, EmptyBlockHasNoLineBoxes) {
  FakeMeasurer measurer;
  const auto root = build({block({}), block({text("   ")})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_TRUE(all_lines(*tree).empty());
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 0);
}

// 塗り情報が border-box といっしょに載る（paint が読む）。
TEST(LayoutBlock, DecorationIsCarriedOnTheBox) {
  FakeMeasurer measurer;
  const auto root = build({block({}, [](ComputedStyle& style) {
    style.background_color = Color{10, 20, 30, 255};
    style.border_width = 3;
    style.border_color = Color{1, 2, 3, 128};
    style.border_radius = 4;
    style.height = Dimension::px(10);
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const BlockBox* div = find_block(*tree, "div");
  ASSERT_NE(div, nullptr);
  EXPECT_EQ(div->decoration.background_color, (Color{10, 20, 30, 255}));
  EXPECT_FLOAT_EQ(div->decoration.border_width, 3);
  EXPECT_EQ(div->decoration.border_color, (Color{1, 2, 3, 128}));
  EXPECT_FLOAT_EQ(div->decoration.border_radius, 4);
  EXPECT_FALSE(div->decoration.invisible());
  EXPECT_FLOAT_EQ(div->rect.inline_size, 200);
  EXPECT_FLOAT_EQ(div->content_rect().inline_size, 194);
  EXPECT_FLOAT_EQ(div->content_rect().block_size, 10);
}

// 無名ブロックは親の塗りを繰り返さない。
TEST(LayoutBlock, AnonymousBlockHasNoDecoration) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), block({})}, [](ComputedStyle& style) {
    style.background_color = Color{10, 20, 30, 255};
  })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const BlockBox* anonymous = find_block(*tree, "#anonymous");
  ASSERT_NE(anonymous, nullptr);
  EXPECT_TRUE(anonymous->decoration.invisible());
  EXPECT_EQ(anonymous->decoration.background_color, kTransparent);
}

}  // namespace
}  // namespace shashoku::layout::test
