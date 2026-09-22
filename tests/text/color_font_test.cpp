// COLR / CPAL のカラーフォント（issue #27）。
// 「色データを持ち、かつ単色の輪郭が空」のグリフは単色では描けないので、豆腐に回す。
// 元の不具合: そのグリフは cmap にあるので missing にならず、ラスタライザは
// 「空白グリフ」として空のビットマップを返し、警告も豆腐も無いまま真っ白な PNG になっていた。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/glyph_source.hpp"
#include "shashoku/error.hpp"
#include "text/colr_test_font.hpp"
#include "text/font_store.hpp"
#include "text/freetype_glyph_source.hpp"
#include "text/shaped_text_checks.hpp"
#include "text/shaper.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {
namespace {

using assets::colr_font_empty_base;
using assets::colr_font_outlined_base;
using assets::expect_valid_clusters;
using assets::kColrBaseGlyph;
using assets::kColrCodepoint;
using assets::kColrFirstLayerGlyph;
using assets::kColrMissingCodepoint;
using assets::kColrNotdefGlyph;
using assets::kColrSecondLayerGlyph;
using assets::noto_sans;
using assets::noto_sans_jp_regular;
using raster::GlyphBitmap;

TextStyle style_at(float size = 64.0F) {
  TextStyle style;
  style.font_size = size;
  return style;
}

bool has_ink(const GlyphBitmap& bitmap) {
  return std::ranges::any_of(bitmap.coverage, [](std::uint8_t value) { return value != 0; });
}

// ShapedText が実際に描く塗りの量（豆腐が「見える」ことの確認）。
std::size_t total_ink(const ShapedText& shaped, raster::GlyphSource& glyphs, float pixel_size) {
  std::size_t ink = 0;
  for (const ShapedGlyph& glyph : shaped.glyphs) {
    Result<GlyphBitmap> bitmap = glyphs.rasterize(glyph.font, glyph.glyph_id, pixel_size, false);
    if (!bitmap) {
      ADD_FAILURE() << "rasterize() failed: " << to_string(bitmap.error());
      return 0;
    }
    ink += static_cast<std::size_t>(
        std::ranges::count_if(bitmap->coverage, [](std::uint8_t value) { return value != 0; }));
  }
  return ink;
}

// ---- テスト用フォントそのものの確認（バイト列ヘルパが壊れたらここで落ちる） ----

TEST(TextColorFont, BuildsAReadableColrFont) {
  EXPECT_EQ(colr_font_empty_base().size(), assets::kColrFontSize);
  // ベースに輪郭を持たせた版は、そのグリフのぶん（36 バイト）だけ大きい。
  EXPECT_EQ(colr_font_outlined_base().size(), assets::kColrFontSize + 36);

  FontStore store;
  const auto font = store.load(colr_font_empty_base());
  ASSERT_TRUE(font.has_value()) << to_string(font.error());

  // 輪郭フォントとして読めている（CBDT / sbix のように load で弾かれない）。
  EXPECT_EQ(store.units_per_em(*font), 1000);
  // cmap にあるのは 'A' だけ。'B' は従来どおりの豆腐の対照。
  EXPECT_EQ(store.glyph_for(*font, kColrCodepoint), kColrBaseGlyph);
  EXPECT_FALSE(store.has_glyph(*font, kColrMissingCodepoint));

  // グリフは 4 個（gid 3 まで描けて、gid 4 は FreeType が拒む）。
  FreeTypeGlyphSource glyphs(store);
  EXPECT_TRUE(glyphs.rasterize(*font, kColrSecondLayerGlyph, 64.0F, false).has_value());
  EXPECT_FALSE(glyphs.rasterize(*font, 4, 64.0F, false).has_value());
}

TEST(TextColorFont, TheColorLayersAndNotdefAreOrdinaryOutlines) {
  FontStore store;
  const FontId font = *store.load(colr_font_empty_base());
  FreeTypeGlyphSource glyphs(store);

  // .notdef と色レイヤー用のグリフは色データを持たないので、従来どおり描かれる。
  for (const GlyphId glyph : {kColrNotdefGlyph, kColrFirstLayerGlyph, kColrSecondLayerGlyph}) {
    Result<GlyphBitmap> bitmap = glyphs.rasterize(font, glyph, 64.0F, false);
    ASSERT_TRUE(bitmap.has_value()) << "gid " << glyph << ": " << to_string(bitmap.error());
    EXPECT_TRUE(has_ink(*bitmap)) << "gid " << glyph;
  }

  // 一方、COLR のベース（色レイヤーだけで絵を作るグリフ）の輪郭は空。
  // ラスタライザから見ると「空白グリフ」と区別がつかない（A19。だから text が判定する）。
  Result<GlyphBitmap> base = glyphs.rasterize(font, kColrBaseGlyph, 64.0F, false);
  ASSERT_TRUE(base.has_value()) << to_string(base.error());
  EXPECT_FALSE(has_ink(*base));
}

// ---- 本題: 色データだけのグリフは豆腐になる ----

TEST(TextColorFont, ColorOnlyGlyphsAreShapedAsTofu) {
  FontStore store;
  ASSERT_TRUE(store.load(colr_font_empty_base()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"AAA";
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 3U);

  for (const ShapedCluster& cluster : shaped.clusters) {
    EXPECT_TRUE(cluster.missing) << "色レイヤーしか無い 'A' は豆腐として報告される";
    EXPECT_EQ(cluster.missing_reason, MissingReason::ColorOnly);
    EXPECT_EQ(cluster.advance, 64.0F);  // 送りは 1em のまま（レイアウトは変わらない）
  }
  // 豆腐は □ か .notdef で描かれる。COLR のベース（空の輪郭）は選ばれない。
  for (const ShapedGlyph& glyph : shaped.glyphs) {
    EXPECT_NE(glyph.glyph_id, kColrBaseGlyph);
  }
}

TEST(TextColorFont, TofuForColorOnlyGlyphsIsActuallyDrawn) {
  FontStore store;
  const FontId font = *store.load(colr_font_empty_base());
  Shaper shaper(store);
  FreeTypeGlyphSource glyphs(store);

  const ShapedText shaped = shape_ok(shaper, U"AAA", style_at());
  // 元の不具合そのもの: ここが 0 だと「全ピクセルが透明な PNG」になる。
  EXPECT_GT(total_ink(shaped, glyphs, 64.0F), 0U);
  EXPECT_EQ(store.units_per_em(font), 1000);
}

// 従来の豆腐（cmap に無い文字）と挙動が変わらないこと。
TEST(TextColorFont, MatchesTheBehaviorOfACharacterMissingFromCmap) {
  FontStore store;
  ASSERT_TRUE(store.load(colr_font_empty_base()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"AB";  // 'A' は色データだけ、'B' は cmap に無い
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 2U);
  ASSERT_EQ(shaped.glyphs.size(), 2U);

  EXPECT_TRUE(shaped.clusters[0].missing);
  EXPECT_TRUE(shaped.clusters[1].missing);
  // 理由だけが違う（警告の文面がここで分かれる。組版は同じ）。
  EXPECT_EQ(shaped.clusters[0].missing_reason, MissingReason::ColorOnly);
  EXPECT_EQ(shaped.clusters[1].missing_reason, MissingReason::NotInAnyFont);
  // 同じ豆腐のグリフ・同じ送りで描かれる（どちらも 1em の □ / .notdef）。
  EXPECT_EQ(shaped.glyphs[0].font, shaped.glyphs[1].font);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, shaped.glyphs[1].glyph_id);
  EXPECT_EQ(shaped.clusters[0].advance, shaped.clusters[1].advance);
}

TEST(TextColorFont, VerticalWritingGetsTheSameTofu) {
  FontStore store;
  ASSERT_TRUE(store.load(colr_font_empty_base()).has_value());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.direction = Direction::Vertical;
  const std::u32string text = U"AAA";
  const ShapedText shaped = shape_ok(shaper, text, style);
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 3U);

  for (std::size_t i = 0; i < shaped.clusters.size(); ++i) {
    EXPECT_TRUE(shaped.clusters[i].missing) << "クラスタ " << i;
    EXPECT_EQ(shaped.clusters[i].advance, 64.0F) << "クラスタ " << i;
  }
  for (const ShapedGlyph& glyph : shaped.glyphs) {
    EXPECT_NE(glyph.glyph_id, kColrBaseGlyph);
    EXPECT_FALSE(glyph.sideways);           // 豆腐は縦書きでも立てる
    EXPECT_EQ(glyph.x_offset, -64.0F / 2);  // 中心軸の左右に 1em を半分ずつ
  }
}

// □（U+25A1）自体が色データだけのフォントでも、空白の豆腐を選ばない。
TEST(TextColorFont, DoesNotPickAColorOnlyGlyphAsTofu) {
  FontStore store;
  const FontId font = *store.load(assets::colr_font_with_color_only_tofu());
  Shaper shaper(store);
  FreeTypeGlyphSource glyphs(store);

  // このフォントでは □ も色データだけのベース（gid 1）に割り当てられている。
  EXPECT_EQ(store.glyph_for(font, assets::kColrTofuCodepoint), kColrBaseGlyph);
  EXPECT_FALSE(store.has_drawable_glyph(font, assets::kColrTofuCodepoint));

  const ShapedText shaped = shape_ok(shaper, U"AB", style_at());
  ASSERT_EQ(shaped.glyphs.size(), 2U);
  for (const ShapedGlyph& glyph : shaped.glyphs) {
    // □ が使えないので .notdef に落ちる。空の輪郭（gid 1）は選ばない。
    EXPECT_EQ(glyph.glyph_id, kColrNotdefGlyph);
  }
  EXPECT_GT(total_ink(shaped, glyphs, 64.0F), 0U);
}

// ---- 色データを持つが輪郭もあるグリフは、従来どおり単色で描く ----

TEST(TextColorFont, ColrBaseWithAnOutlineIsStillDrawn) {
  FontStore store;
  const FontId font = *store.load(colr_font_outlined_base());
  Shaper shaper(store);
  FreeTypeGlyphSource glyphs(store);

  // Segoe UI Emoji のように「ベースが輪郭を持つ」作りのフォント。色は落ちるが絵は出るので、
  // 豆腐にしない（この判定を誤ると、実在のカラー絵文字フォントの出力が変わってしまう）。
  EXPECT_FALSE(store.is_color_only_glyph(font, kColrBaseGlyph));
  EXPECT_TRUE(store.has_drawable_glyph(font, kColrCodepoint));

  const ShapedText shaped = shape_ok(shaper, U"A", style_at());
  ASSERT_EQ(shaped.clusters.size(), 1U);
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_FALSE(shaped.clusters[0].missing);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, kColrBaseGlyph);
  EXPECT_GT(total_ink(shaped, glyphs, 64.0F), 0U);
}

