// DrawImage。等倍・拡大（バイリニア）・縮小（面積平均）・アルファ・クリップ・範囲外 ID。

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "raster/display_list.hpp"
#include "raster/raster_test_util.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster::test {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Target box(float width, float height, float scale = 1) {
  Target t;
  t.width = width;
  t.height = height;
  t.scale = scale;
  return t;
}

Color gray(int v) { return rgba(v, v, v, 255); }

// 等倍・整数位置ならピクセルがそのまま出る。
TEST(RasterImage, OneToOneIntegerPositionIsAnExactCopy) {
  const Bitmap image = make_image(2, 2,
                                  {rgba(10, 20, 30, 255), rgba(40, 50, 60, 255),
                                   rgba(70, 80, 90, 255), rgba(100, 110, 120, 255)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 2, 2}}}, box(2, 2), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(10, 20, 30, 255)));
  EXPECT_TRUE(pixel_is(b, 1, 0, rgba(40, 50, 60, 255)));
  EXPECT_TRUE(pixel_is(b, 0, 1, rgba(70, 80, 90, 255)));
  EXPECT_TRUE(pixel_is(b, 1, 1, rgba(100, 110, 120, 255)));
}

TEST(RasterImage, OneToOneAtAnIntegerOffset) {
  const Bitmap image = make_image(1, 1, {rgba(1, 2, 3, 255)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{2, 1, 1, 1}}}, box(4, 4), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 2, 1, rgba(1, 2, 3, 255)));
  EXPECT_TRUE(pixel_is(b, 1, 1, kTransparent));
}

// scale が掛かっても、デバイスピクセルとソースが 1 : 1 なら素通し。
TEST(RasterImage, ScaleIsAppliedToTheDestination) {
  const Bitmap image = make_image(2, 2, {gray(0), gray(60), gray(120), gray(180)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 1, 1}}}, box(1, 1, 2), glyphs,
                                  std::span<const Bitmap>(images));
  ASSERT_EQ(b.width, 2U);
  EXPECT_TRUE(pixel_is(b, 0, 0, gray(0)));
  EXPECT_TRUE(pixel_is(b, 1, 0, gray(60)));
  EXPECT_TRUE(pixel_is(b, 0, 1, gray(120)));
  EXPECT_TRUE(pixel_is(b, 1, 1, gray(180)));
}

// 2 倍拡大はピクセル中心どうしの線形補間（バイリニア）。
//   x = 0: [0, 0.75]      → 黒
//   x = 1: 0.75 黒 + 0.25 白 = 63.75 → 64
//   x = 2: 0.25 黒 + 0.75 白 = 191.25 → 191
//   x = 3: [1.25, 2]      → 白
TEST(RasterImage, MagnifyingUsesBilinearInterpolation) {
  const Bitmap image = make_image(2, 1, {gray(0), gray(255)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 4, 1}}}, box(4, 1), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, gray(0)));
  EXPECT_TRUE(pixel_is(b, 1, 0, gray(64)));
  EXPECT_TRUE(pixel_is(b, 2, 0, gray(191)));
  EXPECT_TRUE(pixel_is(b, 3, 0, gray(255)));
}

// 1/2 縮小は面積平均。(0 + 60 + 120 + 180) / 4 = 90。
TEST(RasterImage, MinifyingAveragesTheCoveredArea) {
  const Bitmap image = make_image(2, 2, {gray(0), gray(60), gray(120), gray(180)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 1, 1}}}, box(1, 1), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, gray(90)));
}

TEST(RasterImage, MinifyingFourToOnePerAxis) {
  // 4x1 の 0 / 40 / 80 / 120 を 1px に潰すと平均 60。
  const Bitmap image = make_image(4, 1, {gray(0), gray(40), gray(80), gray(120)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 1, 1}}}, box(1, 1), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, gray(60)));
}

// 平均は乗算済みアルファで取る（透明ピクセルの RGB が滲み出さない）。
//   out_a = round((255 + 0) / 2) = 128、out_rgb = 赤のまま
TEST(RasterImage, AveragingRespectsAlphaAndDoesNotBleedTransparentRgb) {
  const Bitmap image = make_image(2, 1, {rgba(255, 0, 0, 255), rgba(0, 0, 255, 0)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 1, 1}}}, box(1, 1), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 0, 0, 128)));
}

