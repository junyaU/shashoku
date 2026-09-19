#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/ids.hpp"
#include "text/font_store.hpp"
#include "text/shaped_text_checks.hpp"
#include "text/shaper.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {
namespace {

using assets::expect_valid_clusters;
using assets::noto_sans;
using assets::noto_sans_jp_bold;
using assets::noto_sans_jp_regular;

// クラスタ i を描くのに使われたフォント。
FontId cluster_font(const ShapedText& shaped, std::size_t index) {
  const ShapedCluster& cluster = shaped.clusters[index];
  return cluster.glyph_begin < cluster.glyph_end ? shaped.glyphs[cluster.glyph_begin].font : 0;
}

TextStyle style_at(float size = 32.0F) {
  TextStyle style;
  style.font_size = size;
  return style;
}

TEST(TextFallback, FallsBackToTheNextFontThatHasTheGlyph) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  const std::u32string text = U"ABCあ";
  const ShapedText shaped = shaper.shape(text, style_at());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 4U);

  // 欧文は追加順で最初にグリフを持つ Noto Sans、和文だけ Noto Sans JP に落ちる
  EXPECT_EQ(cluster_font(shaped, 0), latin);
  EXPECT_EQ(cluster_font(shaped, 1), latin);
  EXPECT_EQ(cluster_font(shaped, 2), latin);
  EXPECT_EQ(cluster_font(shaped, 3), japanese);
  EXPECT_TRUE(shaper.take_missing_glyphs().empty());
}

TEST(TextFallback, FontFamilyChangesTheOrder) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans JP"};
  const std::u32string text = U"ABCあ";
  const ShapedText shaped = shaper.shape(text, style);
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 4U);

  // 指定したフォントが先頭に来るので、欧文もそちらで描かれる
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(cluster_font(shaped, i), japanese) << "クラスタ " << i;
  }
  EXPECT_NE(japanese, latin);
}

TEST(TextFallback, FamilyMatchingIgnoresCaseAndSurroundingSpaces) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"  nOtO sAnS jP  "};
  const ShapedText shaped = shaper.shape(U"A", style);
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(cluster_font(shaped, 0), japanese);
}

// A15: FontStore にない family 名は読み飛ばす（エラーにしない）。
TEST(TextFallback, UnknownFamilyNamesAreSkipped) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"No Such Font", "sans-serif", "Noto Sans JP"};
  const ShapedText shaped = shaper.shape(U"A", style);
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(cluster_font(shaped, 0), japanese);
}

TEST(TextFallback, AllUnknownFamiliesFallBackToLoadOrder) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"No Such Font", "serif"};
  const ShapedText shaped = shaper.shape(U"A", style);
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(cluster_font(shaped, 0), latin);
}

// CSS Fonts 4 §5.2: 同じ family に複数 weight があれば font-weight に最も近いものを選ぶ。
TEST(TextFallback, PicksTheClosestWeightWithinAFamily) {
  FontStore store;
  const FontId regular = *store.load(noto_sans_jp_regular());
  const FontId bold = *store.load(noto_sans_jp_bold());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans JP"};

  style.font_weight = 400;
  EXPECT_EQ(cluster_font(shaper.shape(U"あ", style), 0), regular);

  style.font_weight = 700;
  EXPECT_EQ(cluster_font(shaper.shape(U"あ", style), 0), bold);

  style.font_weight = 900;  // 700 以上が無いので 700 に落ちる
  EXPECT_EQ(cluster_font(shaper.shape(U"あ", style), 0), bold);

  style.font_weight = 100;  // 400 以下が無いので 400 に落ちる
  EXPECT_EQ(cluster_font(shaper.shape(U"あ", style), 0), regular);

  style.font_weight = 600;  // 700 のほうが近い
  EXPECT_EQ(cluster_font(shaper.shape(U"あ", style), 0), bold);
}

TEST(TextFallback, BoldGlyphsDifferFromRegular) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  ASSERT_TRUE(store.load(noto_sans_jp_bold()).has_value());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans JP"};
  style.font_weight = 400;
  const ShapedText regular = shaper.shape(U"永", style);
  style.font_weight = 700;
  const ShapedText bold = shaper.shape(U"永", style);

  ASSERT_EQ(regular.glyphs.size(), 1U);
  ASSERT_EQ(bold.glyphs.size(), 1U);
  EXPECT_NE(regular.glyphs[0].font, bold.glyphs[0].font);
}

TEST(TextFallback, SwitchesFontsRepeatedlyWithinOneRun) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  const std::u32string text = U"あAいBう";
  const ShapedText shaped = shaper.shape(text, style_at());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 5U);

  EXPECT_EQ(cluster_font(shaped, 0), japanese);
  EXPECT_EQ(cluster_font(shaped, 1), latin);
  EXPECT_EQ(cluster_font(shaped, 2), japanese);
  EXPECT_EQ(cluster_font(shaped, 3), latin);
  EXPECT_EQ(cluster_font(shaped, 4), japanese);
}

}  // namespace
}  // namespace shashoku::text
