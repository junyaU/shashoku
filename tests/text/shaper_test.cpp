#include "text/shaper.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "shashoku/error.hpp"
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

  const ShapedText shaped = shape_ok(shaper, text, japanese_style());
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
}

// A30 / issue #9: Shaper は豆腐を溜めない。同じ入力からは必ず同じ結果が返り、
// 「何度シェーピングしたか」が結果に出ない（どの文字が豆腐かは clusters で分かる）。
TEST(TextShaper, ReportsMissingGlyphsPerClusterWithoutAccumulating) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"😀あ😀😁";
  const ShapedText shaped = shape_ok(shaper, text, japanese_style());
  expect_valid_clusters(shaped, text.size());

  ASSERT_EQ(shaped.clusters.size(), 4U);
  EXPECT_TRUE(shaped.clusters[0].missing);
  EXPECT_FALSE(shaped.clusters[1].missing);
  EXPECT_TRUE(shaped.clusters[2].missing) << "同じ文字が 2 回目でも missing は落ちない";
  EXPECT_TRUE(shaped.clusters[3].missing);

  // 2 回目のシェーピングもまったく同じ結果（副作用がない）
  const ShapedText again = shape_ok(shaper, text, japanese_style());
  EXPECT_EQ(again.clusters, shaped.clusters);
  EXPECT_EQ(again.glyphs, shaped.glyphs);
}

TEST(TextShaper, MissingGlyphUsesTheBoxOfTheFirstFontThatHasIt) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  const ShapedText shaped = shape_ok(shaper, U"😀", japanese_style());
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

  const ShapedText shaped = shape_ok(shaper, U"あ", japanese_style());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, latin);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, 0);
  EXPECT_NEAR(shaped.glyphs[0].advance, 32.0F, 0.02F);
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_TRUE(shaped.clusters[0].missing);
}

TEST(TextShaper, AdvancesScaleWithFontSize) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText small = shape_ok(shaper, U"日本語", japanese_style(16.0F));
  const ShapedText large = shape_ok(shaper, U"日本語", japanese_style(48.0F));
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
  const ShapedText first = shape_ok(shaper, text, japanese_style());
  const ShapedText second = shape_ok(shaper, text, japanese_style());
  EXPECT_EQ(first.glyphs, second.glyphs);
  EXPECT_EQ(first.clusters, second.clusters);

  // 別の Shaper インスタンスでも同じ結果になる（グローバル状態に依存しない）
  Shaper other(store);
  const ShapedText third = shape_ok(other, text, japanese_style());
  EXPECT_EQ(first.glyphs, third.glyphs);
  EXPECT_EQ(first.clusters, third.clusters);
}

TEST(TextShaper, GlyphOffsetsAreZeroForPlainJapanese) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const ShapedText shaped = shape_ok(shaper, U"日本語", japanese_style());
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

  const FontMetrics at16 = metrics_ok(shaper, japanese_style(16.0F));
  const FontMetrics at32 = metrics_ok(shaper, japanese_style(32.0F));

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
  const FontMetrics latin = metrics_ok(shaper, latin_first);

  TextStyle jp_first = japanese_style();
  jp_first.font_family = {"Noto Sans JP"};
  const FontMetrics japanese = metrics_ok(shaper, jp_first);

  EXPECT_GT(latin.ascent, 0.0F);
  EXPECT_GT(japanese.ascent, 0.0F);
  EXPECT_NE(latin.ascent, japanese.ascent) << "第一フォントが変われば値も変わるはず";
}

// A30: フォントが 1 つも無い FontStore で測るのは呼び出し側の契約違反（api は NoFonts で
// 弾いている）。黙って空のメトリクス・存在しないフォントの .notdef を返さず、Internal で落ちる。
TEST(TextShaper, WithoutFontsIsAnInternalErrorInsteadOfEmptyResults) {
  const FontStore store;
  Shaper shaper(store);

  const Result<FontMetrics> metrics = shaper.metrics(japanese_style());
  ASSERT_FALSE(metrics.has_value());
  EXPECT_EQ(metrics.error().kind, ErrorKind::Internal);

  const Result<ShapedText> shaped = shaper.shape(U"あA", japanese_style());
  ASSERT_FALSE(shaped.has_value());
  EXPECT_EQ(shaped.error().kind, ErrorKind::Internal);
  EXPECT_NE(shaped.error().message.find("フォント"), std::string::npos);
}

// 「正常に 0 グリフ」（空文字列）は成功であって失敗ではない（text_measurer.hpp）。
TEST(TextShaper, EmptyTextSucceedsEvenThoughItProducesNoGlyphs) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const Result<ShapedText> shaped = shaper.shape(U"", japanese_style());
  ASSERT_TRUE(shaped.has_value()) << to_string(shaped.error());
  EXPECT_TRUE(shaped->glyphs.empty());
  EXPECT_TRUE(shaped->clusters.empty());
}

// 非有限の font_size も呼び出し側のバグ（A25 が px の上限を押さえている）。
TEST(TextShaper, NonFiniteFontSizeIsAnInternalError) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const Result<ShapedText> shaped =
      shaper.shape(U"あ", japanese_style(std::numeric_limits<float>::infinity()));
  ASSERT_FALSE(shaped.has_value());
  EXPECT_EQ(shaped.error().kind, ErrorKind::Internal);
}

}  // namespace
}  // namespace shashoku::text