// ---- 判定そのもの（FontStore） ----

TEST(TextColorFont, DetectsOnlyTheColorOnlyGlyph) {
  FontStore store;
  const FontId font = *store.load(colr_font_empty_base());

  EXPECT_TRUE(store.is_color_only_glyph(font, kColrBaseGlyph));
  EXPECT_FALSE(store.is_color_only_glyph(font, kColrNotdefGlyph));
  EXPECT_FALSE(store.is_color_only_glyph(font, kColrFirstLayerGlyph));
  EXPECT_FALSE(store.is_color_only_glyph(font, kColrSecondLayerGlyph));
  EXPECT_FALSE(store.is_color_only_glyph(font, 4));  // 存在しないグリフ

  // cmap にあっても「単色では描けない」ので has_drawable_glyph は false。
  EXPECT_TRUE(store.has_glyph(font, kColrCodepoint));
  EXPECT_FALSE(store.has_drawable_glyph(font, kColrCodepoint));
  EXPECT_FALSE(store.has_drawable_glyph(font, kColrMissingCodepoint));

  // 不正な FontId でも落ちない（他の照会と同じ）。
  EXPECT_FALSE(store.is_color_only_glyph(99, kColrBaseGlyph));
  EXPECT_FALSE(store.has_drawable_glyph(99, kColrCodepoint));
  EXPECT_EQ(store.color_probe_count(99), 0U);
}

