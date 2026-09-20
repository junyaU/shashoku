#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "paint/display_list_builder.hpp"
#include "paint/paint_test_support.hpp"
#include "raster/display_list.hpp"

// ブロックの塗り（ARCHITECTURE.md §3.9）: 背景 → 枠線 → 子 の順。
namespace shashoku::paint::test {
namespace {

constexpr Color kRed{255, 0, 0, 255};
constexpr Color kBlue{0, 0, 255, 255};
constexpr Color kGreen{0, 128, 0, 255};

TEST(PaintBlock, BackgroundThenBorder) {
  BoxDecoration decoration = fill(kRed);
  decoration.border_width = 2;
  decoration.border_color = kBlue;
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 40), decoration, std::vector<BlockBox>{}));

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::FillRect{Rect{0, 0, 100, 40}, kRed},
                           raster::StrokeRoundedRect{Rect{0, 0, 100, 40}, 0, 2, kBlue}}));
}

// 角丸があるときだけ FillRoundedRect。枠線は radius 0 でも StrokeRoundedRect で出す
// （display_list.hpp: radius = 0 なら普通の矩形の枠）。
TEST(PaintBlock, RoundedBackground) {
  BoxDecoration decoration = fill(kRed);
  decoration.border_radius = 8;
  decoration.border_width = 1;
  decoration.border_color = kBlue;
  const BoxTree box_tree =
      tree(block("#root", lrect(10, 20, 60, 30), decoration, std::vector<BlockBox>{}));

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::FillRoundedRect{Rect{10, 20, 60, 30}, 8, kRed},
                           raster::StrokeRoundedRect{Rect{10, 20, 60, 30}, 8, 1, kBlue}}));
}

TEST(PaintBlock, TransparentPaintEmitsNothing) {
  BoxDecoration decoration;
  decoration.background_color = Color{255, 0, 0, 0};  // 完全に透明
  decoration.border_width = 4;
  decoration.border_color = Color{0, 0, 255, 0};  // 透明な枠線
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 40), decoration, std::vector<BlockBox>{}));

  EXPECT_TRUE(commands_eq(build_display_list(box_tree), {}));
}

TEST(PaintBlock, ZeroWidthBorderEmitsNothing) {
  BoxDecoration decoration;
  decoration.border_width = 0;
  decoration.border_color = kBlue;
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 40), decoration, std::vector<BlockBox>{}));

  EXPECT_TRUE(commands_eq(build_display_list(box_tree), {}));
}

// 前順（親の背景と枠線 → 子）。兄弟は並び順。
TEST(PaintBlock, PreorderTraversal) {
  const BoxTree box_tree =
      tree(block("#root", lrect(0, 0, 100, 60), fill(kRed),
                 std::vector<BlockBox>{
                     block("div", lrect(0, 0, 100, 30), border(1, kGreen),
                           std::vector<BlockBox>{block("p", lrect(5, 5, 90, 20), fill(kBlue),
                                                       std::vector<BlockBox>{})}),
                     block("div", lrect(0, 30, 100, 30), fill(kGreen), std::vector<BlockBox>{})}));

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::FillRect{Rect{0, 0, 100, 60}, kRed},
                           raster::StrokeRoundedRect{Rect{0, 0, 100, 30}, 0, 1, kGreen},
                           raster::FillRect{Rect{5, 5, 90, 20}, kBlue},
                           raster::FillRect{Rect{0, 30, 100, 30}, kGreen}}));
}

// 子を持たないブロックでも落ちない（children の既定はブロックの空列）。
TEST(PaintBlock, EmptyTree) {
  const BoxTree box_tree = tree(block("#root", lrect(0, 0, 0, 0), {}, std::vector<BlockBox>{}));
  EXPECT_TRUE(build_display_list(box_tree).empty());
}

}  // namespace
}  // namespace shashoku::paint::test
