#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/engine.hpp"  // layout_without_memo（A29 のメモで出力が変わらないことの検査）
#include "layout/test_support.hpp"

// <img>（ARCHITECTURE.md §3.8 / A12）。
namespace shashoku::layout::test {
namespace {

using style::AlignItems;
using style::ComputedStyle;
using style::Dimension;
using style::Display;

// 固有寸法 80 × 40（縦横比 2:1）の画像 1 枚。
ImageLookup photos() { return image_table({{.src = "photo", .id = 7, .width = 80, .height = 40}}); }

// インライン <img> 1 枚を流して、その content_rect を返す。
LogicalRect resolved_size(FakeMeasurer& measurer, Tree image, float viewport = 200) {
  const auto root = build({block({std::move(image)})});
  const auto tree = run_layout(root, viewport, measurer, photos());
  EXPECT_TRUE(tree.has_value());
  if (!tree) {
    return {};
  }
  const std::vector<const ImageFragment*> images = all_images(*tree);
  EXPECT_EQ(images.size(), 1U);
  return images.empty() ? LogicalRect{} : images.front()->content_rect;
}

// ---- サイズの解決: CSS > 属性 > 固有寸法 --------------------------------------------

TEST(LayoutImage, IntrinsicSize) {
  FakeMeasurer measurer;
  const LogicalRect rect = resolved_size(measurer, img("photo"));
  EXPECT_FLOAT_EQ(rect.inline_size, 80);
  EXPECT_FLOAT_EQ(rect.block_size, 40);
}

TEST(LayoutImage, CssWidthKeepsTheAspectRatio) {
  FakeMeasurer measurer;
  const LogicalRect rect =
      resolved_size(measurer, img("photo", std::nullopt, std::nullopt,
                                  [](ComputedStyle& style) { style.width = Dimension::px(40); }));
  EXPECT_FLOAT_EQ(rect.inline_size, 40);
  EXPECT_FLOAT_EQ(rect.block_size, 20);
}

TEST(LayoutImage, CssHeightKeepsTheAspectRatio) {
  FakeMeasurer measurer;
  const LogicalRect rect =
      resolved_size(measurer, img("photo", std::nullopt, std::nullopt,
                                  [](ComputedStyle& style) { style.height = Dimension::px(80); }));
  EXPECT_FLOAT_EQ(rect.inline_size, 160);
  EXPECT_FLOAT_EQ(rect.block_size, 80);
}

TEST(LayoutImage, CssWidthAndHeightAreUsedAsIs) {
  FakeMeasurer measurer;
  const LogicalRect rect =
      resolved_size(measurer, img("photo", std::nullopt, std::nullopt, [](ComputedStyle& style) {
                      style.width = Dimension::px(30);
                      style.height = Dimension::px(10);
                    }));
  EXPECT_FLOAT_EQ(rect.inline_size, 30);
  EXPECT_FLOAT_EQ(rect.block_size, 10);
}

TEST(LayoutImage, AttributesAreUsedWhenCssIsAuto) {
  FakeMeasurer measurer;
  const LogicalRect width_only = resolved_size(measurer, img("photo", 40));
  EXPECT_FLOAT_EQ(width_only.inline_size, 40);
  EXPECT_FLOAT_EQ(width_only.block_size, 20);
  const LogicalRect height_only = resolved_size(measurer, img("photo", std::nullopt, 80));
  EXPECT_FLOAT_EQ(height_only.inline_size, 160);
  EXPECT_FLOAT_EQ(height_only.block_size, 80);
  const LogicalRect both = resolved_size(measurer, img("photo", 30, 10));
  EXPECT_FLOAT_EQ(both.inline_size, 30);
  EXPECT_FLOAT_EQ(both.block_size, 10);
}

TEST(LayoutImage, CssBeatsAttributes) {
  FakeMeasurer measurer;
  // width だけ CSS で上書き: height は属性が残るので縦横比は崩れる（ブラウザと同じ）
  const LogicalRect both = resolved_size(measurer, img("photo", 200, 100, [](ComputedStyle& style) {
                                           style.width = Dimension::px(40);
                                         }));
  EXPECT_FLOAT_EQ(both.inline_size, 40);
  EXPECT_FLOAT_EQ(both.block_size, 100);
  // height 属性が無ければ、CSS の width から縦横比で高さを出す
  const LogicalRect width_only =
      resolved_size(measurer, img("photo", 200, std::nullopt,
                                  [](ComputedStyle& style) { style.width = Dimension::px(40); }));
  EXPECT_FLOAT_EQ(width_only.inline_size, 40);
  EXPECT_FLOAT_EQ(width_only.block_size, 20);
}

TEST(LayoutImage, PercentWidthResolvesAgainstTheContainingBlock) {
  FakeMeasurer measurer;
  const auto root =
      build({block({img("photo", std::nullopt, std::nullopt,
                        [](ComputedStyle& style) { style.width = Dimension::percent(50); })},
                   [](ComputedStyle& style) { style.width = Dimension::px(200); })});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<const ImageFragment*> images = all_images(*tree);
  ASSERT_EQ(images.size(), 1U);
  EXPECT_FLOAT_EQ(images[0]->content_rect.inline_size, 100);
  EXPECT_FLOAT_EQ(images[0]->content_rect.block_size, 50);
}

// ---- インラインの <img> --------------------------------------------------------------

// CSS の既定の vertical-align: margin-box の下端がベースラインに乗る。
TEST(LayoutImage, InlineImageSitsOnTheBaseline) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), img("photo")})});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  // 行はベースラインより上に画像の高さぶん広がり、下は支柱の descent のまま
  EXPECT_FLOAT_EQ(lines[0]->baseline, 40);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, 40 + fake_descent(16));
  const std::vector<const ImageFragment*> images = image_fragments(*lines[0]);
  ASSERT_EQ(images.size(), 1U);
  EXPECT_EQ(images[0]->image, 7U);
  EXPECT_FLOAT_EQ(images[0]->rect.inline_start, 16);  // 「あ」の後ろ
  EXPECT_FLOAT_EQ(images[0]->rect.block_start, 0);
  EXPECT_FLOAT_EQ(images[0]->rect.block_end(), lines[0]->baseline);
  // 文字は同じベースラインに乗る
  ASSERT_EQ(text_fragments(*lines[0]).size(), 1U);
  EXPECT_FLOAT_EQ(text_fragments(*lines[0])[0]->baseline, 40);
}

