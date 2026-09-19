#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include "core/ids.hpp"
#include "text/char_properties.hpp"
#include "text/font_store.hpp"
#include "text/shaped_text_checks.hpp"
#include "text/shaper.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

// 縦書き（ARCHITECTURE.md §3.5）。UAX #50 の Vertical_Orientation で立てるか横倒しかを決め、
// 立てる run は HB_DIRECTION_TTB で shape して `vert` の縦書き用グリフを得る。

namespace shashoku::text {
namespace {

using assets::expect_valid_clusters;
using assets::noto_sans;
using assets::noto_sans_jp_regular;

constexpr float kSize = 32.0F;

TextStyle horizontal_style() {
  TextStyle style;
  style.font_size = kSize;
  return style;
}

TextStyle vertical_style() {
  TextStyle style = horizontal_style();
  style.direction = Direction::Vertical;
  return style;
}

GlyphId only_glyph(const ShapedText& shaped) {
  return shaped.glyphs.size() == 1 ? shaped.glyphs[0].glyph_id : 0;
}

TEST(TextVerticalOrientation, ClassifiesTheCharactersWeCareAbout) {
  // 立てる
  EXPECT_EQ(vertical_orientation(U'あ'), VerticalOrientation::Upright);
  EXPECT_EQ(vertical_orientation(U'漢'), VerticalOrientation::Upright);
  EXPECT_EQ(vertical_orientation(U'、'), VerticalOrientation::TransformedUpright);
  EXPECT_EQ(vertical_orientation(U'。'), VerticalOrientation::TransformedUpright);
  EXPECT_EQ(vertical_orientation(U'っ'), VerticalOrientation::TransformedUpright);
  // vert があれば立てて差し替え、無ければ横倒し
  EXPECT_EQ(vertical_orientation(U'ー'), VerticalOrientation::TransformedRotated);
  EXPECT_EQ(vertical_orientation(U'（'), VerticalOrientation::TransformedRotated);
  EXPECT_EQ(vertical_orientation(U'「'), VerticalOrientation::TransformedRotated);
  // 横倒し
  EXPECT_EQ(vertical_orientation(U'A'), VerticalOrientation::Rotated);
  EXPECT_EQ(vertical_orientation(U'1'), VerticalOrientation::Rotated);
  EXPECT_EQ(vertical_orientation(U'-'), VerticalOrientation::Rotated);
}

TEST(TextVertical, PunctuationGetsVerticalGlyphs) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  for (const char32_t cp : {U'。', U'、', U'ー', U'（', U'」'}) {
    const std::u32string text(1, cp);
    const ShapedText horizontal = shaper.shape(text, horizontal_style());
    const ShapedText vertical = shaper.shape(text, vertical_style());
    ASSERT_EQ(horizontal.glyphs.size(), 1U);
    ASSERT_EQ(vertical.glyphs.size(), 1U);
    EXPECT_NE(only_glyph(vertical), only_glyph(horizontal))
        << "vert が効いていない: U+" << static_cast<std::uint32_t>(cp);
    EXPECT_FALSE(vertical.glyphs[0].sideways);
  }
}

TEST(TextVertical, KanjiKeepsTheSameGlyph) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  // 漢字には縦書き用の別グリフが無いので、横書きと同じグリフが立つ。
  // （かなは Noto Sans CJK が vert で縦組み用の字形に差し替えるので、ここでは使わない）
  const ShapedText horizontal = shaper.shape(U"永", horizontal_style());
  const ShapedText vertical = shaper.shape(U"永", vertical_style());
  EXPECT_EQ(only_glyph(vertical), only_glyph(horizontal));
  EXPECT_FALSE(vertical.glyphs[0].sideways);
  EXPECT_NEAR(vertical.glyphs[0].advance, kSize, 0.02F);
}

TEST(TextVertical, LatinRunsAreLaidSideways) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"あABCい";
  const ShapedText shaped = shaper.shape(text, vertical_style());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.glyphs.size(), 5U);

  EXPECT_FALSE(shaped.glyphs[0].sideways);
  EXPECT_TRUE(shaped.glyphs[1].sideways);
  EXPECT_TRUE(shaped.glyphs[2].sideways);
  EXPECT_TRUE(shaped.glyphs[3].sideways);
  EXPECT_FALSE(shaped.glyphs[4].sideways);

  for (const ShapedGlyph& glyph : shaped.glyphs) {
    EXPECT_GT(glyph.advance, 0.0F) << "縦書きの送りは下向きが正";
  }
}

