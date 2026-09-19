// 矩形・角丸矩形・枠線・クリップの被覆率。
// 「被覆率（0..255）× クリップ × 色のアルファ → 実効アルファ」の経路は 1 本しかないので、
// 不透明な色で塗ったときのアルファ値がそのまま被覆率になる。期待値はそれを使って手計算する。

#include <cstdint>
#include <limits>
#include <numbers>
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

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Target square(float size, float scale = 1) {
  Target t;
  t.width = size;
  t.height = size;
  t.scale = scale;
  return t;
}

// 不透明な黒で塗ったときのアルファ（= 被覆率）。
std::uint32_t alpha_at(const Bitmap& b, std::uint32_t x, std::uint32_t y) {
  return b.pixel(x, y).a;
}

// ---------------------------------------------------------------------------
// FillRect
// ---------------------------------------------------------------------------

TEST(RasterFill, IntegerRectIsFullyCovered) {
  const Bitmap b = must_rasterize({FillRect{Rect{1, 1, 2, 2}, kBlack}}, square(4));
  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      const bool inside = x >= 1 && x <= 2 && y >= 1 && y <= 2;
      EXPECT_TRUE(pixel_is(b, x, y, inside ? kBlack : kTransparent));
    }
  }
}

// 0.5px 境界のピクセルは面積比 0.5 → 被覆率 round(127.5) = 128。
TEST(RasterFill, HalfPixelEdgeGivesHalfCoverage) {
  Target t = square(2);
  t.height = 1;
  const Bitmap b = must_rasterize({FillRect{Rect{0.5F, 0, 1.5F, 1}, rgba(255, 0, 0, 255)}}, t);
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 0, 0, 128)));
  EXPECT_TRUE(pixel_is(b, 1, 0, rgba(255, 0, 0, 255)));
}

// 角のピクセルは x と y の面積比の積。0.5 * 0.5 = 0.25 → round(63.75) = 64。
TEST(RasterFill, FractionalCornerUsesAreaOfBothAxes) {
  const Bitmap b = must_rasterize({FillRect{Rect{0.5F, 0.5F, 0.5F, 0.5F}, kBlack}}, square(2));
  EXPECT_EQ(alpha_at(b, 0, 0), 64U);
  EXPECT_EQ(alpha_at(b, 1, 0), 0U);
  EXPECT_EQ(alpha_at(b, 0, 1), 0U);
  EXPECT_EQ(alpha_at(b, 1, 1), 0U);
}

TEST(RasterFill, ScaleTwoExpandsToDevicePixels) {
  const Bitmap b = must_rasterize({FillRect{Rect{0, 0, 1, 1}, kBlack}}, square(2, 2));
  ASSERT_EQ(b.width, 4U);
  EXPECT_TRUE(pixel_is(b, 0, 0, kBlack));
  EXPECT_TRUE(pixel_is(b, 1, 1, kBlack));
  EXPECT_TRUE(pixel_is(b, 2, 0, kTransparent));
  EXPECT_TRUE(pixel_is(b, 0, 2, kTransparent));
}

// scale 1.5 では 1 CSS px が 1.5 デバイス px になる。
//   x = 0: 面積 1      → 255
//   x = 1: 面積 0.5    → 128
//   (1, 1): 0.5 * 0.5  → 64
TEST(RasterFill, FractionalScaleProducesPartialCoverage) {
  const Bitmap b = must_rasterize({FillRect{Rect{0, 0, 1, 1}, kBlack}}, square(2, 1.5F));
  ASSERT_EQ(b.width, 3U);
  ASSERT_EQ(b.height, 3U);
  EXPECT_EQ(alpha_at(b, 0, 0), 255U);
  EXPECT_EQ(alpha_at(b, 1, 0), 128U);
  EXPECT_EQ(alpha_at(b, 0, 1), 128U);
  EXPECT_EQ(alpha_at(b, 1, 1), 64U);
  EXPECT_EQ(alpha_at(b, 2, 0), 0U);
}