TEST(LayoutImage, InlineImageBoxPropertiesShiftTheFragment) {
  FakeMeasurer measurer;
  const auto root =
      build({block({img("photo", std::nullopt, std::nullopt, [](ComputedStyle& style) {
        style.margin = {Dimension::px(5), Dimension::px(5), Dimension::px(5), Dimension::px(5)};
        style.padding = {3, 3, 3, 3};
        style.border_width = 2;
      })})});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  // margin-box は 100 × 60。行はその高さぶん上に広がる
  EXPECT_FLOAT_EQ(lines[0]->baseline, 60);
  const std::vector<const ImageFragment*> images = image_fragments(*lines[0]);
  ASSERT_EQ(images.size(), 1U);
  EXPECT_FLOAT_EQ(images[0]->rect.inline_start, 5);
  EXPECT_FLOAT_EQ(images[0]->rect.block_start, 5);
  EXPECT_FLOAT_EQ(images[0]->rect.inline_size, 90);  // 80 + padding 6 + border 4
  EXPECT_FLOAT_EQ(images[0]->rect.block_size, 50);
  EXPECT_FLOAT_EQ(images[0]->content_rect.inline_start, 10);
  EXPECT_FLOAT_EQ(images[0]->content_rect.block_start, 10);
  EXPECT_FLOAT_EQ(images[0]->content_rect.inline_size, 80);
  EXPECT_FLOAT_EQ(images[0]->content_rect.block_size, 40);
}

// 行分割器から見た <img> は分割不能な箱（ItemKind::Atomic）。
TEST(LayoutImage, ImageWrapsAsAnAtomicItem) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいう"), img("photo")})});
  const auto tree = run_layout(root, 100, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あいう", ""}));
  EXPECT_TRUE(image_fragments(*lines[0]).empty());
  ASSERT_EQ(image_fragments(*lines[1]).size(), 1U);
  EXPECT_FLOAT_EQ(image_fragments(*lines[1])[0]->rect.inline_start, 0);
}

// ---- display: block の <img> ---------------------------------------------------------

// 画像だけの行には支柱を入れない = 画像の下にディセンダぶんのすき間ができない。
TEST(LayoutImage, BlockImageHasNoDescenderGap) {
  FakeMeasurer measurer;
  const auto root = build({img("photo", std::nullopt, std::nullopt,
                               [](ComputedStyle& style) { style.display = Display::Block; })});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const BlockBox* box = find_block(*tree, "img");
  ASSERT_NE(box, nullptr);
  EXPECT_FLOAT_EQ(box->rect.inline_size, 80);
  EXPECT_FLOAT_EQ(box->rect.block_size, 40);
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 40);
  // 塗りは ImageFragment 側が持つ（ブロックと二重に描かないため）
  EXPECT_TRUE(box->decoration.invisible());
  ASSERT_NE(box->lines(), nullptr);
  ASSERT_EQ(box->lines()->size(), 1U);
  EXPECT_FLOAT_EQ(box->lines()->front().rect.block_size, 40);
}