TEST(TextVertical, FullWidthAdvanceIsOneEm) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"吾輩は猫である。";
  const ShapedText shaped = shaper.shape(text, vertical_style());
  expect_valid_clusters(shaped, text.size());
  for (const ShapedCluster& cluster : shaped.clusters) {
    EXPECT_NEAR(cluster.advance, kSize, 0.02F);
  }
}

TEST(TextVertical, UprightPenSitsOnTheCentreAxis) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText shaped = shaper.shape(U"永", vertical_style());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  // 全角グリフの字面（1em）が中心軸の左右に半分ずつ出るので、原点は軸の 0.5em 左。
  EXPECT_NEAR(shaped.glyphs[0].x_offset, -kSize / 2.0F, 0.1F);
  // ベースラインはペン位置より下（ascent ぶん）。
  EXPECT_GT(shaped.glyphs[0].y_offset, kSize * 0.5F);
  EXPECT_LT(shaped.glyphs[0].y_offset, kSize * 1.2F);
}

TEST(TextVertical, SidewaysBaselineIsShiftedTowardsTheCentreAxis) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText shaped = shaper.shape(U"A", vertical_style());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_TRUE(shaped.glyphs[0].sideways);
  EXPECT_FLOAT_EQ(shaped.glyphs[0].y_offset, 0.0F);

  // 回転後は ascent 側が軸の右、descent 側が左に出るので、ベースラインは軸より左。
  const FontMetrics metrics = shaper.metrics(vertical_style());
  EXPECT_NEAR(shaped.glyphs[0].x_offset, (metrics.descent - metrics.ascent) / 2.0F, 0.02F);
  EXPECT_LT(shaped.glyphs[0].x_offset, 0.0F);
}

TEST(TextVertical, MissingGlyphsAreUpright) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"あ😀";
  const ShapedText shaped = shaper.shape(text, vertical_style());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.glyphs.size(), 2U);

  EXPECT_TRUE(shaped.clusters[1].missing);
  EXPECT_FALSE(shaped.glyphs[1].sideways);
  EXPECT_NEAR(shaped.glyphs[1].advance, kSize, 0.02F);
  EXPECT_NEAR(shaped.glyphs[1].x_offset, -kSize / 2.0F, 0.02F);
  EXPECT_GT(shaped.glyphs[1].y_offset, 0.0F);
}

// UAX #50 の Tr は「vert があれば差し替え、無ければ横倒し」。
// Noto Sans（欧文）は vert を持たないので、同じ Tr の文字でも横倒しに落ちる。
TEST(TextVertical, FallsBackSidewaysWhenTheFontHasNoVerticalForm) {
  constexpr char32_t kLeftQuote = 0x201C;  // “ は Tr
  ASSERT_EQ(vertical_orientation(kLeftQuote), VerticalOrientation::TransformedRotated);

  FontStore latin_only;
  ASSERT_TRUE(latin_only.load(noto_sans()).has_value());
  ASSERT_TRUE(latin_only.has_glyph(0, kLeftQuote));
  Shaper latin_shaper(latin_only);

  const ShapedText latin = latin_shaper.shape(std::u32string(1, kLeftQuote), vertical_style());
  ASSERT_EQ(latin.glyphs.size(), 1U);
  EXPECT_TRUE(latin.glyphs[0].sideways);

  // 同じ Tr でも、和文フォントが縦書き用グリフを持つ「 は立つ。
  // 立てるか倒すかはフォントが決める（UAX #50 の「transformed, fallback rotated」）。
  ASSERT_EQ(vertical_orientation(U'「'), VerticalOrientation::TransformedRotated);
  FontStore japanese;
  ASSERT_TRUE(japanese.load(noto_sans_jp_regular()).has_value());
  Shaper japanese_shaper(japanese);

  const ShapedText vertical = japanese_shaper.shape(U"「", vertical_style());
  ASSERT_EQ(vertical.glyphs.size(), 1U);
  EXPECT_FALSE(vertical.glyphs[0].sideways);

  // 和文フォントでも vert を持たない “ は横倒しになる。
  const ShapedText quote = japanese_shaper.shape(std::u32string(1, kLeftQuote), vertical_style());
  ASSERT_EQ(quote.glyphs.size(), 1U);
  EXPECT_TRUE(quote.glyphs[0].sideways);
}

TEST(TextVertical, IsDeterministic) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"「縦書き」のテスト（Vertical）ー 2026 年。";
  const ShapedText first = shaper.shape(text, vertical_style());
  expect_valid_clusters(first, text.size());

  Shaper other(store);
  const ShapedText second = other.shape(text, vertical_style());
  EXPECT_EQ(first.glyphs, second.glyphs);
  EXPECT_EQ(first.clusters, second.clusters);
}

}  // namespace
}  // namespace shashoku::text
