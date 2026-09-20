#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "paint/display_list_builder.hpp"
#include "paint/paint_test_support.hpp"
#include "raster/display_list.hpp"

// 論理 → 物理の変換（ARCHITECTURE.md A1 / §3.9）。縦書きは layout の第 3 段で有効になるが、
// 変換そのものは paint の仕事なので、手で組んだボックスツリーでここだけ先に固めておく。
//
//   vertical-rl: x = viewport_width − block_end、y = inline_start
//                ペン位置の x = viewport_width − baseline（行の中心軸）
namespace shashoku::paint::test {
namespace {

constexpr Color kRed{255, 0, 0, 255};
constexpr float kViewportWidth = 200;

TEST(PaintVertical, BlockRectIsTransposedFromTheRightEdge) {
  const BoxTree box_tree = tree(
      block("#root", lrect(10, 20, 50, 30), fill(kRed), std::vector<BlockBox>{}), kViewportWidth,
      WritingMode::VerticalRl);

  // block は紙面の右端からの距離。左端は block_end = 20 + 30 = 50 の側。
  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::FillRect{Rect{200 - 50, 10, 30, 50}, kRed}}));
}

// 同じ木を横書きで組むと、inline = x / block = y のまま読める（A1）。
TEST(PaintVertical, HorizontalIsIdentity) {
  const BoxTree box_tree =
      tree(block("#root", lrect(10, 20, 50, 30), fill(kRed), std::vector<BlockBox>{}),
           kViewportWidth, WritingMode::HorizontalTb);

  EXPECT_TRUE(
      commands_eq(build_display_list(box_tree), {raster::FillRect{Rect{10, 20, 50, 30}, kRed}}));
}

TEST(PaintVertical, GlyphPenPosition) {
  // 中心軸 block = 40、ペンの inline = 12 と 28。
  TextFragment fragment = text(0, 16, kBlack, 40, {{10, 12}, {11, 28}});
  fragment.sideways = true;
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 100), {},
                 std::vector<LineBox>{line(lrect(0, 24, 100, 32), 40, {fragment})}),
           kViewportWidth, WritingMode::VerticalRl);

  EXPECT_TRUE(commands_eq(
      build_display_list(box_tree),
      {raster::DrawGlyphs{0, 16, kBlack, true, {{10, Point{160, 12}}, {11, Point{160, 28}}}}}));
}

// グリフのオフセットだけは物理 px のまま運ばれる（box_tree.hpp）。変換せずに足す。
TEST(PaintVertical, GlyphOffsetsStayPhysical) {
  TextFragment fragment = text(0, 16, kBlack, 40, {{10, 12}});
  fragment.glyphs[0].x_offset = 3;
  fragment.glyphs[0].y_offset = -5;
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 100), {},
                 std::vector<LineBox>{line(lrect(0, 24, 100, 32), 40, {fragment})}),
           kViewportWidth, WritingMode::VerticalRl);

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::DrawGlyphs{0, 16, kBlack, false, {{10, Point{163, 7}}}}}));
}

TEST(PaintVertical, ImageRectIsTransposed) {
  ImageFragment image;
  image.image = 1;
  image.rect = lrect(8, 16, 40, 40);
  image.content_rect = lrect(8, 16, 40, 40);
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 100), {},
                 std::vector<LineBox>{line(lrect(0, 16, 100, 40), 36, {image})}),
           kViewportWidth, WritingMode::VerticalRl);

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::DrawImage{1, Rect{200 - 56, 8, 40, 40}}}));
}

}  // namespace
}  // namespace shashoku::paint::test
