// encode → decode がもとの Bitmap に戻ること（ARCHITECTURE.md §3.2 の受け入れ条件）。

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/png.hpp"
#include "png/png_test_util.hpp"
#include "shashoku/error.hpp"

namespace shashoku::png {
namespace {

void expect_round_trip(const Bitmap& original, const char* what) {
  const Result<std::vector<std::uint8_t>> encoded = encode(original);
  ASSERT_TRUE(encoded.has_value())
      << what << ": " << (encoded.has_value() ? std::string{} : to_string(encoded.error()));
  const Result<Bitmap> decoded = decode(*encoded);
  ASSERT_TRUE(decoded.has_value())
      << what << ": " << (decoded.has_value() ? std::string{} : to_string(decoded.error()));
  EXPECT_EQ(decoded->width, original.width) << what;
  EXPECT_EQ(decoded->height, original.height) << what;
  EXPECT_EQ(decoded->rgba, original.rgba) << what;
}

TEST(PngRoundTrip, SinglePixel) {
  expect_round_trip(test::make_solid(1, 1, Color{1, 2, 3, 4}), "1x1");
}

TEST(PngRoundTrip, Solid) {
  expect_round_trip(test::make_solid(64, 32, Color{0x33, 0x66, 0x99, 0xFF}), "solid opaque");
  expect_round_trip(test::make_solid(64, 32, kTransparent), "solid transparent");
  expect_round_trip(test::make_solid(64, 32, kWhite), "solid white");
  expect_round_trip(test::make_solid(64, 32, kBlack), "solid black");
}

TEST(PngRoundTrip, Gradient) { expect_round_trip(test::make_gradient(97, 61), "gradient"); }

TEST(PngRoundTrip, OddSizes) {
  expect_round_trip(test::make_gradient(1, 17), "1x17");
  expect_round_trip(test::make_gradient(17, 1), "17x1");
  expect_round_trip(test::make_gradient(3, 7), "3x7");
  expect_round_trip(test::make_noise(13, 29, 11), "13x29 noise");
}

TEST(PngRoundTrip, PartiallyTransparent) {
  Bitmap bitmap(31, 19);
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    for (std::uint32_t x = 0; x < bitmap.width; ++x) {
      bitmap.set_pixel(x, y,
                       Color{static_cast<std::uint8_t>(x * 8), static_cast<std::uint8_t>(y * 12),
                             0x40, static_cast<std::uint8_t>((x + y) % 2 == 0 ? 0 : 255)});
    }
  }
  expect_round_trip(bitmap, "checkerboard alpha");

  // 完全に透明なピクセルでも RGB は保たれる（PNG はストレートアルファをそのまま書く）
  const Bitmap colored_transparent(4, 4, Color{200, 100, 50, 0});
  expect_round_trip(colored_transparent, "transparent with colour");
}

TEST(PngRoundTrip, Noise) {
  for (std::uint64_t seed = 1; seed <= 8; ++seed) {
    expect_round_trip(test::make_noise(37, 23, seed), "noise");
  }
}

TEST(PngRoundTrip, TallAndWide) {
  expect_round_trip(test::make_gradient(1, 512), "1x512");
  expect_round_trip(test::make_gradient(512, 1), "512x1");
}

// 圧縮レベルは「どれだけ縮めるか」だけを変える。0〜9 のどれでも元の Bitmap に戻る
// （ARCHITECTURE.md A32。受け入れ条件「どのレベルでも encode → decode が元に戻る」）。
TEST(PngRoundTrip, EveryCompressionLevel) {
  const std::vector<Bitmap> images{
      test::make_solid(1, 1, Color{9, 8, 7, 6}),
      test::make_solid(48, 32, Color{0x33, 0x66, 0x99, 0x80}),
      test::make_gradient(97, 61),
      test::make_noise(64, 40, 20260920),
  };
  for (const Bitmap& image : images) {
    for (int level = 0; level <= 9; ++level) {
      const Result<std::vector<std::uint8_t>> encoded = encode(image, level);
      ASSERT_TRUE(encoded.has_value())
          << "level " << level << ": "
          << (encoded.has_value() ? std::string{} : to_string(encoded.error()));
      const Result<Bitmap> decoded = decode(*encoded);
      ASSERT_TRUE(decoded.has_value())
          << "level " << level << ": "
          << (decoded.has_value() ? std::string{} : to_string(decoded.error()));
      EXPECT_EQ(decoded->width, image.width) << "level " << level;
      EXPECT_EQ(decoded->height, image.height) << "level " << level;
      EXPECT_EQ(decoded->rgba, image.rgba) << "level " << level;
    }
  }
}

TEST(PngRoundTrip, EveryByteValueSurvives) {
  // 256 通りのバイト値がすべて素通しされることを 1 枚で確かめる
  Bitmap bitmap(256, 1);
  for (std::uint32_t x = 0; x < 256; ++x) {
    const auto v = static_cast<std::uint8_t>(x);
    bitmap.set_pixel(x, 0, Color{v, static_cast<std::uint8_t>(255 - x), v, v});
  }
  expect_round_trip(bitmap, "all byte values");
}

}  // namespace
}  // namespace shashoku::png
