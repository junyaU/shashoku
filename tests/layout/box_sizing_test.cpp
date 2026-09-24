#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/geometry.hpp"
#include "layout/test_support.hpp"

// box-sizing（ARCHITECTURE.md A56 / §3.8。CSS Box Sizing 3 §3）。
//
// `border-box` のとき `width` / `height` / `flex-basis` は **border box** の寸法になり、
// content は「指定値 − padding（その軸の 2 辺）− border x 2」（下限 0）。`%` は先に解決する。
// `content-box`（既定）の数字は今までどおりで、既存のテスト群がそれを固定している。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;

// layout にも `BoxSizing`（使用値の構造体）があるので、style 側の enum には別名を付ける。
using SizingMode = style::BoxSizing;

constexpr SizingMode kContent = SizingMode::ContentBox;
constexpr SizingMode kBorder = SizingMode::BorderBox;

// ---- block の width / height -------------------------------------------------

struct BlockCase {
  std::string_view name;
  SizingMode sizing = kContent;
  Dimension width;
  Dimension height;
  Edges<float> padding;  // 上右下左
  float border = 0;
  float outer_inline = 0;    // 箱の border-box の inline サイズ
  float outer_block = 0;     // 同 block サイズ
  float content_inline = 0;  // 中の `width: auto` の子の幅 = 親の content 幅
};

// 幅 400 の紙面に「箱 + その中の width: auto の子」を 1 つ組む。
void run_block_case(const BlockCase& test, bool vertical) {
  FakeMeasurer measurer;
  const StyleFn style = [&test](ComputedStyle& s) {
    s.box_sizing = test.sizing;
    s.width = test.width;
    s.height = test.height;
    s.padding = test.padding;
    s.border_width = test.border;
  };
  const style::StyledNode root =
      vertical ? build_vertical({block({block({})}, style)}) : build({block({block({})}, style)});
  const Result<BoxTree> tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                                        : run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value()) << (tree ? "" : tree.error().message);
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 3U);
  EXPECT_FLOAT_EQ(rects[1].rect.inline_size, test.outer_inline);
  EXPECT_FLOAT_EQ(rects[1].rect.block_size, test.outer_block);
  EXPECT_FLOAT_EQ(rects[2].rect.inline_size, test.content_inline);
}

TEST(LayoutBoxSizing, BlockWidthAndHeight) {
  const std::vector<BlockCase> cases{
      // 既定（content-box）: 指定値がそのまま content になり、箱は padding と border のぶん外へ
      {.name = "content-box: 指定値は content",
       .sizing = kContent,
       .width = Dimension::px(200),
       .height = Dimension::px(100),
       .padding = {20, 20, 20, 20},
       .border = 2,
       .outer_inline = 244,
       .outer_block = 144,
       .content_inline = 200},
      // border-box: 指定値が箱の外寸。content = 200 − 20x2 − 2x2 = 156 / 100 − 40 − 4 = 56
      {.name = "border-box: 指定値は border box",
       .sizing = kBorder,
       .width = Dimension::px(200),
       .height = Dimension::px(100),
       .padding = {20, 20, 20, 20},
       .border = 2,
       .outer_inline = 200,
       .outer_block = 100,
       .content_inline = 156},
      // padding も border も無ければ 2 つは同じ
      {.name = "border-box: padding も border も無ければ content-box と同じ",
       .sizing = kBorder,
       .width = Dimension::px(200),
       .height = Dimension::px(100),
       .padding = {},
       .border = 0,
       .outer_inline = 200,
       .outer_block = 100,
       .content_inline = 200},
      // 辺ごとに違う padding: inline は左右、block は上下だけを引く
      {.name = "border-box: 辺ごとに違う padding",
       .sizing = kBorder,
       .width = Dimension::px(200),
       .height = Dimension::px(100),
       .padding = {5, 10, 15, 20},
       .border = 1,
       .outer_inline = 200,
       .outer_block = 100,
       .content_inline = 168},  // 200 − (20 + 10) − 2
      // `%` は先に解決してから引く（50% of 400 = 200 → content 180）
      {.name = "border-box: % の width",
       .sizing = kBorder,
       .width = Dimension::percent(50),
       .height = Dimension::auto_(),
       .padding = {10, 10, 10, 10},
       .border = 0,
       .outer_inline = 200,
       .outer_block = 20,  // 中身の高さ 0 + padding 上下
       .content_inline = 180},
      // 引ききれないときは content が 0 で止まる（箱は指定値より大きくなる。仕様どおり）
      {.name = "border-box: padding が指定値より大きい",
       .sizing = kBorder,
       .width = Dimension::px(10),
       .height = Dimension::auto_(),
       .padding = {20, 20, 20, 20},
       .border = 0,
       .outer_inline = 40,
       .outer_block = 40,
       .content_inline = 0},
  };
  for (const BlockCase& test : cases) {
    SCOPED_TRACE(test.name);
    run_block_case(test, false);
  }
}

