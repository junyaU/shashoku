#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "paint/display_list_builder.hpp"
#include "paint/paint_test_support.hpp"
#include "raster/display_list.hpp"

// 行の中身（ARCHITECTURE.md §3.9）: fragments の順に、背景 → 文字 → 画像。
namespace shashoku::paint::test {
namespace {

constexpr Color kRed{255, 0, 0, 255};
constexpr Color kYellow{255, 255, 0, 255};

BoxTree with_line(std::vector<InlineFragment> fragments, float baseline = 16) {
  return tree(
      block("#root", lrect(0, 0, 100, 20), {},
            std::vector<LineBox>{line(lrect(0, 0, 100, 20), baseline, std::move(fragments))}));
}

// グリフ原点 = ペン位置 + (x_offset, y_offset)。横書きのペン位置は (inline, baseline)。
TEST(PaintInline, GlyphOrigin) {
  TextFragment fragment = text(3, 16, kBlack, 16, {{10, 0}, {11, 16}});
  fragment.glyphs[1].x_offset = 1.5F;
  fragment.glyphs[1].y_offset = -2;
  const BoxTree box_tree = with_line({fragment});

  EXPECT_TRUE(commands_eq(
      build_display_list(box_tree),
      {raster::DrawGlyphs{3, 16, kBlack, false, {{10, Point{0, 16}}, {11, Point{17.5F, 14}}}}}));
}

// 同じ font / size / color / sideways が続く断片は 1 つの DrawGlyphs にまとめる。
TEST(PaintInline, MergesRunsWithSameStyle) {
  const BoxTree box_tree =
      with_line({text(0, 16, kBlack, 16, {{10, 0}}), text(0, 16, kBlack, 16, {{11, 16}}),
                 text(0, 16, kBlack, 16, {{12, 32}})});

  const raster::DisplayList list = build_display_list(box_tree);
  EXPECT_TRUE(commands_eq(
      list,
      {raster::DrawGlyphs{
          0, 16, kBlack, false, {{10, Point{0, 16}}, {11, Point{16, 16}}, {12, Point{32, 16}}}}}));
}

// フォント・サイズ・色・sideways のどれか 1 つでも違えば分かれる。
TEST(PaintInline, DoesNotMergeDifferentStyles) {
  TextFragment sideways = text(0, 16, kBlack, 16, {{13, 48}});
  sideways.sideways = true;
  const BoxTree box_tree =
      with_line({text(0, 16, kBlack, 16, {{10, 0}}), text(1, 16, kBlack, 16, {{11, 16}}),
                 text(1, 20, kBlack, 16, {{12, 32}}), text(1, 20, kRed, 16, {{12, 40}}), sideways});

  const raster::DisplayList list = build_display_list(box_tree);
  EXPECT_EQ(list.size(), 5U);
}

// 別の行の断片はまとめない（間に他の命令が入りうるので行をまたがない）。
TEST(PaintInline, DoesNotMergeAcrossLines) {
  const BoxTree box_tree = tree(block(
      "#root", lrect(0, 0, 100, 40), {},
      std::vector<LineBox>{line(lrect(0, 0, 100, 20), 16, {text(0, 16, kBlack, 16, {{10, 0}})}),
                           line(lrect(0, 20, 100, 20), 36, {text(0, 16, kBlack, 36, {{11, 0}})})}));

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::DrawGlyphs{0, 16, kBlack, false, {{10, Point{0, 16}}}},
                           raster::DrawGlyphs{0, 16, kBlack, false, {{11, Point{0, 36}}}}}));
}

// インライン背景は FillRect。断片の順序どおりに文字の前に出る。
TEST(PaintInline, InlineBackground) {
  const BoxTree box_tree = with_line(
      {InlineBackground{lrect(8, 2, 32, 16), kYellow}, text(0, 16, kBlack, 16, {{10, 8}})});

  EXPECT_TRUE(commands_eq(build_display_list(box_tree),
                          {raster::FillRect{Rect{8, 2, 32, 16}, kYellow},
                           raster::DrawGlyphs{0, 16, kBlack, false, {{10, Point{8, 16}}}}}));
}

