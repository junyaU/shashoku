// 合成の数式（source-over・ストレートアルファ・8bit 四捨五入）とターゲットの検証。
// 期待値はすべて手計算で出している（ARCHITECTURE.md §3.3）。

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/raster_test_util.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster::test {
namespace {

Target one_pixel(Color background) {
  Target t;
  t.width = 1;
  t.height = 1;
  t.background = background;
  return t;
}

// ターゲット全面を覆う FillRect。
DrawCmd cover(Color color, float size = 1) { return FillRect{Rect{0, 0, size, size}, color}; }

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// ---------------------------------------------------------------------------
// 合成の数式
// ---------------------------------------------------------------------------

TEST(RasterBlend, OpaqueOverTransparent) {
  const Bitmap b = must_rasterize({cover(rgba(10, 20, 30, 255))}, one_pixel(kTransparent));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(10, 20, 30, 255)));
}

TEST(RasterBlend, OpaqueOverOpaqueReplaces) {
  const Bitmap b = must_rasterize({cover(rgba(10, 20, 30, 255))}, one_pixel(kWhite));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(10, 20, 30, 255)));
}

// 白の不透明背景に赤 a=128。
//   out_a = 255
//   g = (0 * 128 + 255 * 127 + 127) / 255 = 127   （127 = round(255 * 127/255)）
TEST(RasterBlend, SemiTransparentOverOpaque) {
  const Bitmap b = must_rasterize({cover(rgba(255, 0, 0, 128))}, one_pixel(kWhite));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 127, 127, 255)));
}

// 透明背景に半透明を 1 枚。ストレートアルファなので色はそのまま残る。
TEST(RasterBlend, SemiTransparentOverTransparent) {
  const Bitmap b = must_rasterize({cover(rgba(255, 0, 0, 128))}, one_pixel(kTransparent));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 0, 0, 128)));
}

// 半透明 on 半透明。
//   keep  = round(128 * 127 / 255) = 64
//   out_a = 128 + 64 = 192
//   r     = (0 * 128 + 255 * 64 + 96) / 192 = 85
//   b     = (255 * 128 + 0 * 64 + 96) / 192 = 170
TEST(RasterBlend, SemiTransparentOverSemiTransparent) {
  const Bitmap b = must_rasterize({cover(rgba(255, 0, 0, 128)), cover(rgba(0, 0, 255, 128))},
                                  one_pixel(kTransparent));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(85, 0, 170, 192)));
}

// 不透明黒の上に白 a=64 → 灰色 64（= round(255 * 64/255)）。
TEST(RasterBlend, QuarterAlphaOverOpaqueBlack) {
  const Bitmap b = must_rasterize({cover(rgba(255, 255, 255, 64))}, one_pixel(kBlack));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(64, 64, 64, 255)));
}

TEST(RasterBlend, ZeroAlphaLeavesDestinationUntouched) {
  const Bitmap b = must_rasterize({cover(rgba(200, 100, 50, 0))}, one_pixel(rgba(10, 20, 30, 255)));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(10, 20, 30, 255)));
}

// 決定性のため、アルファ 0 のピクセルの RGB は 0 に正規化する。
TEST(RasterBlend, TransparentPixelsHaveZeroRgb) {
  const Bitmap b = must_rasterize({}, one_pixel(rgba(200, 100, 50, 0)));
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(0, 0, 0, 0)));
}

TEST(RasterBlend, OpaqueBackgroundFillsWholeTarget) {
  Target t = one_pixel(rgba(1, 2, 3, 255));
  t.width = 3;
  t.height = 2;
  const Bitmap b = must_rasterize({}, t);
  ASSERT_EQ(b.width, 3U);
  ASSERT_EQ(b.height, 2U);
  for (std::uint32_t y = 0; y < 2; ++y) {
    for (std::uint32_t x = 0; x < 3; ++x) {
      EXPECT_TRUE(pixel_is(b, x, y, rgba(1, 2, 3, 255)));
    }
  }
}