TEST(RasterFill, RectLargerThanTargetIsClipped) {
  const Bitmap b = must_rasterize({FillRect{Rect{-10, -10, 100, 100}, kBlack}}, square(2));
  for (std::uint32_t y = 0; y < 2; ++y) {
    for (std::uint32_t x = 0; x < 2; ++x) {
      EXPECT_TRUE(pixel_is(b, x, y, kBlack));
    }
  }
}

TEST(RasterFill, RectOutsideTargetDrawsNothing) {
  const DisplayList list = {
      FillRect{Rect{50, 50, 10, 10}, kBlack},
      FillRect{Rect{-50, -50, 10, 10}, kBlack},
  };
  const Bitmap b = must_rasterize(list, square(2));
  EXPECT_EQ(coverage_sum(b), 0.0);
}

TEST(RasterFill, EmptyRectDrawsNothing) {
  const DisplayList list = {
      FillRect{Rect{0, 0, 0, 2}, kBlack},
      FillRect{Rect{0, 0, 2, 0}, kBlack},
      FillRect{Rect{0, 0, -2, -2}, kBlack},
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, square(2))), 0.0);
}

TEST(RasterFill, NonFiniteCoordinatesAreIgnored) {
  const DisplayList list = {
      FillRect{Rect{kNaN, 0, 2, 2}, kBlack},
      FillRect{Rect{0, kNaN, 2, 2}, kBlack},
      FillRect{Rect{0, 0, kNaN, 2}, kBlack},
      FillRect{Rect{0, 0, 2, kNaN}, kBlack},
      FillRect{Rect{kInf, 0, 2, 2}, kBlack},
      FillRect{Rect{0, 0, kInf, kInf}, kBlack},
      FillRect{Rect{-kInf, -kInf, kInf, kInf}, kBlack},
      FillRoundedRect{Rect{0, 0, 2, 2}, kNaN, kBlack},
      FillRoundedRect{Rect{0, 0, 2, 2}, kInf, kBlack},
      StrokeRoundedRect{Rect{0, 0, 2, 2}, 0, kNaN, kBlack},
      StrokeRoundedRect{Rect{kNaN, 0, 2, 2}, 0, 1, kBlack},
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, square(2))), 0.0);
}

// ---------------------------------------------------------------------------
// FillRoundedRect
// ---------------------------------------------------------------------------

// 半径 0 の角丸は FillRect と 1 ビットも違わないこと。
TEST(RasterRounded, ZeroRadiusIsBitIdenticalToFillRect) {
  for (const Rect& r :
       std::vector<Rect>{{1, 1, 3, 2}, {0.5F, 0.25F, 2.75F, 1.5F}, {-1, -0.5F, 8, 8}}) {
    const Bitmap plain = must_rasterize({FillRect{r, rgba(10, 200, 30, 170)}}, square(4));
    const Bitmap rounded =
        must_rasterize({FillRoundedRect{r, 0, rgba(10, 200, 30, 170)}}, square(4));
    EXPECT_EQ(plain, rounded) << "rect " << r.x << "," << r.y << " " << r.width << "x" << r.height;
  }
}

// 半径は min(w, h) / 2 に切り詰める。
TEST(RasterRounded, RadiusIsClampedToHalfOfShorterSide) {
  const Rect r{0, 0, 20, 10};
  const Bitmap huge = must_rasterize({FillRoundedRect{r, 100, kBlack}}, square(20));
  const Bitmap capsule = must_rasterize({FillRoundedRect{r, 5, kBlack}}, square(20));
  EXPECT_EQ(huge, capsule);
}

TEST(RasterRounded, SquareWithMaxRadiusIsACircle) {
  const Bitmap b = must_rasterize({FillRoundedRect{Rect{0, 0, 20, 20}, 10, kBlack}}, square(20));
  EXPECT_EQ(alpha_at(b, 0, 0), 0U);  // 角は円の外
  EXPECT_EQ(alpha_at(b, 19, 19), 0U);
  EXPECT_EQ(alpha_at(b, 10, 10), 255U);  // 中心
  EXPECT_GT(alpha_at(b, 0, 10), 200U);   // 左端の中央は円に近い
}

