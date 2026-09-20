#include "text/shaper.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "text/font_store.hpp"
#include "text/shaped_text_checks.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {
namespace {

using assets::expect_valid_clusters;
using assets::noto_sans;
using assets::noto_sans_jp_regular;

TextStyle japanese_style(float size = 32.0F) {
  TextStyle style;
  style.font_size = size;
  return style;
}

// DESIGN.md Phase 2 の受け入れ条件:
// 「こんにちは、世界のみんな。ABC😀」が 1 行でシェーピングでき、😀 だけが豆腐になる。
TEST(TextShaper, Phase2AcceptsMixedJapaneseLatinAndEmoji) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"こんにちは、世界のみんな。ABC😀";
  ASSERT_EQ(text.size(), 17U);

  const ShapedText shaped = shaper.shape(text, japanese_style());
  expect_valid_clusters(shaped, text.size());

  // 1 コードポイント = 1 クラスタ（結合文字も合字もない文字列）
  ASSERT_EQ(shaped.clusters.size(), text.size());

  // 全角 13 文字は 1em の送り
  for (std::size_t i = 0; i < 13; ++i) {
    EXPECT_NEAR(shaped.clusters[i].advance, 32.0F, 0.02F) << "クラスタ " << i;
    EXPECT_FALSE(shaped.clusters[i].missing) << "クラスタ " << i;
  }
  // 欧文 3 文字は全角より狭い
  for (std::size_t i = 13; i < 16; ++i) {
    EXPECT_GT(shaped.clusters[i].advance, 0.0F);
    EXPECT_LT(shaped.clusters[i].advance, 32.0F);
    EXPECT_FALSE(shaped.clusters[i].missing);
  }
  // 😀 だけが豆腐。送りは 1em
  EXPECT_TRUE(shaped.clusters[16].missing);
  EXPECT_NEAR(shaped.clusters[16].advance, 32.0F, 0.02F);

  const std::vector<MissingGlyph> missing = shaper.take_missing_glyphs();
  ASSERT_EQ(missing.size(), 1U);
  EXPECT_EQ(missing[0].cp, U'\U0001F600');
  EXPECT_TRUE(shaper.take_missing_glyphs().empty()) << "take_missing_glyphs は取り出して空にする";
}

TEST(TextShaper, RecordsEachMissingCodepointOnce) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"😀あ😀😁";
  const ShapedText shaped = shaper.shape(text, japanese_style());
  expect_valid_clusters(shaped, text.size());

  const std::vector<MissingGlyph> missing = shaper.take_missing_glyphs();
  ASSERT_EQ(missing.size(), 2U);
  EXPECT_EQ(missing[0].cp, U'\U0001F600');
  EXPECT_EQ(missing[1].cp, U'\U0001F601');
}

TEST(TextShaper, MissingGlyphUsesTheBoxOfTheFirstFontThatHasIt) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  const ShapedText shaped = shaper.shape(U"😀", japanese_style());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, jp);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, store.glyph_for(jp, U'□'));
  EXPECT_NE(shaped.glyphs[0].glyph_id, 0);
}

TEST(TextShaper, MissingGlyphFallsBackToNotdefWhenNoFontHasTheBox) {
  // Noto Sans（欧文）は □（U+25A1）を持たない。その場合だけ第一フォントの .notdef。
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  ASSERT_FALSE(store.has_glyph(latin, U'□'));
  Shaper shaper(store);

  const ShapedText shaped = shaper.shape(U"あ", japanese_style());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, latin);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, 0);
  EXPECT_NEAR(shaped.glyphs[0].advance, 32.0F, 0.02F);
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_TRUE(shaped.clusters[0].missing);
}

TEST(TextShaper, EmptyTextProducesNothing) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText shaped = shaper.shape(U"", japanese_style());
  EXPECT_TRUE(shaped.glyphs.empty());
  EXPECT_TRUE(shaped.clusters.empty());
}

