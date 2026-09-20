#include "core/bitmap.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"

namespace shashoku {
namespace {

TEST(Bitmap, DefaultIsEmpty) {
  const Bitmap bitmap;
  EXPECT_EQ(bitmap.width, 0U);
  EXPECT_EQ(bitmap.height, 0U);
  EXPECT_TRUE(bitmap.rgba.empty());
}

TEST(Bitmap, ZeroSizeAllocatesNothing) {
  const Bitmap bitmap(0U, 0U, kWhite);
  EXPECT_TRUE(bitmap.rgba.empty());
}

// rgba.size() == width * height * 4 が不変条件
TEST(Bitmap, AllocatesFourBytesPerPixel) {
  const Bitmap bitmap(3U, 2U);
  EXPECT_EQ(bitmap.rgba.size(), 3U * 2U * 4U);
}

TEST(Bitmap, DefaultFillIsTransparent) {
  const Bitmap bitmap(2U, 2U);
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    for (std::uint32_t x = 0; x < bitmap.width; ++x) {
      EXPECT_EQ(bitmap.pixel(x, y), kTransparent);
    }
  }
}

TEST(Bitmap, FillsEveryPixel) {
  const Bitmap bitmap(2U, 3U, kWhite);
  EXPECT_EQ(bitmap.rgba, std::vector<std::uint8_t>(std::size_t{2} * 3 * 4, 255));
}

// バイトの並びは R, G, B, A（ストレートアルファ）
TEST(Bitmap, StoresChannelsInRgbaOrder) {
  const Bitmap bitmap(1U, 1U, Color{1, 2, 3, 4});
  EXPECT_EQ(bitmap.rgba, (std::vector<std::uint8_t>{1, 2, 3, 4}));
}

TEST(Bitmap, OffsetIsRowMajor) {
  const Bitmap bitmap(4U, 3U);
  EXPECT_EQ(bitmap.offset(0U, 0U), 0U);
  EXPECT_EQ(bitmap.offset(1U, 0U), 4U);
  EXPECT_EQ(bitmap.offset(0U, 1U), 16U);
  EXPECT_EQ(bitmap.offset(3U, 2U), ((2U * 4U) + 3U) * 4U);
}

TEST(Bitmap, PixelRoundTrip) {
  Bitmap bitmap(3U, 2U);
  std::uint8_t n = 0;
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    for (std::uint32_t x = 0; x < bitmap.width; ++x) {
      bitmap.set_pixel(x, y,
                       Color{n, static_cast<std::uint8_t>(n + 1U),
                             static_cast<std::uint8_t>(n + 2U), static_cast<std::uint8_t>(n + 3U)});
      n = static_cast<std::uint8_t>(n + 10U);
    }
  }

  n = 0;
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    for (std::uint32_t x = 0; x < bitmap.width; ++x) {
      EXPECT_EQ(bitmap.pixel(x, y),
                (Color{n, static_cast<std::uint8_t>(n + 1U), static_cast<std::uint8_t>(n + 2U),
                       static_cast<std::uint8_t>(n + 3U)}));
      n = static_cast<std::uint8_t>(n + 10U);
    }
  }
}

TEST(Bitmap, SetPixelTouchesOnlyOnePixel) {
  Bitmap bitmap(2U, 2U, kWhite);
  bitmap.set_pixel(1U, 0U, kBlack);
  EXPECT_EQ(bitmap.pixel(0U, 0U), kWhite);
  EXPECT_EQ(bitmap.pixel(1U, 0U), kBlack);
  EXPECT_EQ(bitmap.pixel(0U, 1U), kWhite);
  EXPECT_EQ(bitmap.pixel(1U, 1U), kWhite);
}

TEST(Bitmap, EqualityComparesSizeAndPixels) {
  const Bitmap a(2U, 2U, kWhite);
  Bitmap b(2U, 2U, kWhite);
  EXPECT_EQ(a, b);

  b.set_pixel(0U, 0U, kBlack);
  EXPECT_NE(a, b);

  EXPECT_NE(Bitmap(2U, 1U, kWhite), Bitmap(1U, 2U, kWhite));
}

TEST(Color, Defaults) {
  const Color color;
  EXPECT_EQ(color.r, 0U);
  EXPECT_EQ(color.g, 0U);
  EXPECT_EQ(color.b, 0U);
  EXPECT_EQ(color.a, 255U);
  EXPECT_FALSE(color.transparent());
}

TEST(Color, TransparentOnlyChecksAlpha) {
  EXPECT_TRUE(kTransparent.transparent());
  EXPECT_TRUE((Color{255, 255, 255, 0}.transparent()));
  EXPECT_FALSE((Color{0, 0, 0, 1}.transparent()));
  EXPECT_FALSE(kBlack.transparent());
}

TEST(Color, Constants) {
  EXPECT_EQ(kBlack, (Color{0, 0, 0, 255}));
  EXPECT_EQ(kWhite, (Color{255, 255, 255, 255}));
  EXPECT_EQ(kTransparent, (Color{0, 0, 0, 0}));
}

}  // namespace
}  // namespace shashoku