// 縦書きでも式は論理軸で同じ（A1 / A56）。`height` が inline、`width` が block になり、
// 引く padding の辺も入れ替わる（inline = 上下、block = 右左）。
TEST(LayoutBoxSizing, VerticalUsesTheLogicalAxes) {
  const std::vector<BlockCase> cases{
      // padding 上 5 / 右 10 / 下 15 / 左 20、border 1
      //   inline（= height 200）は 上 5 + 下 15 + 2 → content 178
      //   block （= width 300）は 右 10 + 左 20 + 2 → content 268
      {.name = "vertical-rl border-box",
       .sizing = kBorder,
       .width = Dimension::px(300),
       .height = Dimension::px(200),
       .padding = {5, 10, 15, 20},
       .border = 1,
       .outer_inline = 200,
       .outer_block = 300,
       .content_inline = 178},
      {.name = "vertical-rl content-box",
       .sizing = kContent,
       .width = Dimension::px(300),
       .height = Dimension::px(200),
       .padding = {5, 10, 15, 20},
       .border = 1,
       .outer_inline = 222,
       .outer_block = 332,
       .content_inline = 200},
  };
  for (const BlockCase& test : cases) {
    SCOPED_TRACE(test.name);
    run_block_case(test, true);
  }
}

// ---- flex ---------------------------------------------------------------------

// flex コンテナ（ルートの最初の子）の 1 つ目のアイテムの矩形。
LogicalRect first_item(const BoxTree& tree) {
  const std::vector<BlockBox>* containers = tree.root.blocks();
  if (containers == nullptr || containers->empty()) {
    return {};
  }
  const std::vector<BlockBox>* items = containers->front().blocks();
  if (items == nullptr || items->empty()) {
    return {};
  }
  return items->front().rect;
}

struct FlexCase {
  std::string_view name;
  SizingMode sizing = kContent;
  bool column = false;
  Dimension width;        // アイテムの width
  Dimension height;       // アイテムの height
  Dimension flex_basis;   // auto なら width / height を見る
  float item_inline = 0;  // アイテムの border-box の inline サイズ
  float item_block = 0;   // 同 block サイズ
};

// 幅 400 x 高さ 300 の flex コンテナに、`flex: none` のアイテムを 1 つ置く。
// アイテムの padding は 10px 4 辺、border は 2px。
void run_flex_case(const FlexCase& test) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({},
                                       [&test](ComputedStyle& s) {
                                         s.box_sizing = test.sizing;
                                         s.width = test.width;
                                         s.height = test.height;
                                         s.flex_basis = test.flex_basis;
                                         s.padding = {10, 10, 10, 10};
                                         s.border_width = 2;
                                         s.flex_grow = 0;
                                         s.flex_shrink = 0;
                                       })},
                                [&test](ComputedStyle& s) {
                                  s.flex_direction = test.column ? style::FlexDirection::Column
                                                                 : style::FlexDirection::Row;
                                  s.align_items = style::AlignItems::FlexStart;
                                  s.height = Dimension::px(300);
                                })});
  const Result<BoxTree> tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value()) << (tree ? "" : tree.error().message);
  const LogicalRect rect = first_item(*tree);
  EXPECT_FLOAT_EQ(rect.inline_size, test.item_inline);
  EXPECT_FLOAT_EQ(rect.block_size, test.item_block);
}

TEST(LayoutBoxSizing, FlexItemMainAndCrossSizes) {
  const std::vector<FlexCase> cases{
      // row: width が主軸、height が交差軸。padding 10x2 + border 2x2 = 24 を引く
      {.name = "row content-box",
       .sizing = kContent,
       .column = false,
       .width = Dimension::px(100),
       .height = Dimension::px(50),
       .flex_basis = Dimension::auto_(),
       .item_inline = 124,
       .item_block = 74},
      {.name = "row border-box",
       .sizing = kBorder,
       .column = false,
       .width = Dimension::px(100),
       .height = Dimension::px(50),
       .flex_basis = Dimension::auto_(),
       .item_inline = 100,
       .item_block = 50},
      // flex-basis も box-sizing の影響を受ける（CSS Flexbox 1 §7.2.3）
      {.name = "row border-box flex-basis",
       .sizing = kBorder,
       .column = false,
       .width = Dimension::auto_(),
       .height = Dimension::px(50),
       .flex_basis = Dimension::px(120),
       .item_inline = 120,
       .item_block = 50},
      {.name = "row content-box flex-basis",
       .sizing = kContent,
       .column = false,
       .width = Dimension::auto_(),
       .height = Dimension::px(50),
       .flex_basis = Dimension::px(120),
       .item_inline = 144,
       .item_block = 74},
      // column: height が主軸、width が交差軸
      {.name = "column content-box",
       .sizing = kContent,
       .column = true,
       .width = Dimension::px(100),
       .height = Dimension::px(50),
       .flex_basis = Dimension::auto_(),
       .item_inline = 124,
       .item_block = 74},
      {.name = "column border-box",
       .sizing = kBorder,
       .column = true,
       .width = Dimension::px(100),
       .height = Dimension::px(50),
       .flex_basis = Dimension::auto_(),
       .item_inline = 100,
       .item_block = 50},
      {.name = "column border-box flex-basis",
       .sizing = kBorder,
       .column = true,
       .width = Dimension::px(100),
       .height = Dimension::auto_(),
       .flex_basis = Dimension::px(80),
       .item_inline = 100,
       .item_block = 80},
  };
  for (const FlexCase& test : cases) {
    SCOPED_TRACE(test.name);
    run_flex_case(test);
  }
}