TEST(TextShaper, AdvancesScaleWithFontSize) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText small = shaper.shape(U"日本語", japanese_style(16.0F));
  const ShapedText large = shaper.shape(U"日本語", japanese_style(48.0F));
  ASSERT_EQ(small.clusters.size(), 3U);
  ASSERT_EQ(large.clusters.size(), 3U);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(small.clusters[i].advance, 16.0F, 0.02F);
    EXPECT_NEAR(large.clusters[i].advance, 48.0F, 0.02F);
  }
}

TEST(TextShaper, IsDeterministic) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"吾輩は猫である。Name is not yet. 😀";
  const ShapedText first = shaper.shape(text, japanese_style());
  const ShapedText second = shaper.shape(text, japanese_style());
  EXPECT_EQ(first.glyphs, second.glyphs);
  EXPECT_EQ(first.clusters, second.clusters);

  // 別の Shaper インスタンスでも同じ結果になる（グローバル状態に依存しない）
  Shaper other(store);
  const ShapedText third = other.shape(text, japanese_style());
  EXPECT_EQ(first.glyphs, third.glyphs);
  EXPECT_EQ(first.clusters, third.clusters);
}

TEST(TextShaper, GlyphOffsetsAreZeroForPlainJapanese) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText shaped = shaper.shape(U"日本語", japanese_style());
  for (const ShapedGlyph& glyph : shaped.glyphs) {
    EXPECT_FALSE(glyph.sideways);
    EXPECT_FLOAT_EQ(glyph.x_offset, 0.0F);
    EXPECT_FLOAT_EQ(glyph.y_offset, 0.0F);
  }
}

TEST(TextShaper, MetricsArePositiveAndProportionalToFontSize) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const FontMetrics at16 = shaper.metrics(japanese_style(16.0F));
  const FontMetrics at32 = shaper.metrics(japanese_style(32.0F));

  EXPECT_GT(at16.ascent, 0.0F);
  EXPECT_GT(at16.descent, 0.0F);
  EXPECT_GE(at16.line_gap, 0.0F);
  // ascent + descent は 1em 前後（Noto は 1.16em ほど）
  EXPECT_GT(at16.ascent + at16.descent, 16.0F);
  EXPECT_LT(at16.ascent + at16.descent, 32.0F);

  EXPECT_NEAR(at32.ascent, at16.ascent * 2.0F, 0.05F);
  EXPECT_NEAR(at32.descent, at16.descent * 2.0F, 0.05F);
}

TEST(TextShaper, MetricsComeFromTheFirstFontOfTheStack) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const TextStyle latin_first = japanese_style();
  const FontMetrics latin = shaper.metrics(latin_first);

  TextStyle jp_first = japanese_style();
  jp_first.font_family = {"Noto Sans JP"};
  const FontMetrics japanese = shaper.metrics(jp_first);

  EXPECT_GT(latin.ascent, 0.0F);
  EXPECT_GT(japanese.ascent, 0.0F);
  EXPECT_NE(latin.ascent, japanese.ascent) << "第一フォントが変われば値も変わるはず";
}

TEST(TextShaper, MetricsAreEmptyWithoutFonts) {
  const FontStore store;
  Shaper shaper(store);
  const FontMetrics metrics = shaper.metrics(japanese_style());
  EXPECT_FLOAT_EQ(metrics.ascent, 0.0F);
  EXPECT_FLOAT_EQ(metrics.descent, 0.0F);
}

TEST(TextShaper, ShapesWithoutFontsWithoutCrashing) {
  const FontStore store;
  Shaper shaper(store);
  const std::u32string text = U"あA";
  const ShapedText shaped = shaper.shape(text, japanese_style());
  expect_valid_clusters(shaped, text.size());
  for (const ShapedCluster& cluster : shaped.clusters) {
    EXPECT_TRUE(cluster.missing);
  }
}

}  // namespace
}  // namespace shashoku::text