// DESIGN.md Phase 1 の受け入れ条件「重なった半透明矩形」。
// 透明背景に、赤 a=128 の矩形と青 a=128 の矩形を 1 列だけ重ねる。
TEST(RasterBlend, Phase1OverlappingTranslucentRectsOnTransparent) {
  Target t;
  t.width = 4;
  t.height = 2;
  const DisplayList list = {
      FillRect{Rect{0, 0, 3, 2}, rgba(255, 0, 0, 128)},
      FillRect{Rect{2, 0, 2, 2}, rgba(0, 0, 255, 128)},
  };
  const Bitmap b = must_rasterize(list, t);
  for (std::uint32_t y = 0; y < 2; ++y) {
    EXPECT_TRUE(pixel_is(b, 0, y, rgba(255, 0, 0, 128)));  // 赤だけ
    EXPECT_TRUE(pixel_is(b, 1, y, rgba(255, 0, 0, 128)));
    EXPECT_TRUE(pixel_is(b, 2, y, rgba(85, 0, 170, 192)));  // 重なり
    EXPECT_TRUE(pixel_is(b, 3, y, rgba(0, 0, 255, 128)));   // 青だけ
  }
}

// 同じ重ね方を不透明な白背景の上で。
//   赤 a=128 on 白   → (255, 127, 127, 255)
//   青 a=128 on それ → r = (0*128 + 255*127 + 127)/255 = 127
//                      g = (0*128 + 127*127 + 127)/255 = 63
//                      b = (255*128 + 127*127 + 127)/255 = 191
TEST(RasterBlend, Phase1OverlappingTranslucentRectsOnWhite) {
  Target t;
  t.width = 4;
  t.height = 1;
  t.background = kWhite;
  const DisplayList list = {
      FillRect{Rect{0, 0, 3, 1}, rgba(255, 0, 0, 128)},
      FillRect{Rect{2, 0, 2, 1}, rgba(0, 0, 255, 128)},
  };
  const Bitmap b = must_rasterize(list, t);
  EXPECT_TRUE(pixel_is(b, 0, 0, rgba(255, 127, 127, 255)));
  EXPECT_TRUE(pixel_is(b, 1, 0, rgba(255, 127, 127, 255)));
  EXPECT_TRUE(pixel_is(b, 2, 0, rgba(127, 63, 191, 255)));
  EXPECT_TRUE(pixel_is(b, 3, 0, rgba(127, 127, 255, 255)));
}

// ---------------------------------------------------------------------------
// Target の検証
// ---------------------------------------------------------------------------

TEST(RasterTarget, NonPositiveSizeIsInvalidOption) {
  for (const std::pair<float, float>& size :
       std::vector<std::pair<float, float>>{{0, 4}, {4, 0}, {-1, 4}, {4, -1}, {0, 0}}) {
    Target t;
    t.width = size.first;
    t.height = size.second;
    EXPECT_EQ(must_fail({}, t).kind, ErrorKind::InvalidOption)
        << size.first << " x " << size.second;
  }
}

TEST(RasterTarget, NonFiniteSizeIsInvalidOption) {
  for (const float v : {kNaN, kInf}) {
    Target t;
    t.width = v;
    t.height = 4;
    EXPECT_EQ(must_fail({}, t).kind, ErrorKind::InvalidOption);
    t.width = 4;
    t.height = v;
    EXPECT_EQ(must_fail({}, t).kind, ErrorKind::InvalidOption);
  }
}

TEST(RasterTarget, NonPositiveOrNonFiniteScaleIsInvalidOption) {
  for (const float scale : {0.0F, -1.0F, kNaN, kInf}) {
    Target t;
    t.width = 4;
    t.height = 4;
    t.scale = scale;
    EXPECT_EQ(must_fail({}, t).kind, ErrorKind::InvalidOption);
  }
}

