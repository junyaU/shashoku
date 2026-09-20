#include "raster/glyph_source.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"
#include "text/font_store.hpp"
#include "text/freetype_glyph_source.hpp"
#include "text/test_fonts.hpp"

namespace shashoku::text {
namespace {

using assets::noto_sans;
using assets::noto_sans_jp_regular;
using raster::GlyphBitmap;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

bool has_ink(const GlyphBitmap& bitmap) {
  return std::ranges::any_of(bitmap.coverage, [](std::uint8_t value) { return value != 0; });
}

// 成功を確かめてビットマップを取り出す。
GlyphBitmap must_rasterize(raster::GlyphSource& glyphs, FontId font, GlyphId glyph_id,
                           float pixel_size, bool sideways = false) {
  Result<GlyphBitmap> result = glyphs.rasterize(font, glyph_id, pixel_size, sideways);
  if (!result) {
    ADD_FAILURE() << "rasterize() failed: " << to_string(result.error());
    return GlyphBitmap{};
  }
  EXPECT_EQ(result->coverage.size(), static_cast<std::size_t>(result->width) * result->height);
  return std::move(*result);
}

// 失敗を確かめてエラーを取り出す。
Error must_fail(raster::GlyphSource& glyphs, FontId font, GlyphId glyph_id, float pixel_size,
                bool sideways = false) {
  Result<GlyphBitmap> result = glyphs.rasterize(font, glyph_id, pixel_size, sideways);
  if (result) {
    ADD_FAILURE() << "rasterize() unexpectedly succeeded";
    return Error{};
  }
  return result.error();
}

TEST(TextGlyphSource, RasterizesAJapaneseGlyph) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  const GlyphBitmap bitmap = must_rasterize(glyphs, jp, store.glyph_for(jp, U'あ'), 32.0F);

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

  const GlyphBitmap small = must_rasterize(glyphs, jp, glyph, 32.0F);
  const GlyphBitmap large = must_rasterize(glyphs, jp, glyph, 64.0F);

  EXPECT_NEAR(static_cast<double>(large.width), static_cast<double>(small.width) * 2.0, 2.0);
  EXPECT_NEAR(static_cast<double>(large.height), static_cast<double>(small.height) * 2.0, 2.0);
  EXPECT_NEAR(static_cast<double>(large.top), static_cast<double>(small.top) * 2.0, 2.0);
}

TEST(TextGlyphSource, AcceptsFractionalPixelSizes) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'あ');

  const GlyphBitmap at32 = must_rasterize(glyphs, jp, glyph, 32.0F);
  const GlyphBitmap at32_5 = must_rasterize(glyphs, jp, glyph, 32.5F);
  const GlyphBitmap at33 = must_rasterize(glyphs, jp, glyph, 33.0F);

  EXPECT_TRUE(has_ink(at32_5));
  EXPECT_GE(at32_5.width, at32.width);
  EXPECT_LE(at32_5.width, at33.width);
  EXPECT_NE(at32.coverage, at32_5.coverage) << "26.6 の小数サイズが効いていない";
}

// 空白は「描くものがないグリフ」。成功して空のビットマップになる（失敗ではない）。
TEST(TextGlyphSource, BlankGlyphIsASuccessfulEmptyBitmap) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  for (const char32_t cp : {U' ', U'　'}) {
    const GlyphId glyph = store.glyph_for(jp, cp);
    ASSERT_NE(glyph, 0U) << "テスト用フォントに U+" << static_cast<std::uint32_t>(cp) << " がない";
    const Result<GlyphBitmap> space = glyphs.rasterize(jp, glyph, 32.0F, false);
    ASSERT_TRUE(space.has_value()) << to_string(space.error());
    EXPECT_EQ(space->width, 0U);
    EXPECT_EQ(space->height, 0U);
    EXPECT_TRUE(space->coverage.empty());
  }
}

// 1/64 px 未満は 26.6 で 0 になる。この解像度では描くものがないので成功（空）とする。
TEST(TextGlyphSource, SizeBelowOneSixtyFourthOfAPixelIsEmpty) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  const GlyphBitmap tiny = must_rasterize(glyphs, jp, store.glyph_for(jp, U'あ'), 0.001F);
  EXPECT_EQ(tiny.width, 0U);
  EXPECT_EQ(tiny.height, 0U);
  EXPECT_TRUE(tiny.coverage.empty());
}

TEST(TextGlyphSource, IsDeterministic) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'永');

  const GlyphBitmap first = must_rasterize(glyphs, jp, glyph, 24.0F);
  // 別のグリフやサイズを挟んでも結果が変わらないこと（FT_Face の状態に引きずられない）
  const GlyphBitmap between = must_rasterize(glyphs, jp, store.glyph_for(jp, U'あ'), 48.0F, true);
  ASSERT_TRUE(has_ink(between));
  const GlyphBitmap second = must_rasterize(glyphs, jp, glyph, 24.0F);

  EXPECT_EQ(first, second);

  FreeTypeGlyphSource other(store);
  EXPECT_EQ(first, must_rasterize(other, jp, glyph, 24.0F));
}

