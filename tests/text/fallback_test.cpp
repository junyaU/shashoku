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
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());
  ASSERT_EQ(shaped.clusters.size(), 4U);

  // 欧文は追加順で最初にグリフを持つ Noto Sans、和文だけ Noto Sans JP に落ちる
  EXPECT_EQ(cluster_font(shaped, 0), latin);
  EXPECT_EQ(cluster_font(shaped, 1), latin);
  EXPECT_EQ(cluster_font(shaped, 2), latin);
  EXPECT_EQ(cluster_font(shaped, 3), japanese);
  for (const ShapedCluster& cluster : shaped.clusters) {
    EXPECT_FALSE(cluster.missing);
  }
}

TEST(TextFallback, FontFamilyChangesTheOrder) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans JP"};
  const std::u32string text = U"ABCあ";
  const ShapedText shaped = shape_ok(shaper, text, style);
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
  const ShapedText shaped = shape_ok(shaper, U"A", style);
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
  const ShapedText shaped = shape_ok(shaper, U"A", style);
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
  const ShapedText shaped = shape_ok(shaper, U"A", style);
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(cluster_font(shaped, 0), latin);
}

// font-family は family の優先順を変えるだけで、太さの照合は常に全 family に効く。
// font-family を書かない普通の HTML でも font-weight: 700 の見出しが Bold になること。
TEST(TextFallback, WeightAppliesWithoutAnyFontFamily) {
  FontStore store;
  const FontId regular = *store.load(noto_sans_jp_regular());
  const FontId bold = *store.load(noto_sans_jp_bold());
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  TextStyle style = style_at();  // font_family は空のまま
  ASSERT_TRUE(style.font_family.empty());

  struct Case {
    int weight;
    FontId expected;
  };
  for (const Case& testcase : {Case{100, regular}, Case{400, regular}, Case{500, regular},
                               Case{600, bold}, Case{700, bold}, Case{900, bold}}) {
    style.font_weight = testcase.weight;
    EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), testcase.expected)
        << "font-weight " << testcase.weight;
    // 欧文も同じ列から引かれる（Noto Sans JP グループが先なので Bold が 'A' を持つ）
    EXPECT_EQ(cluster_font(shape_ok(shaper, U"A", style), 0), testcase.expected)
        << "font-weight " << testcase.weight;
  }
}

// 別の family を font-family で指定しても、そこに無い文字が落ちた先の family では
// 指定した太さの face が選ばれる。
TEST(TextFallback, WeightAppliesInsideFamiliesThatFontFamilyDidNotName) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  const FontId bold = *store.load(noto_sans_jp_bold());
  const FontId latin = *store.load(noto_sans());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans"};  // 欧文だけを名指しする
  style.font_weight = 700;

  EXPECT_EQ(cluster_font(shape_ok(shaper, U"A", style), 0), latin) << "名指しした family が先";
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold)
      << "落ちた先の family でも font-weight は効く";
}

TEST(TextFallback, WeightAppliesInVerticalTextToo) {
  FontStore store;
  const FontId regular = *store.load(noto_sans_jp_regular());
  const FontId bold = *store.load(noto_sans_jp_bold());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.direction = Direction::Vertical;
  style.font_weight = 700;
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold);
  style.font_weight = 400;
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), regular);
}

// family に face が 1 つしかなければ、font-weight が何であってもそれが使われる。
TEST(TextFallback, SingleFaceFamilyIsUsedWhateverTheWeight) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  TextStyle style = style_at();
  for (const int weight : {100, 400, 700, 900}) {
    style.font_weight = weight;
    EXPECT_EQ(cluster_font(shape_ok(shaper, U"A", style), 0), latin) << "font-weight " << weight;
    EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), japanese)
        << "font-weight " << weight;
  }
}

// グループの順序は「その family の最初のフォントが追加された順」。
// 同じ family の 2 つ目以降の face を足しても、family の並びは動かない。
TEST(TextFallback, FamilyOrderFollowsTheFirstFaceOfEachFamily) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId regular = *store.load(noto_sans_jp_regular());
  const FontId bold = *store.load(noto_sans_jp_bold());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_weight = 700;
  // Noto Sans が先に追加されているので、'A' は Bold ではなく Noto Sans で描かれる
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"A", style), 0), latin);
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold);

  style.font_family = {"Noto Sans JP"};
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"A", style), 0), bold) << "名指しで family の順が変わる";

  style.font_weight = 400;
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"A", style), 0), regular);
}