// 交差軸の stretch は content-box と同じ（border-box は「指定された寸法」にだけ効く）。
TEST(LayoutBoxSizing, StretchIsUnaffected) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({},
                                       [](ComputedStyle& s) {
                                         s.box_sizing = SizingMode::BorderBox;
                                         s.padding = {10, 10, 10, 10};
                                         s.border_width = 2;
                                       })},
                                [](ComputedStyle& s) {
                                  s.align_items = style::AlignItems::Stretch;
                                  s.height = Dimension::px(120);
                                })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(first_item(*tree).block_size, 120);  // 交差軸いっぱい
}

// ---- 固有寸法（shrink-to-fit と min-content / max-content）----------------------

// `width` を書いた子の固有寸法は「content + padding + border + margin」なので、
// border-box では**指定値 + margin** がそのまま外寸になる。
TEST(LayoutBoxSizing, IntrinsicWidthUsesTheBorderBoxValue) {
  FakeMeasurer measurer;
  // column の flex コンテナ（幅 auto）の交差軸は shrink-to-fit = 子の max-content
  const auto root = build({flex({block({},
                                       [](ComputedStyle& s) {
                                         s.box_sizing = SizingMode::BorderBox;
                                         s.width = Dimension::px(150);
                                         s.padding = {8, 8, 8, 8};
                                         s.border_width = 1;
                                       })},
                                [](ComputedStyle& s) {
                                  s.flex_direction = style::FlexDirection::Column;
                                  s.align_items = style::AlignItems::FlexStart;
                                  s.width = Dimension::px(300);
                                })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  // align-items: flex-start なので交差軸は固有寸法どおり = 150（= 指定した border box）
  EXPECT_FLOAT_EQ(first_item(*tree).inline_size, 150);
}

// ---- <img> ---------------------------------------------------------------------

// 固有寸法 80 x 40 の画像。
ImageLookup photo() { return image_table({{.src = "photo", .id = 3, .width = 80, .height = 40}}); }

// <img> の content_rect（画像そのものが占める矩形）。
LogicalRect image_content(FakeMeasurer& measurer, Tree image) {
  const auto root = build({block({std::move(image)})});
  const auto tree = run_layout(root, 400, measurer, photo());
  EXPECT_TRUE(tree.has_value());
  if (!tree) {
    return {};
  }
  const std::vector<const ImageFragment*> images = all_images(*tree);
  EXPECT_EQ(images.size(), 1U);
  return images.empty() ? LogicalRect{} : images.front()->content_rect;
}

// CSS の width / height と width / height 属性は同じプロパティなので、同じ扱い（A56）。
TEST(LayoutBoxSizing, ImageCssAndAttributeSizes) {
  FakeMeasurer measurer;
  const StyleFn border_box = [](ComputedStyle& s) {
    s.box_sizing = SizingMode::BorderBox;
    s.padding = {10, 10, 10, 10};
    s.border_width = 2;
  };
  // CSS の width: 100px（border box）→ content 100 − 20 − 4 = 76、縦横比 2:1 で高さ 38
  const LogicalRect css = image_content(
      measurer, img("photo", std::nullopt, std::nullopt, [&border_box](ComputedStyle& s) {
        border_box(s);
        s.width = Dimension::px(100);
      }));
  EXPECT_FLOAT_EQ(css.inline_size, 76);
  EXPECT_FLOAT_EQ(css.block_size, 38);

  // width 属性も同じ
  const LogicalRect attr = image_content(measurer, img("photo", 100, std::nullopt, border_box));
  EXPECT_FLOAT_EQ(attr.inline_size, 76);
  EXPECT_FLOAT_EQ(attr.block_size, 38);

  // height 属性も同じ（40 − 20 − 4 = 16、比で幅 32）
  const LogicalRect height_attr =
      image_content(measurer, img("photo", std::nullopt, 40, border_box));
  EXPECT_FLOAT_EQ(height_attr.inline_size, 32);
  EXPECT_FLOAT_EQ(height_attr.block_size, 16);

  // content-box は今までどおり（引き算なし）
  const LogicalRect content =
      image_content(measurer, img("photo", std::nullopt, std::nullopt, [](ComputedStyle& s) {
                      s.padding = {10, 10, 10, 10};
                      s.border_width = 2;
                      s.width = Dimension::px(100);
                    }));
  EXPECT_FLOAT_EQ(content.inline_size, 100);
  EXPECT_FLOAT_EQ(content.block_size, 50);
}

}  // namespace
}  // namespace shashoku::layout::test