TEST(TextGlyphSource, SidewaysSwapsWidthAndHeight) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(latin, U'A');

  const GlyphBitmap upright = must_rasterize(glyphs, latin, glyph, 40.0F, false);
  const GlyphBitmap rotated = must_rasterize(glyphs, latin, glyph, 40.0F, true);

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

  const GlyphBitmap upright = must_rasterize(glyphs, latin, glyph, 40.0F, false);
  const GlyphBitmap rotated = must_rasterize(glyphs, latin, glyph, 40.0F, true);
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

// issue #3: 失敗を空白として返さない。まず「不正な FontId」= 呼び出し側のバグ。
TEST(TextGlyphSource, InvalidFontIdIsInternalError) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  FreeTypeGlyphSource glyphs(store);

  const Error e = must_fail(glyphs, 99, 1, 32.0F);
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_NE(e.message.find("FontId 99"), std::string::npos) << e.message;
}

TEST(TextGlyphSource, WithoutFontsEveryGlyphIsInternalError) {
  const FontStore store;
  FreeTypeGlyphSource glyphs(store);
  EXPECT_EQ(must_fail(glyphs, 0, 0, 32.0F).kind, ErrorKind::Internal);
}

// フォントが持っていないグリフ ID は FT_Load_Glyph が弾く → FontLoad。
TEST(TextGlyphSource, GlyphIdOutOfRangeIsFontLoadError) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  const Error e = must_fail(glyphs, jp, 65535, 32.0F);
  EXPECT_EQ(e.kind, ErrorKind::FontLoad);
  EXPECT_NE(e.message.find("FT_Load_Glyph"), std::string::npos) << e.message;
  EXPECT_NE(e.message.find("glyph 65535"), std::string::npos) << e.message;
}

// pixel_size の契約違反（glyph_source.hpp）は呼び出し側のバグなので Internal。
TEST(TextGlyphSource, NonPositiveOrNonFinitePixelSizeIsInternalError) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'あ');

  for (const float size : {0.0F, -8.0F, kNaN, kInf, -kInf}) {
    EXPECT_EQ(must_fail(glyphs, jp, glyph, size).kind, ErrorKind::Internal) << "size " << size;
  }
  // 26.6 に収まらない巨大なサイズは FreeType に渡せない
  EXPECT_EQ(must_fail(glyphs, jp, glyph, 1.0e30F).kind, ErrorKind::FontLoad);
}

// 結線の確認: ディスプレイリストの DrawGlyphs に不正な FontId が載っていたら、
// ラスタライズ全体が Internal エラーで落ちる（黙って空白の PNG を返さない。issue #3）。
TEST(TextGlyphSource, InvalidFontIdInADrawGlyphsCommandFailsTheRasterization) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  FreeTypeGlyphSource glyphs(store);

  raster::DrawGlyphs cmd;
  cmd.font = 99;  // FontStore にない
  cmd.size = 16;
  cmd.color = kBlack;
  cmd.glyphs = {raster::GlyphInstance{1, Point{0, 16}}};
  const raster::Target target{.width = 32, .height = 32, .scale = 1, .background = kTransparent};

  const Result<Bitmap> result = raster::rasterize({cmd}, target, glyphs);
  ASSERT_FALSE(result.has_value()) << "不正な FontId が「成功・空白」になっている";
  EXPECT_EQ(result.error().kind, ErrorKind::Internal);
}

// ---------------------------------------------------------------------------
// 実行ごとのグリフキャッシュ（ARCHITECTURE.md A33）
// ---------------------------------------------------------------------------

// キャッシュは純粋な写像のメモ化なので、**あってもなくても結果は 1 ビットも変わらない**。
TEST(TextGlyphCache, ResultsAreIdenticalWithAndWithoutTheCache) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  const FontId latin = *store.load(noto_sans());

  FreeTypeGlyphSource cached(store);
  FreeTypeGlyphSource uncached(store, 0);  // 容量 0 = 一切メモしない

  const std::u32string_view text = U"あいうえおあいうえお永永永ABCABC";
  for (const float size : {12.0F, 24.0F, 24.0F, 33.5F}) {
    for (const bool sideways : {false, true}) {
      for (const char32_t cp : text) {
        const FontId font = store.has_glyph(jp, cp) ? jp : latin;
        const GlyphId glyph = store.glyph_for(font, cp);
        const Result<GlyphBitmap> a = cached.rasterize(font, glyph, size, sideways);
        const Result<GlyphBitmap> b = uncached.rasterize(font, glyph, size, sideways);
        ASSERT_TRUE(a.has_value()) << to_string(a.error());
        ASSERT_TRUE(b.has_value()) << to_string(b.error());
        EXPECT_EQ(*a, *b) << "U+" << static_cast<std::uint32_t>(cp) << " at " << size << "px";
      }
    }
  }

  EXPECT_GT(cached.cache_stats().hits, 0U) << "キャッシュが一度も当たっていない";
  EXPECT_EQ(uncached.cache_stats().hits, 0U);
  EXPECT_EQ(uncached.cache_stats().entries, 0U);
  EXPECT_EQ(uncached.cache_stats().bytes, 0U);
}

