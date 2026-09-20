#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "core/ids.hpp"
#include "text/font_store.hpp"
#include "text/shaped_text_checks.hpp"
#include "text/shaper.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

// A2: 行分割の単位はクラスタ。結合文字・異体字セレクタ・合字・ZWJ 連結の途中で
// 割ってはいけないので、それらが 1 クラスタにまとまり、別フォントにも割れないことを見る。

namespace shashoku::text {
namespace {

using assets::expect_valid_clusters;
using assets::noto_sans;
using assets::noto_sans_jp_regular;

// 目に見えない文字はソースに直接書かず、コードポイントで組み立てる。
constexpr char32_t kCombiningVoicedMark = 0x3099;   // 濁点（か + これ = が）
constexpr char32_t kCombiningAcute = 0x0301;        // アキュートアクセント
constexpr char32_t kZeroWidthJoiner = 0x200D;       // ZWJ
constexpr char32_t kVariationSelector17 = 0xE0100;  // IVS の 1 つ目
constexpr char32_t kManEmoji = 0x1F468;
constexpr char32_t kWomanEmoji = 0x1F469;

TextStyle style_at(float size = 32.0F) {
  TextStyle style;
  style.font_size = size;
  return style;
}

// クラスタ内のグリフがすべて同じフォントであること。
void expect_single_font(const ShapedText& shaped, std::size_t index) {
  const ShapedCluster& cluster = shaped.clusters[index];
  ASSERT_LT(cluster.glyph_begin, cluster.glyph_end);
  const FontId font = shaped.glyphs[cluster.glyph_begin].font;
  for (std::uint32_t g = cluster.glyph_begin; g < cluster.glyph_end; ++g) {
    EXPECT_EQ(shaped.glyphs[g].font, font) << "クラスタ " << index << " が別フォントに割れた";
  }
}

TEST(TextCluster, CombiningVoicedSoundMarkStaysWithItsBase) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = std::u32string(U"か") + kCombiningVoicedMark;  // か + 濁点 = が
  ASSERT_EQ(text.size(), 2U);
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());

  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(shaped.clusters[0].text_begin, 0U);
  EXPECT_EQ(shaped.clusters[0].text_end, 2U);
  expect_single_font(shaped, 0);
}

TEST(TextCluster, VariationSelectorStaysWithItsBase) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  // 葛 + 異体字セレクタ、そのあと別の漢字
  const std::u32string text = std::u32string(U"葛") + kVariationSelector17 + U"城";
  ASSERT_EQ(text.size(), 3U);
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());

  ASSERT_EQ(shaped.clusters.size(), 2U);
  EXPECT_EQ(shaped.clusters[0].text_begin, 0U);
  EXPECT_EQ(shaped.clusters[0].text_end, 2U) << "異体字セレクタは基底文字と同じクラスタ";
  EXPECT_EQ(shaped.clusters[1].text_begin, 2U);
  expect_single_font(shaped, 0);
  expect_single_font(shaped, 1);
  // 異体字セレクタ自身を cmap に持たなくても、別フォントにも豆腐にも落とさない
  EXPECT_FALSE(shaped.clusters[0].missing);
}

TEST(TextCluster, ZeroWidthJoinerKeepsBothSidesInOneCluster) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  const std::u32string text = std::u32string(U"A") + kZeroWidthJoiner + U"B";
  ASSERT_EQ(text.size(), 3U);
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());

  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(shaped.clusters[0].text_end, 3U);
  expect_single_font(shaped, 0);
}

TEST(TextCluster, LatinLigatureIsOneCluster) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  const std::u32string text = U"fi";
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());

  // Noto Sans は liga で fi を 1 グリフにする。合字を持たないフォントに差し替えても
  // 壊れないよう、グリフ数で場合分けして判定する。
  if (shaped.glyphs.size() == 1) {
    ASSERT_EQ(shaped.clusters.size(), 1U);
    EXPECT_EQ(shaped.clusters[0].text_begin, 0U);
    EXPECT_EQ(shaped.clusters[0].text_end, 2U);
  } else {
    EXPECT_EQ(shaped.clusters.size(), 2U);
  }
}

TEST(TextCluster, EmojiZwjSequenceIsOneCluster) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  // 男 + ZWJ + 女。どちらのコードポイントも和文フォントには無いので豆腐になるが、
  // ZWJ で繋がった列は 1 クラスタにまとまる（途中で行を割らない）。
  const std::u32string text = std::u32string(1, kManEmoji) + kZeroWidthJoiner + kWomanEmoji;
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());

  ASSERT_EQ(shaped.clusters.size(), 1U);
  EXPECT_EQ(shaped.clusters[0].text_end, 3U);
  EXPECT_TRUE(shaped.clusters[0].missing);
}

TEST(TextCluster, CombiningMarkOnLatinStaysWithItsBase) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  const std::u32string text = std::u32string(U"e") + kCombiningAcute + U"a";
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());

  ASSERT_EQ(shaped.clusters.size(), 2U);
  EXPECT_EQ(shaped.clusters[0].text_end, 2U);
  expect_single_font(shaped, 0);
}

TEST(TextCluster, LeadingCombiningMarkDoesNotCrash) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
  Shaper shaper(store);

  const std::u32string text = std::u32string(1, kCombiningVoicedMark) + U"あ";
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());
}

TEST(TextCluster, DefaultIgnorableOnlyTextIsStillCovered) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans()).has_value());
  Shaper shaper(store);

  const std::u32string text(1, kZeroWidthJoiner);
  const ShapedText shaped = shape_ok(shaper, text, style_at());
  expect_valid_clusters(shaped, text.size());
}

}  // namespace
}  // namespace shashoku::text