TEST(RasterRounded, CornersAreMirrorSymmetric) {
  const Bitmap b = must_rasterize({FillRoundedRect{Rect{0, 0, 16, 16}, 5, kBlack}}, square(16));
  for (std::uint32_t y = 0; y < 16; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      EXPECT_EQ(alpha_at(b, x, y), alpha_at(b, 15 - x, y)) << "左右 (" << x << ", " << y << ")";
      EXPECT_EQ(alpha_at(b, x, y), alpha_at(b, x, 15 - y)) << "上下 (" << x << ", " << y << ")";
    }
  }
}

TEST(RasterRounded, EdgePixelsHaveIntermediateCoverage) {
  const Bitmap b = must_rasterize({FillRoundedRect{Rect{0, 0, 16, 16}, 5, kBlack}}, square(16));
  EXPECT_EQ(alpha_at(b, 0, 0), 0U);                  // 角の外
  EXPECT_EQ(alpha_at(b, 8, 8), 255U);                // 内側
  EXPECT_EQ(alpha_at(b, 8, 0), 255U);                // 直線部分
  const std::uint32_t diagonal = alpha_at(b, 1, 1);  // 角の縁
  EXPECT_GT(diagonal, 0U);
  EXPECT_LT(diagonal, 255U);
}

// 被覆率の総和 ≈ 解析的な面積（w*h − (4 − π)r²）。
TEST(RasterRounded, CoverageSumApproximatesAnalyticArea) {
  struct Case {
    float size;
    float radius;
  };
  for (const Case& c : std::vector<Case>{{16, 5}, {20, 10}, {32, 8}, {9, 4}}) {
    const Bitmap b = must_rasterize({FillRoundedRect{Rect{0, 0, c.size, c.size}, c.radius, kBlack}},
                                    square(c.size));
    const auto size = static_cast<double>(c.size);
    const auto radius = static_cast<double>(c.radius);
    const double want = (size * size) - ((4.0 - std::numbers::pi) * radius * radius);
    EXPECT_NEAR(coverage_sum(b), want, 0.75) << "size " << c.size << " radius " << c.radius;
  }
}

TEST(RasterRounded, NegativeRadiusBehavesLikeZero) {
  const Rect r{1, 1, 3, 2};
  EXPECT_EQ(must_rasterize({FillRoundedRect{r, -5, kBlack}}, square(4)),
            must_rasterize({FillRect{r, kBlack}}, square(4)));
}

// ---------------------------------------------------------------------------
// StrokeRoundedRect
// ---------------------------------------------------------------------------

// radius 0・整数座標・幅 1 の枠線は、外周 1px だけがちょうど塗られる。
TEST(RasterStroke, ZeroRadiusGivesExactPixels) {
  const Bitmap b = must_rasterize({StrokeRoundedRect{Rect{0, 0, 6, 6}, 0, 1, kBlack}}, square(6));
  for (std::uint32_t y = 0; y < 6; ++y) {
    for (std::uint32_t x = 0; x < 6; ++x) {
      const bool on_border = x == 0 || y == 0 || x == 5 || y == 5;
      EXPECT_TRUE(pixel_is(b, x, y, on_border ? kBlack : kTransparent));
    }
  }
}

TEST(RasterStroke, InteriorIsNotPainted) {
  const DisplayList list = {
      FillRect{Rect{0, 0, 6, 6}, kWhite},
      StrokeRoundedRect{Rect{0, 0, 6, 6}, 0, 2, rgba(255, 0, 0, 128)},
  };
  const Bitmap b = must_rasterize(list, square(6));
  for (std::uint32_t y = 2; y < 4; ++y) {
    for (std::uint32_t x = 2; x < 4; ++x) {
      EXPECT_TRUE(pixel_is(b, x, y, kWhite));  // 内側は白のまま
    }
  }
}