// 同じ鍵（FontId, glyph_id, pixel_size, sideways）の 2 回目以降は当たる。
TEST(TextGlyphCache, RepeatedRequestsHit) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId glyph = store.glyph_for(jp, U'永');

  const GlyphBitmap first = must_rasterize(glyphs, jp, glyph, 24.0F);
  EXPECT_EQ(glyphs.cache_stats().hits, 0U);
  EXPECT_EQ(glyphs.cache_stats().misses, 1U);

  EXPECT_EQ(must_rasterize(glyphs, jp, glyph, 24.0F), first);
  EXPECT_EQ(glyphs.cache_stats().hits, 1U);
  EXPECT_EQ(glyphs.cache_stats().misses, 1U);

  // 鍵の 4 要素はどれが違っても別項目になる。
  must_rasterize(glyphs, jp, glyph, 24.5F);                       // pixel_size が違う
  must_rasterize(glyphs, jp, glyph, 24.0F, true);                 // sideways が違う
  must_rasterize(glyphs, jp, store.glyph_for(jp, U'あ'), 24.0F);  // glyph_id が違う
  EXPECT_EQ(glyphs.cache_stats().hits, 1U);
  EXPECT_EQ(glyphs.cache_stats().misses, 4U);
  EXPECT_EQ(glyphs.cache_stats().entries, 4U);
}

// 空白グリフ（被覆率 0 バイト）も覚える。ただし鍵とヘッダの分は容量に数えるので、
// 項目数だけが無制限に増えることはない。
TEST(TextGlyphCache, BlankGlyphsAreRemembered) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);
  const GlyphId space = store.glyph_for(jp, U' ');

  must_rasterize(glyphs, jp, space, 24.0F);
  must_rasterize(glyphs, jp, space, 24.0F);
  EXPECT_EQ(glyphs.cache_stats().hits, 1U);
  EXPECT_EQ(glyphs.cache_stats().entries, 1U);
  EXPECT_GT(glyphs.cache_stats().bytes, 0U);
}

// 容量を超えたら**新規登録をやめるだけ**で、既にある項目は追い出さない（決定的）。
// 結果は容量に関係なく同じ。
TEST(TextGlyphCache, StopsRegisteringWhenFullAndNeverEvicts) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  // 24px の全角グリフ 1 個ぶんくらいしか入らない容量。
  FreeTypeGlyphSource small(store, 700);
  FreeTypeGlyphSource unlimited(store);

  const GlyphId first = store.glyph_for(jp, U'永');
  const GlyphBitmap kept = must_rasterize(small, jp, first, 24.0F);
  ASSERT_EQ(small.cache_stats().entries, 1U);
  const std::size_t bytes_after_first = small.cache_stats().bytes;

  for (const char32_t cp : std::u32string_view(U"あいうえおかきくけこ")) {
    const GlyphId glyph = store.glyph_for(jp, cp);
    EXPECT_EQ(must_rasterize(small, jp, glyph, 24.0F), must_rasterize(unlimited, jp, glyph, 24.0F));
  }
  // 入り切らなかったぶんは登録されないが、最初の 1 個は残り続ける。
  EXPECT_EQ(small.cache_stats().entries, 1U);
  EXPECT_EQ(small.cache_stats().bytes, bytes_after_first);
  EXPECT_EQ(must_rasterize(small, jp, first, 24.0F), kept);
  EXPECT_GT(small.cache_stats().hits, 0U);
  EXPECT_GT(unlimited.cache_stats().entries, 1U);
}

// エラーはキャッシュしない（キャッシュの有無でエラーの出方まで変えない）。
TEST(TextGlyphCache, FailuresAreNotCachedAndKeepTheirErrorKind) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(must_fail(glyphs, jp, 65535, 32.0F).kind, ErrorKind::FontLoad);
    EXPECT_EQ(must_fail(glyphs, 99, 1, 32.0F).kind, ErrorKind::Internal);
    EXPECT_EQ(must_fail(glyphs, jp, 1, 0.0F).kind, ErrorKind::Internal);
  }
  EXPECT_EQ(glyphs.cache_stats().entries, 0U);
  EXPECT_EQ(glyphs.cache_stats().hits, 0U);
}

// 逆に、空白だけのディスプレイリストは従来どおり成功する（空白は失敗ではない）。
TEST(TextGlyphSource, BlankGlyphsInADrawGlyphsCommandStillSucceed) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  FreeTypeGlyphSource glyphs(store);

  raster::DrawGlyphs cmd;
  cmd.font = jp;
  cmd.size = 16;
  cmd.color = kBlack;
  cmd.glyphs = {raster::GlyphInstance{store.glyph_for(jp, U' '), Point{0, 16}},
                raster::GlyphInstance{store.glyph_for(jp, U'　'), Point{8, 16}}};
  const raster::Target target{.width = 32, .height = 32, .scale = 1, .background = kTransparent};

  const Result<Bitmap> result = raster::rasterize({cmd}, target, glyphs);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  for (const std::uint8_t value : result->rgba) {
    EXPECT_EQ(value, 0U) << "空白グリフが何か描いている";
  }
}

}  // namespace
}  // namespace shashoku::text
