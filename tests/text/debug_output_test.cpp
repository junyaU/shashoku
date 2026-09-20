#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "raster/glyph_source.hpp"
#include "shashoku/error.hpp"
#include "text/font_store.hpp"
#include "text/freetype_glyph_source.hpp"
#include "text/shaped_text_checks.hpp"
#include "text/shaper.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

// 目視確認用のデバッグ出力。raster / png モジュールはまだ無いので、被覆率をそのまま
// PGM（P2・テキスト形式）で SHASHOKU_TEST_OUTPUT_DIR に書き出す。合否には使わない。

namespace shashoku::text {
namespace {

using assets::noto_sans;
using assets::noto_sans_jp_regular;

struct GrayImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;

  [[nodiscard]] std::size_t offset(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
  }

  void blend(int x, int y, std::uint8_t coverage) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return;
    }
    std::uint8_t& target = pixels[offset(x, y)];
    target = std::max(target, coverage);
  }
};

// ペン位置 (pen_x, pen_y) からグリフを並べる。原点はデバイスピクセルの整数に丸める（A8）。
void draw(GrayImage& image, const ShapedText& shaped, raster::GlyphSource& glyphs, float pen_x,
          float pen_y, float pixel_size, bool vertical) {
  for (const ShapedGlyph& glyph : shaped.glyphs) {
    const auto origin_x = static_cast<int>(std::lround(pen_x + glyph.x_offset));
    const auto origin_y = static_cast<int>(std::lround(pen_y + glyph.y_offset));
    const Result<raster::GlyphBitmap> bitmap =
        glyphs.rasterize(glyph.font, glyph.glyph_id, pixel_size, glyph.sideways);
    ASSERT_TRUE(bitmap.has_value()) << to_string(bitmap.error());
    for (std::uint32_t row = 0; row < bitmap->height; ++row) {
      for (std::uint32_t column = 0; column < bitmap->width; ++column) {
        image.blend(origin_x + bitmap->left + static_cast<int>(column),
                    origin_y - bitmap->top + static_cast<int>(row),
                    bitmap->coverage[static_cast<std::size_t>(row) * bitmap->width + column]);
      }
    }
    if (vertical) {
      pen_y += glyph.advance;
    } else {
      pen_x += glyph.advance;
    }
  }
}

void write_pgm(const GrayImage& image, const std::string& name) {
  const std::filesystem::path directory(SHASHOKU_TEST_OUTPUT_DIR);
  std::error_code ignored;
  std::filesystem::create_directories(directory, ignored);

  std::ofstream out(directory / name);
  if (!out) {
    return;
  }
  out << "P2\n" << image.width << " " << image.height << "\n255\n";
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      // 白地に黒で描く（被覆率の反転）
      out << (255 - image.pixels[image.offset(x, y)]) << " ";
    }
    out << "\n";
  }
}

TEST(TextDebugOutput, WritesAHorizontalLine) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);
  FreeTypeGlyphSource glyphs(store);

  TextStyle style;
  style.font_size = 24.0F;
  const ShapedText shaped = shape_ok(shaper, U"こんにちは、世界のみんな。ABC😀", style);

  GrayImage image{520, 40, std::vector<std::uint8_t>(520UL * 40)};
  draw(image, shaped, glyphs, 4.0F, 28.0F, style.font_size, false);
  write_pgm(image, "text_horizontal.pgm");

  SUCCEED();
}

TEST(TextDebugOutput, WritesAVerticalLine) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);
  FreeTypeGlyphSource glyphs(store);

  TextStyle style;
  style.font_size = 24.0F;
  style.direction = Direction::Vertical;
  const ShapedText shaped = shape_ok(shaper, U"「縦書き」のテスト（ABC）ー。", style);

  GrayImage image{40, 420, std::vector<std::uint8_t>(40UL * 420)};
  draw(image, shaped, glyphs, 20.0F, 6.0F, style.font_size, true);
  write_pgm(image, "text_vertical.pgm");

  SUCCEED();
}

}  // namespace
}  // namespace shashoku::text