// 「外周の被覆率 − 内周の被覆率」で 1 回だけ塗るので、角も他の辺と同じアルファになる。
TEST(RasterStroke, TranslucentStrokeDoesNotDoublePaintCorners) {
  const Bitmap b =
      must_rasterize({StrokeRoundedRect{Rect{0, 0, 6, 6}, 0, 1, rgba(255, 0, 0, 128)}}, square(6));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 0, 0, 128)));  // 左上の角
  EXPECT_TRUE(pixel_is(b, 5, 0, rgba(255, 0, 0, 128)));
  EXPECT_TRUE(pixel_is(b, 0, 5, rgba(255, 0, 0, 128)));
  EXPECT_TRUE(pixel_is(b, 5, 5, rgba(255, 0, 0, 128)));
  EXPECT_TRUE(pixel_is(b, 3, 0, rgba(255, 0, 0, 128)));  // 上辺
  EXPECT_TRUE(pixel_is(b, 0, 3, rgba(255, 0, 0, 128)));  // 左辺
}

// 幅 0.5 の枠線。外周は整数座標、内周は (0.5, 0.5)-(3.5, 3.5)。
//   (0, 0): 1 − 0.25 = 0.75 → round(191.25) = 191
//   (1, 0): 1 − 0.5  = 0.5  → round(127.5)  = 128
//   (1, 1): 1 − 1    = 0
TEST(RasterStroke, FractionalWidthUsesOuterMinusInnerCoverage) {
  const Bitmap b =
      must_rasterize({StrokeRoundedRect{Rect{0, 0, 4, 4}, 0, 0.5F, kBlack}}, square(4));
  EXPECT_EQ(alpha_at(b, 0, 0), 191U);
  EXPECT_EQ(alpha_at(b, 1, 0), 128U);
  EXPECT_EQ(alpha_at(b, 0, 1), 128U);
  EXPECT_EQ(alpha_at(b, 1, 1), 0U);
  EXPECT_EQ(alpha_at(b, 3, 3), 191U);
}

// 内周が潰れるほど太い枠線は外周の塗りつぶしになる。
TEST(RasterStroke, TooWideStrokeFillsTheWholeShape) {
  const Rect r{0, 0, 6, 6};
  EXPECT_EQ(must_rasterize({StrokeRoundedRect{r, 0, 10, kBlack}}, square(6)),
            must_rasterize({FillRect{r, kBlack}}, square(6)));
  EXPECT_EQ(must_rasterize({StrokeRoundedRect{r, 2, 3, kBlack}}, square(6)),
            must_rasterize({FillRoundedRect{r, 2, kBlack}}, square(6)));
}

TEST(RasterStroke, ZeroWidthDrawsNothing) {
  EXPECT_EQ(
      coverage_sum(must_rasterize({StrokeRoundedRect{Rect{0, 0, 6, 6}, 2, 0, kBlack}}, square(6))),
      0.0);
}

// 角丸の枠線の被覆率 ≈ 外周の塗りつぶし − 内周の塗りつぶし（丸めの差だけずれる）。
TEST(RasterStroke, RoundedStrokeMatchesOuterMinusInnerFill) {
  const Rect outer{0, 0, 16, 16};
  const Rect inner{2, 2, 12, 12};
  const Bitmap stroke = must_rasterize({StrokeRoundedRect{outer, 5, 2, kBlack}}, square(16));
  const Bitmap fill_outer = must_rasterize({FillRoundedRect{outer, 5, kBlack}}, square(16));
  const Bitmap fill_inner = must_rasterize({FillRoundedRect{inner, 3, kBlack}}, square(16));
  for (std::uint32_t y = 0; y < 16; ++y) {
    for (std::uint32_t x = 0; x < 16; ++x) {
      const int want = static_cast<int>(alpha_at(fill_outer, x, y)) -
                       static_cast<int>(alpha_at(fill_inner, x, y));
      EXPECT_NEAR(static_cast<int>(alpha_at(stroke, x, y)), want, 2)
          << "(" << x << ", " << y << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// PushClip / PopClip
// ---------------------------------------------------------------------------

TEST(RasterClip, RectangularClipLimitsPainting) {
  const DisplayList list = {
      PushClip{Rect{1, 1, 2, 2}, 0},
      FillRect{Rect{0, 0, 4, 4}, kBlack},
      PopClip{},
  };
  const Bitmap b = must_rasterize(list, square(4));
  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      const bool inside = x >= 1 && x <= 2 && y >= 1 && y <= 2;
      EXPECT_TRUE(pixel_is(b, x, y, inside ? kBlack : kTransparent));
    }
  }
}

TEST(RasterClip, PaintingResumesAfterPopClip) {
  const DisplayList list = {
      PushClip{Rect{0, 0, 1, 1}, 0},
      PopClip{},
      FillRect{Rect{0, 0, 4, 4}, kBlack},
  };
  const Bitmap b = must_rasterize(list, square(4));
  EXPECT_TRUE(pixel_is(b, 3, 3, kBlack));
}

// クリップの縁もアンチエイリアスされる。0.5px なら被覆率 128。
TEST(RasterClip, ClipEdgeIsAntiAliased) {
  Target t = square(2);
  t.height = 1;
  const DisplayList list = {
      PushClip{Rect{0.5F, 0, 1.5F, 1}, 0},
      FillRect{Rect{0, 0, 2, 1}, kWhite},
      PopClip{},
  };
  const Bitmap b = must_rasterize(list, t);
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 255, 255, 128)));
  EXPECT_TRUE(pixel_is(b, 1, 0, kWhite));
}