TEST(LayoutImage, BlockImageIsCenteredByAutoMargins) {
  FakeMeasurer measurer;
  const auto root = build({img("photo", std::nullopt, std::nullopt, [](ComputedStyle& style) {
    style.display = Display::Block;
    style.margin = {Dimension::px(0), Dimension::auto_(), Dimension::px(0), Dimension::auto_()};
  })});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const BlockBox* box = find_block(*tree, "img");
  ASSERT_NE(box, nullptr);
  EXPECT_FLOAT_EQ(box->rect.inline_start, 160);
  ASSERT_EQ(all_images(*tree).size(), 1U);
  EXPECT_FLOAT_EQ(all_images(*tree)[0]->rect.inline_start, 160);
}

TEST(LayoutImage, DecorationIsCarriedOnTheFragment) {
  FakeMeasurer measurer;
  const auto root =
      build({block({img("photo", std::nullopt, std::nullopt, [](ComputedStyle& style) {
        style.background_color = Color{1, 2, 3, 255};
        style.border_width = 2;
        style.border_color = Color{4, 5, 6, 255};
        style.border_radius = 8;
      })})});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<const ImageFragment*> images = all_images(*tree);
  ASSERT_EQ(images.size(), 1U);
  EXPECT_EQ(images[0]->decoration.background_color, (Color{1, 2, 3, 255}));
  EXPECT_FLOAT_EQ(images[0]->decoration.border_width, 2);
  EXPECT_EQ(images[0]->decoration.border_color, (Color{4, 5, 6, 255}));
  EXPECT_FLOAT_EQ(images[0]->decoration.border_radius, 8);
}

// ---- flex アイテムとしての <img> -------------------------------------------------------

// stretch でも画像は歪めない（アスペクト比を保つ = flex-start 扱い）。
TEST(LayoutImage, FlexItemImageIsNotStretched) {
  FakeMeasurer measurer;
  const auto root = build({flex({img("photo")}, [](ComputedStyle& style) {
    style.height = Dimension::px(200);
    style.align_items = AlignItems::Stretch;
  })});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 1U);
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 80);
  EXPECT_FLOAT_EQ(items[0].rect.block_size, 40);
  ASSERT_EQ(all_images(*tree).size(), 1U);
  EXPECT_FLOAT_EQ(all_images(*tree)[0]->content_rect.block_size, 40);
}

// A29: 準備済み段落（PreparedParagraph）は固有寸法の計測と配置で共有するが、**<img> を
// 含む段落だけは共有しない**。`%` の幅は content_inline_size を基準にするので、
// 計測のときの幅（= 包含ブロックの幅）で作った段落を配置に使い回すと、画像の大きさが
// 変わってしまう。shrink-to-fit（align-items: flex-start）だと 2 つの幅が食い違う。
TEST(LayoutImage, PercentImageIsResolvedAgainstThePlacementWidth) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({text("あ"), img("photo", std::nullopt, std::nullopt,
                                                        [](ComputedStyle& style) {
                                                          style.width = Dimension::percent(25);
                                                        })})},
                                [](ComputedStyle& style) {
                                  style.flex_direction = style::FlexDirection::Column;
                                  style.align_items = AlignItems::FlexStart;  // 幅は shrink-to-fit
                                })});
  const auto tree = run_layout(root, 400, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<const ImageFragment*> images = all_images(*tree);
  ASSERT_EQ(images.size(), 1U);
  // アイテムの幅（shrink-to-fit）に対する 25%。包含ブロックの 400 に対する 25% = 100 ではない
  const float item_width = tree->root.blocks()->front().blocks()->front().rect.inline_size;
  EXPECT_FLOAT_EQ(images[0]->content_rect.inline_size, item_width / 4);
  EXPECT_LT(images[0]->content_rect.inline_size, 100);

  // メモの有無で結果が変わらない（段落を共有してしまうとここで落ちる）
  FakeMeasurer plain;
  const auto without_memo = layout_without_memo(root, make_options(400), plain, photos());
  ASSERT_TRUE(without_memo.has_value());
  EXPECT_EQ(dump_json(*tree), dump_json(*without_memo));
}

// 画像は自動最小サイズによって内容サイズより縮まない。
TEST(LayoutImage, FlexItemImageDoesNotShrinkBelowItsSize) {
  FakeMeasurer measurer;
  const auto root = build({flex({img("photo"), img("photo")})});
  const auto tree = run_layout(root, 100, measurer, photos());
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 2U);
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 80);
  EXPECT_FLOAT_EQ(items[1].rect.inline_size, 80);
  EXPECT_FLOAT_EQ(items[1].rect.inline_start, 80);
}

}  // namespace
}  // namespace shashoku::layout::test