// 背景を挟んだ同じスタイルの断片はまとめない（背景が文字の上に来てしまう）。
TEST(PaintInline, BackgroundBreaksGlyphRun) {
  const BoxTree box_tree = with_line({text(0, 16, kBlack, 16, {{10, 0}}),
                                      InlineBackground{lrect(16, 0, 16, 16), kYellow},
                                      text(0, 16, kBlack, 16, {{11, 16}})});

  const raster::DisplayList list = build_display_list(box_tree);
  ASSERT_EQ(list.size(), 3U);
  EXPECT_TRUE(std::holds_alternative<raster::DrawGlyphs>(list[0]));
  EXPECT_TRUE(std::holds_alternative<raster::FillRect>(list[1]));
  EXPECT_TRUE(std::holds_alternative<raster::DrawGlyphs>(list[2]));
}

TEST(PaintInline, TransparentInlineBackgroundEmitsNothing) {
  const BoxTree box_tree =
      with_line({InlineBackground{lrect(8, 2, 32, 16), Color{255, 255, 0, 0}}});
  EXPECT_TRUE(build_display_list(box_tree).empty());
}

TEST(PaintInline, InvisibleTextEmitsNothing) {
  const BoxTree box_tree =
      with_line({text(0, 16, Color{0, 0, 0, 0}, 16, {{10, 0}}), text(0, 16, kBlack, 16, {})});
  EXPECT_TRUE(build_display_list(box_tree).empty());
}

// 角丸なしの画像: 背景 → DrawImage → 枠線。クリップは出さない。
TEST(PaintInline, ImageWithoutRadius) {
  ImageFragment image;
  image.image = 2;
  image.rect = lrect(0, 0, 40, 40);
  image.content_rect = lrect(2, 2, 36, 36);
  image.decoration = border(2, kRed);
  image.decoration.background_color = kYellow;
  const BoxTree box_tree = with_line({image});

  EXPECT_TRUE(commands_eq(
      build_display_list(box_tree),
      {raster::FillRect{Rect{0, 0, 40, 40}, kYellow}, raster::DrawImage{2, Rect{2, 2, 36, 36}},
       raster::StrokeRoundedRect{Rect{0, 0, 40, 40}, 0, 2, kRed}}));
}

// 角丸つきの画像: content_rect を内周の角丸（radius − border_width）でクリップする。
TEST(PaintInline, ImageWithRadiusIsClipped) {
  ImageFragment image;
  image.image = 0;
  image.rect = lrect(0, 0, 40, 40);
  image.content_rect = lrect(3, 3, 34, 34);
  image.decoration = border(3, kRed, 20);
  const BoxTree box_tree = with_line({image});

  EXPECT_TRUE(commands_eq(
      build_display_list(box_tree),
      {raster::PushClip{Rect{3, 3, 34, 34}, 17}, raster::DrawImage{0, Rect{3, 3, 34, 34}},
       raster::PopClip{}, raster::StrokeRoundedRect{Rect{0, 0, 40, 40}, 20, 3, kRed}}));
}

// 内周の半径は負にならない（枠線が半径より太い場合）。
TEST(PaintInline, ImageInnerRadiusClampedToZero) {
  ImageFragment image;
  image.content_rect = lrect(0, 0, 10, 10);
  image.rect = lrect(0, 0, 10, 10);
  image.decoration.border_radius = 2;
  image.decoration.border_width = 6;
  image.decoration.border_color = kRed;
  const BoxTree box_tree = with_line({image});

  const raster::DisplayList list = build_display_list(box_tree);
  ASSERT_FALSE(list.empty());
  const auto& clip = std::get<raster::PushClip>(list[0]);
  EXPECT_FLOAT_EQ(clip.radius, 0);
}

}  // namespace
}  // namespace shashoku::paint::test