TEST(RasterTarget, DeviceSizeIsCssSizeTimesScaleRoundedUp) {
  struct Case {
    float width;
    float height;
    float scale;
    std::uint32_t want_width;
    std::uint32_t want_height;
  };
  for (const Case& c : std::vector<Case>{
           {4, 3, 1, 4, 3},
           {4.2F, 3, 1, 5, 3},
           {4, 3, 2, 8, 6},
           {4, 3, 1.5F, 6, 5},     // ceil(4.5) = 5
           {0.1F, 0.1F, 1, 1, 1},  // 1px 未満でも 1px になる
       }) {
    Target t;
    t.width = c.width;
    t.height = c.height;
    t.scale = c.scale;
    const Bitmap b = must_rasterize({}, t);
    EXPECT_EQ(b.width, c.want_width) << c.width << " x " << c.height << " @" << c.scale;
    EXPECT_EQ(b.height, c.want_height) << c.width << " x " << c.height << " @" << c.scale;
    EXPECT_EQ(b.rgba.size(), static_cast<std::size_t>(b.width) * b.height * 4);
  }
}

// 大きすぎる出力は LimitExceeded（不正な値ではなく「上限を超えた」なので。A21）。
TEST(RasterTarget, HugeDeviceSizeExceedsTheLimit) {
  Target wide;
  wide.width = 1.0e9F;  // 1 辺が 2^26 を超える
  wide.height = 1;
  EXPECT_EQ(must_fail({}, wide).kind, ErrorKind::LimitExceeded);

  Target big;
  big.width = 20000;  // 20000 * 20000 = 4e8 > 2^26
  big.height = 20000;
  EXPECT_EQ(must_fail({}, big).kind, ErrorKind::LimitExceeded);

  Target scaled;
  scaled.width = 1000;
  scaled.height = 1000;
  scaled.scale = 1000;  // 1e6 px 角
  EXPECT_EQ(must_fail({}, scaled).kind, ErrorKind::LimitExceeded);
}

// 2^26 ピクセルを 1 つでも超えたら拒否する（ぴったりは通すが、テストで 256MB は確保しない）。
TEST(RasterTarget, JustOverThePixelLimitExceedsTheLimit) {
  Target t;
  t.width = 8192;
  t.height = 8193;  // 8192 * 8193 = 2^26 + 8192
  EXPECT_EQ(must_fail({}, t).kind, ErrorKind::LimitExceeded);
}

// 上限は呼び出し側が決める（api は RenderLimits::device_pixels を渡す）。
// ちょうどは通り、1 画素でも超えたら LimitExceeded。ピクセルは確保されない。
TEST(RasterTarget, MaxDevicePixelsIsAField) {
  Target exact;
  exact.width = 40;
  exact.height = 10;
  exact.max_device_pixels = 400;
  const Bitmap b = must_rasterize({}, exact);
  EXPECT_EQ(b.width, 40U);
  EXPECT_EQ(b.height, 10U);

  Target over = exact;
  over.max_device_pixels = 399;
  const Error error = must_fail({}, over);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("40 x 10 = 400"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("the limit of 399"), std::string::npos) << error.message;

  // scale 込みで数える（40 x 10 @2 = 1600 px）。
  Target scaled = exact;
  scaled.scale = 2;
  scaled.max_device_pixels = 1599;
  EXPECT_EQ(must_fail({}, scaled).kind, ErrorKind::LimitExceeded);
  scaled.max_device_pixels = 1600;
  EXPECT_EQ(must_rasterize({}, scaled).rgba.size(), std::size_t{1600} * 4);
}

TEST(RasterTarget, LargeButAllowedSizeIsAccepted) {
  Target t;
  t.width = 2048;
  t.height = 1024;
  const Bitmap b = must_rasterize({}, t);
  EXPECT_EQ(b.width, 2048U);
  EXPECT_EQ(b.height, 1024U);
  EXPECT_EQ(b.rgba.size(), static_cast<std::size_t>(2048) * 1024 * 4);
}

}  // namespace
}  // namespace shashoku::raster::test