// COLR を持たないフォントでは、グリフごとの判定に **1 回も入らない**。
// （ここが 0 でなくなったら、ふつうのフォントの読み込みが遅くなっている）
TEST(TextColorFont, FontsWithoutColorTablesAreNeverProbed) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  const FontId latin = *store.load(noto_sans());
  const FontId colr = *store.load(colr_font_empty_base());

  EXPECT_EQ(store.color_probe_count(jp), 0U);
  EXPECT_EQ(store.color_probe_count(latin), 0U);
  EXPECT_EQ(store.color_probe_count(colr), 4U);  // COLR を持つフォントだけ全グリフを調べる

  // 従来のフォントでは判定が何も変わらない（has_glyph と同じ答え）。
  for (const char32_t cp : std::u32string(U"Aあ漢、0")) {
    EXPECT_EQ(store.has_drawable_glyph(jp, cp), store.has_glyph(jp, cp))
        << "コードポイント " << static_cast<std::uint32_t>(cp);
    EXPECT_EQ(store.has_drawable_glyph(latin, cp), store.has_glyph(latin, cp))
        << "コードポイント " << static_cast<std::uint32_t>(cp);
  }
}

// ---- フォールバック列の扱い ----

TEST(TextColorFont, FallsBackToTheNextFontThatCanDrawTheGlyph) {
  FontStore store;
  const FontId colr = *store.load(colr_font_empty_base());
  const FontId latin = *store.load(noto_sans());
  Shaper shaper(store);

  // 先頭の COLR フォントは 'A' を色データでしか持たないので、次の欧文フォントに送る。
  const ShapedText shaped = shape_ok(shaper, U"A", style_at());
  ASSERT_EQ(shaped.clusters.size(), 1U);
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_FALSE(shaped.clusters[0].missing);
  EXPECT_EQ(shaped.glyphs[0].font, latin);
  EXPECT_NE(shaped.glyphs[0].font, colr);
}

TEST(TextColorFont, DoesNotChangeTheOrderWhenTheFirstFontCanDraw) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  ASSERT_TRUE(store.load(colr_font_empty_base()).has_value());
  Shaper shaper(store);

  // フォールバック列の 2 番目に COLR フォントを置いても、1 番目の非カラーのフォントが優先される
  // （探索順を壊していないことの確認）。
  const ShapedText shaped = shape_ok(shaper, U"A", style_at());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_FALSE(shaped.clusters[0].missing);
  EXPECT_EQ(shaped.glyphs[0].font, latin);
}

}  // namespace
}  // namespace shashoku::text