// 半透明の画像は source-over で下に重なる。a=128 の白 on 不透明の黒 → 灰 128。
TEST(RasterImage, TranslucentImageIsCompositedOverTheBackground) {
  const Bitmap image = make_image(1, 1, {rgba(255, 255, 255, 128)});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  Target t = box(1, 1);
  t.background = kBlack;
  const Bitmap b =
      must_rasterize({DrawImage{0, Rect{0, 0, 1, 1}}}, t, glyphs, std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, gray(128)));
}

// 小数座標の dest は端のピクセルが面積比で薄くなる（矩形と同じ被覆率の経路）。
TEST(RasterImage, FractionalDestinationUsesPartialCoverage) {
  const Bitmap image = make_image(1, 1, {kWhite});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const Bitmap b = must_rasterize({DrawImage{0, Rect{0, 0, 0.5F, 1}}}, box(1, 1), glyphs,
                                  std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 255, 255, 128)));
}

TEST(RasterImage, DestinationOutsideTheTargetIsCropped) {
  const Bitmap image = make_image(1, 1, {kWhite});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const DisplayList list = {
      DrawImage{0, Rect{-10, -10, 4, 4}},
      DrawImage{0, Rect{100, 100, 4, 4}},
  };
  const Bitmap b = must_rasterize(list, box(2, 2), glyphs, std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 0, 0, kTransparent));  // -10 + 4 = -6 なのでどちらも外
  EXPECT_TRUE(pixel_is(b, 1, 1, kTransparent));
}

TEST(RasterImage, ClipAppliesToImages) {
  const Bitmap image = make_image(1, 1, {kWhite});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const DisplayList list = {
      PushClip{Rect{0, 0, 2, 4}, 0},
      DrawImage{0, Rect{0, 0, 4, 4}},
      PopClip{},
  };
  const Bitmap b = must_rasterize(list, box(4, 4), glyphs, std::span<const Bitmap>(images));
  EXPECT_TRUE(pixel_is(b, 1, 1, kWhite));
  EXPECT_TRUE(pixel_is(b, 2, 1, kTransparent));
}

TEST(RasterImage, EmptyOrNonFiniteDestinationDrawsNothing) {
  const Bitmap image = make_image(1, 1, {kWhite});
  const std::vector<Bitmap> images = {image};
  FakeGlyphSource glyphs;
  const DisplayList list = {
      DrawImage{0, Rect{0, 0, 0, 2}},
      DrawImage{0, Rect{0, 0, 2, 0}},
      DrawImage{0, Rect{kNaN, 0, 2, 2}},
      DrawImage{0, Rect{0, 0, kNaN, 2}},
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, box(2, 2), glyphs, std::span<const Bitmap>(images))),
            0.0);
}

TEST(RasterImage, ZeroSizedImageDrawsNothing) {
  const std::vector<Bitmap> images = {Bitmap{}};
  FakeGlyphSource glyphs;
  EXPECT_EQ(coverage_sum(must_rasterize({DrawImage{0, Rect{0, 0, 2, 2}}}, box(2, 2), glyphs,
                                        std::span<const Bitmap>(images))),
            0.0);
}

TEST(RasterImage, OutOfRangeImageIdIsInternalError) {
  const std::vector<Bitmap> images = {make_image(1, 1, {kWhite})};
  FakeGlyphSource glyphs;
  const Error e = must_fail({DrawImage{1, Rect{0, 0, 2, 2}}}, box(2, 2), glyphs,
                            std::span<const Bitmap>(images));
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_NE(e.message.find("DrawImage"), std::string::npos);
}

TEST(RasterImage, EmptyImageTableMakesEveryIdOutOfRange) {
  FakeGlyphSource glyphs;
  EXPECT_EQ(must_fail({DrawImage{0, Rect{0, 0, 2, 2}}}, box(2, 2), glyphs).kind,
            ErrorKind::Internal);
}

// Bitmap の不変条件（rgba.size() == w * h * 4）を破った画像は Internal エラー。
TEST(RasterImage, MalformedImageIsInternalError) {
  Bitmap broken(2, 2);
  broken.rgba.resize(5);
  const std::vector<Bitmap> images = {broken};
  FakeGlyphSource glyphs;
  EXPECT_EQ(must_fail({DrawImage{0, Rect{0, 0, 2, 2}}}, box(2, 2), glyphs,
                      std::span<const Bitmap>(images))
                .kind,
            ErrorKind::Internal);
}

}  // namespace
}  // namespace shashoku::raster::test
