#include "raster/glyph_source.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <gtest/gtest.h>

#include "core/ids.hpp"
#include "text/font_store.hpp"
#include "text/freetype_glyph_source.hpp"
#include "text/test_fonts.hpp"

namespace shashoku::text {
namespace {

using assets::noto_sans;
using assets::noto_sans_jp_regular;
using raster::GlyphBitmap;

bool has_ink(const GlyphBitmap& bitmap) {
  return std::ranges::any_of(bitmap.coverage, [](std::uint8_t value) { return value != 0; });
}

TEST(TextGlyphSource, RasterizesAJapaneseGlyph) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  const GlyphBitmap bitmap = glyphs.rasterize(jp, store.glyph_for(jp, U'あ'), 32.0F, false);

  EXPECT_EQ(bitmap.coverage.size(), static_cast<std::size_t>(bitmap.width) * bitmap.height);
  EXPECT_TRUE(has_ink(bitmap));
  // 32px の全角グリフは 32px 四方に収まるあたりの大きさになる
  EXPECT_GE(bitmap.width, 16U);
  EXPECT_LE(bitmap.width, 40U);
  EXPECT_GE(bitmap.height, 16U);
  EXPECT_LE(bitmap.height, 40U);
  // 字面はベースラインより上（top が正）で、左端はほぼ原点
  EXPECT_GT(bitmap.top, 0);
  EXPECT_GE(bitmap.left, -4);
  EXPECT_LE(bitmap.left, 8);
}

TEST(TextGlyphSource, SizeScalesTheBitmap) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'あ');

  const GlyphBitmap small = glyphs.rasterize(jp, glyph, 32.0F, false);
  const GlyphBitmap large = glyphs.rasterize(jp, glyph, 64.0F, false);

  EXPECT_NEAR(static_cast<double>(large.width), static_cast<double>(small.width) * 2.0, 2.0);
  EXPECT_NEAR(static_cast<double>(large.height), static_cast<double>(small.height) * 2.0, 2.0);
  EXPECT_NEAR(static_cast<double>(large.top), static_cast<double>(small.top) * 2.0, 2.0);
}

TEST(TextGlyphSource, AcceptsFractionalPixelSizes) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'あ');

  const GlyphBitmap at32 = glyphs.rasterize(jp, glyph, 32.0F, false);
  const GlyphBitmap at32_5 = glyphs.rasterize(jp, glyph, 32.5F, false);
  const GlyphBitmap at33 = glyphs.rasterize(jp, glyph, 33.0F, false);

  EXPECT_TRUE(has_ink(at32_5));
  EXPECT_GE(at32_5.width, at32.width);
  EXPECT_LE(at32_5.width, at33.width);
  EXPECT_NE(at32.coverage, at32_5.coverage) << "26.6 の小数サイズが効いていない";
}

TEST(TextGlyphSource, BlankGlyphHasEmptyBitmap) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  const GlyphBitmap space = glyphs.rasterize(jp, store.glyph_for(jp, U' '), 32.0F, false);
  EXPECT_EQ(space.width, 0U);
  EXPECT_EQ(space.height, 0U);
  EXPECT_TRUE(space.coverage.empty());
}

TEST(TextGlyphSource, IsDeterministic) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'永');

  const GlyphBitmap first = glyphs.rasterize(jp, glyph, 24.0F, false);
  // 別のグリフやサイズを挟んでも結果が変わらないこと（FT_Face の状態に引きずられない）
  (void)glyphs.rasterize(jp, store.glyph_for(jp, U'あ'), 48.0F, true);
  const GlyphBitmap second = glyphs.rasterize(jp, glyph, 24.0F, false);

  EXPECT_EQ(first, second);

  FreeTypeGlyphSource other(store);
  EXPECT_EQ(first, other.rasterize(jp, glyph, 24.0F, false));
}

TEST(TextGlyphSource, SidewaysSwapsWidthAndHeight) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(latin, U'A');

  const GlyphBitmap upright = glyphs.rasterize(latin, glyph, 40.0F, false);
  const GlyphBitmap rotated = glyphs.rasterize(latin, glyph, 40.0F, true);

  ASSERT_TRUE(has_ink(upright));
  ASSERT_TRUE(has_ink(rotated));
  EXPECT_NE(upright.width, upright.height) << "縦横が同じだと入れ替わりを検出できない";
  EXPECT_EQ(rotated.width, upright.height);
  EXPECT_EQ(rotated.height, upright.width);

  // 時計回りに 90° 回すと、ベースラインより上（top > 0）の字面が原点の右（left >= 0）に来る。
  EXPECT_GE(rotated.left, 0);
  EXPECT_LE(std::abs(rotated.top), 2);
}

TEST(TextGlyphSource, RotationIsAQuarterTurnOfThePixels) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(latin, U'L');  // 回転が分かりやすい非対称なグリフ

  const GlyphBitmap upright = glyphs.rasterize(latin, glyph, 40.0F, false);
  const GlyphBitmap rotated = glyphs.rasterize(latin, glyph, 40.0F, true);
  ASSERT_EQ(rotated.width, upright.height);
  ASSERT_EQ(rotated.height, upright.width);

  // 時計回り 90°: 元の (x, y) が (height-1-y, x) に来る。
  // アンチエイリアスの丸めで完全一致はしないので、ざっくり被覆率の合計で見る。
  std::uint64_t upright_sum = 0;
  for (const std::uint8_t value : upright.coverage) {
    upright_sum += value;
  }
  std::uint64_t rotated_sum = 0;
  for (const std::uint8_t value : rotated.coverage) {
    rotated_sum += value;
  }
  ASSERT_GT(upright_sum, 0U);
  const double ratio = static_cast<double>(rotated_sum) / static_cast<double>(upright_sum);
  EXPECT_NEAR(ratio, 1.0, 0.05);
}

TEST(TextGlyphSource, InvalidArgumentsGiveEmptyBitmaps) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  EXPECT_TRUE(glyphs.rasterize(99, 1, 32.0F, false).coverage.empty());
  EXPECT_TRUE(glyphs.rasterize(jp, 65535, 32.0F, false).coverage.empty());
  EXPECT_TRUE(glyphs.rasterize(jp, store.glyph_for(jp, U'あ'), 0.0F, false).coverage.empty());
  EXPECT_TRUE(glyphs.rasterize(jp, store.glyph_for(jp, U'あ'), -8.0F, false).coverage.empty());
}

TEST(TextGlyphSource, WorksWithoutFonts) {
  const FontStore store;
  FreeTypeGlyphSource glyphs(store);
  EXPECT_TRUE(glyphs.rasterize(0, 0, 32.0F, false).coverage.empty());
}

}  // namespace
}  // namespace shashoku::text