// 入れ子のクリップは被覆率の積。128 * 128 / 255 = 64.25 → 64。
TEST(RasterClip, NestedClipsMultiply) {
  const Target t = square(1);
  const DisplayList list = {
      PushClip{Rect{0, 0, 0.5F, 1}, 0},
      PushClip{Rect{0, 0, 0.5F, 1}, 0},
      FillRect{Rect{0, 0, 1, 1}, kBlack},
      PopClip{},
      PopClip{},
  };
  EXPECT_EQ(alpha_at(must_rasterize(list, t), 0, 0), 64U);
}

TEST(RasterClip, NestedClipsIntersect) {
  const DisplayList list = {
      PushClip{Rect{0, 0, 3, 4}, 0},
      PushClip{Rect{2, 0, 2, 4}, 0},
      FillRect{Rect{0, 0, 4, 4}, kBlack},
      PopClip{},
      PopClip{},
  };
  const Bitmap b = must_rasterize(list, square(4));
  EXPECT_EQ(alpha_at(b, 1, 1), 0U);
  EXPECT_EQ(alpha_at(b, 2, 1), 255U);  // 2 つのクリップの共通部分は x = 2 だけ
  EXPECT_EQ(alpha_at(b, 3, 1), 0U);
}

TEST(RasterClip, RoundedClipSoftensCorners) {
  const DisplayList list = {
      PushClip{Rect{0, 0, 16, 16}, 5},
      FillRect{Rect{0, 0, 16, 16}, kBlack},
      PopClip{},
  };
  const Bitmap clipped = must_rasterize(list, square(16));
  const Bitmap direct =
      must_rasterize({FillRoundedRect{Rect{0, 0, 16, 16}, 5, kBlack}}, square(16));
  EXPECT_EQ(clipped, direct);
}

TEST(RasterClip, EmptyClipBlocksEverything) {
  const DisplayList list = {
      PushClip{Rect{0, 0, 0, 0}, 0},
      FillRect{Rect{0, 0, 4, 4}, kBlack},
      PopClip{},
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, square(4))), 0.0);
}

TEST(RasterClip, NonFiniteClipBlocksEverything) {
  const DisplayList list = {
      PushClip{Rect{kNaN, 0, 4, 4}, 0},
      FillRect{Rect{0, 0, 4, 4}, kBlack},
      PopClip{},
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, square(4))), 0.0);
}

TEST(RasterClip, PopWithoutPushIsInternalError) {
  const Error e = must_fail({PopClip{}}, square(4));
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_NE(e.message.find("PopClip"), std::string::npos);
}

TEST(RasterClip, UnbalancedPushAtEndIsInternalError) {
  const Error e = must_fail({PushClip{Rect{0, 0, 1, 1}, 0}}, square(4));
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_NE(e.message.find("PushClip"), std::string::npos);
}

TEST(RasterClip, ExtraPopAfterBalancedPairIsInternalError) {
  const DisplayList list = {
      PushClip{Rect{0, 0, 1, 1}, 0},
      PopClip{},
      PopClip{},
  };
  EXPECT_EQ(must_fail(list, square(4)).kind, ErrorKind::Internal);
}

}  // namespace
}  // namespace shashoku::raster::test