TEST(TextFallback, MetricsComeFromTheWeightMatchedFace) {
  FontStore store;
  const FontId regular = *store.load(noto_sans_jp_regular());
  const FontId bold = *store.load(noto_sans_jp_bold());
  Shaper shaper(store);
  EXPECT_NE(regular, bold);

  TextStyle style = style_at();
  style.font_weight = 700;
  const FontMetrics bold_metrics = metrics_ok(shaper, style);
  EXPECT_GT(bold_metrics.ascent, 0.0F);
  EXPECT_GT(bold_metrics.descent, 0.0F);
  // 先頭が Bold になっているので、Bold のグリフが選ばれることで裏づける
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold);
}

TEST(TextFallback, TofuBoxComesFromTheWeightMatchedFace) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  const FontId bold = *store.load(noto_sans_jp_bold());
  Shaper shaper(store);

  TextStyle style = style_at();  // font_family なし
  style.font_weight = 700;
  const ShapedText shaped = shape_ok(shaper, U"😀", style);
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, bold);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, store.glyph_for(bold, U'□'));
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
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), regular);

  style.font_weight = 700;
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold);

  style.font_weight = 900;  // 700 以上が無いので 700 に落ちる
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold);

  style.font_weight = 100;  // 400 以下が無いので 400 に落ちる
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), regular);

  style.font_weight = 600;  // 700 のほうが近い
  EXPECT_EQ(cluster_font(shape_ok(shaper, U"あ", style), 0), bold);
}

TEST(TextFallback, BoldGlyphsDifferFromRegular) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  ASSERT_TRUE(store.load(noto_sans_jp_bold()).has_value());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans JP"};
  style.font_weight = 400;
  const ShapedText regular = shape_ok(shaper, U"永", style);
  style.font_weight = 700;
  const ShapedText bold = shape_ok(shaper, U"永", style);

  ASSERT_EQ(regular.glyphs.size(), 1U);
  ASSERT_EQ(bold.glyphs.size(), 1U);
  EXPECT_NE(regular.glyphs[0].font, bold.glyphs[0].font);
}

// 豆腐の □ は第一フォントだけでなくフォールバック列全体から探す。
// 欧文フォントが先頭のとき、第一フォントの .notdef（幅が狭い）で描くと不揃いになる。
TEST(TextFallback, TofuBoxComesFromTheWholeFallbackChain) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  ASSERT_FALSE(store.has_glyph(latin, U'□'));
  ASSERT_TRUE(store.has_glyph(japanese, U'□'));
  Shaper shaper(store);

  const ShapedText shaped = shape_ok(shaper, U"😀", style_at());
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, japanese) << "第一フォントに □ が無ければ次を探す";
  EXPECT_EQ(shaped.glyphs[0].glyph_id, store.glyph_for(japanese, U'□'));
  EXPECT_NE(shaped.glyphs[0].glyph_id, 0);

  // 送りと豆腐の印はこれまでどおり
  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_TRUE(shaped.clusters[0].missing);
  EXPECT_NEAR(shaped.clusters[0].advance, 32.0F, 0.02F);
}

// font-family で並べ替えたフォールバック列の順に探す（FontStore の追加順ではなく）。
TEST(TextFallback, TofuBoxFollowsTheResolvedFontOrder) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  const FontId bold = *store.load(noto_sans_jp_bold());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.font_family = {"Noto Sans JP"};
  style.font_weight = 700;
  const ShapedText shaped = shape_ok(shaper, U"😀", style);
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, bold);
  EXPECT_EQ(shaped.glyphs[0].glyph_id, store.glyph_for(bold, U'□'));
}

TEST(TextFallback, TofuBoxIsUprightInVerticalText) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  TextStyle style = style_at();
  style.direction = Direction::Vertical;
  const ShapedText shaped = shape_ok(shaper, U"😀", style);
  ASSERT_EQ(shaped.glyphs.size(), 1U);
  EXPECT_EQ(shaped.glyphs[0].font, japanese);
  EXPECT_FALSE(shaped.glyphs[0].sideways);
  EXPECT_NEAR(shaped.glyphs[0].advance, 32.0F, 0.02F);
  EXPECT_NEAR(shaped.glyphs[0].x_offset, -16.0F, 0.02F);
  EXPECT_GT(shaped.glyphs[0].y_offset, 0.0F);
}

TEST(TextFallback, SwitchesFontsRepeatedlyWithinOneRun) {
  FontStore store;
  const FontId latin = *store.load(noto_sans());
  const FontId japanese = *store.load(noto_sans_jp_regular());
  Shaper shaper(store);

  const std::u32string text = U"あAいBう";
  const ShapedText shaped = shape_ok(shaper, text, style_at());
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
