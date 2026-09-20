#include "raster/raster_test_util.hpp"

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster::test {
namespace {

std::string describe(Color c) {
  return "(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b) +
         ", " + std::to_string(c.a) + ")";
}

}  // namespace

Color rgba(int r, int g, int b, int a) {
  return Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
               static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
}

void FakeGlyphSource::set(GlyphId glyph_id, GlyphBitmap bitmap) {
  glyphs_[glyph_id] = std::move(bitmap);
}

GlyphBitmap FakeGlyphSource::rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                       bool sideways) {
  calls_.push_back(Call{font, glyph_id, pixel_size, sideways});
  const auto it = glyphs_.find(glyph_id);
  if (it == glyphs_.end()) {
    return GlyphBitmap{};  // 未登録は空白グリフ扱い
  }
  return it->second;
}

GlyphBitmap solid_glyph(std::int32_t left, std::int32_t top, std::uint32_t width,
                        std::uint32_t height, std::uint8_t coverage) {
  GlyphBitmap bitmap;
  bitmap.left = left;
  bitmap.top = top;
  bitmap.width = width;
  bitmap.height = height;
  bitmap.coverage.assign(static_cast<std::size_t>(width) * height, coverage);
  return bitmap;
}

GlyphBitmap glyph_rows(std::int32_t left, std::int32_t top,
                       std::initializer_list<std::vector<std::uint8_t>> rows) {
  GlyphBitmap bitmap;
  bitmap.left = left;
  bitmap.top = top;
  bitmap.height = static_cast<std::uint32_t>(rows.size());
  bitmap.width = rows.size() == 0 ? 0U : static_cast<std::uint32_t>(rows.begin()->size());
  for (const std::vector<std::uint8_t>& row : rows) {
    EXPECT_EQ(row.size(), bitmap.width) << "glyph_rows: 行の長さが揃っていない";
    bitmap.coverage.insert(bitmap.coverage.end(), row.begin(), row.end());
  }
  return bitmap;
}

Bitmap make_image(std::uint32_t width, std::uint32_t height, const std::vector<Color>& pixels) {
  EXPECT_EQ(pixels.size(), static_cast<std::size_t>(width) * height);
  Bitmap bitmap(width, height);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      bitmap.set_pixel(x, y, pixels[(static_cast<std::size_t>(y) * width) + x]);
    }
  }
  return bitmap;
}

Bitmap must_rasterize(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                      std::span<const Bitmap> images) {
  Result<Bitmap> result = rasterize(list, target, glyphs, images);
  if (!result) {
    ADD_FAILURE() << "rasterize() failed: " << to_string(result.error());
    return Bitmap{};
  }
  return std::move(result).value();
}

Bitmap must_rasterize(const DisplayList& list, const Target& target) {
  FakeGlyphSource glyphs;
  return must_rasterize(list, target, glyphs);
}

Error must_fail(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                std::span<const Bitmap> images) {
  Result<Bitmap> result = rasterize(list, target, glyphs, images);
  if (result) {
    ADD_FAILURE() << "rasterize() unexpectedly succeeded";
    return Error{};
  }
  return result.error();
}

Error must_fail(const DisplayList& list, const Target& target) {
  FakeGlyphSource glyphs;
  return must_fail(list, target, glyphs);
}

testing::AssertionResult pixel_is(const Bitmap& bitmap, std::uint32_t x, std::uint32_t y,
                                  Color want) {
  if (x >= bitmap.width || y >= bitmap.height) {
    return testing::AssertionFailure() << "pixel (" << x << ", " << y << ") is outside the "
                                       << bitmap.width << "x" << bitmap.height << " bitmap";
  }
  const Color got = bitmap.pixel(x, y);
  if (got == want) {
    return testing::AssertionSuccess();
  }
  return testing::AssertionFailure()
         << "pixel (" << x << ", " << y << ") is " << describe(got) << ", want " << describe(want);
}

double coverage_sum(const Bitmap& bitmap) {
  double total = 0;
  for (std::size_t i = 3; i < bitmap.rgba.size(); i += 4) {
    total += static_cast<double>(bitmap.rgba[i]);
  }
  return total / 255.0;
}

}  // namespace shashoku::raster::test
