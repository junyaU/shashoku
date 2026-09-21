#include "text/font_store.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "shashoku/error.hpp"
#include "text/test_fonts.hpp"

namespace shashoku::text {
namespace {

using assets::noto_sans;
using assets::noto_sans_jp_bold;
using assets::noto_sans_jp_regular;

TEST(TextFontStore, LoadsThreeFontsInOrder) {
  FontStore store;
  EXPECT_TRUE(store.empty());

  const auto jp = store.load(noto_sans_jp_regular());
  const auto bold = store.load(noto_sans_jp_bold());
  const auto latin = store.load(noto_sans());

  ASSERT_TRUE(jp.has_value()) << to_string(jp.error());
  ASSERT_TRUE(bold.has_value()) << to_string(bold.error());
  ASSERT_TRUE(latin.has_value()) << to_string(latin.error());

  // 追加順がフォールバック順。FontId は 0 から順に払い出される。
  EXPECT_EQ(*jp, 0U);
  EXPECT_EQ(*bold, 1U);
  EXPECT_EQ(*latin, 2U);
  EXPECT_EQ(store.size(), 3U);
  EXPECT_FALSE(store.empty());
}

TEST(TextFontStore, ReportsFamilyWeightAndStyle) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  const FontId bold = *store.load(noto_sans_jp_bold());
  const FontId latin = *store.load(noto_sans());

  EXPECT_EQ(store.family(jp), "Noto Sans JP");
  EXPECT_EQ(store.family(bold), "Noto Sans JP");
  EXPECT_EQ(store.family(latin), "Noto Sans");

  EXPECT_EQ(store.weight(jp), 400);
  EXPECT_EQ(store.weight(bold), 700);
  EXPECT_EQ(store.weight(latin), 400);

  EXPECT_FALSE(store.is_italic(jp));
  EXPECT_FALSE(store.is_italic(bold));

  EXPECT_EQ(store.units_per_em(jp), 1000);
  EXPECT_EQ(store.units_per_em(latin), 1000);
}

TEST(TextFontStore, ResolvesGlyphsThroughCmap) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  const FontId latin = *store.load(noto_sans());

  EXPECT_TRUE(store.has_glyph(jp, U'あ'));
  EXPECT_TRUE(store.has_glyph(jp, U'A'));
  EXPECT_FALSE(store.has_glyph(jp, U'\U0001F600'));  // 😀 は和文サブセットに無い

  EXPECT_TRUE(store.has_glyph(latin, U'A'));
  EXPECT_FALSE(store.has_glyph(latin, U'あ'));  // 欧文フォントに和文グリフは無い

  EXPECT_NE(store.glyph_for(jp, U'あ'), 0);
  EXPECT_EQ(store.glyph_for(jp, U'\U0001F600'), 0);
}

TEST(TextFontStore, RejectsEmptyBytes) {
  FontStore store;
  const auto result = store.load({});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::FontLoad);
  EXPECT_EQ(store.size(), 0U);
}

TEST(TextFontStore, RejectsRandomBytes) {
  // 乱数の種を固定した「フォントではないバイト列」（DESIGN.md §10-4）。
  std::vector<std::uint8_t> garbage(4096);
  std::uint32_t state = 0x1234'5678U;
  for (std::uint8_t& byte : garbage) {
    state = state * 1664525U + 1013904223U;
    byte = static_cast<std::uint8_t>(state >> 24U);
  }

  FontStore store;
  const auto result = store.load(garbage);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::FontLoad);
  EXPECT_EQ(store.size(), 0U);
}

TEST(TextFontStore, RejectsTruncatedFont) {
  const std::vector<std::uint8_t>& full = noto_sans_jp_regular();
  ASSERT_GT(full.size(), 4096U);
  const std::vector<std::uint8_t> truncated(full.begin(), full.begin() + 4096);

  FontStore store;
  const auto result = store.load(truncated);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::FontLoad);
  EXPECT_EQ(store.size(), 0U);
}

// エラーメッセージの言語は英語にそろえる（#20）。text モジュールだけが日本語だったので、
// 試用者が受け取る文面が混ざっていた。ASCII 以外が混ざっていないことで見張る。
TEST(TextFontStore, ErrorMessagesAreAscii) {
  const std::vector<std::uint8_t>& full = noto_sans_jp_regular();
  ASSERT_GT(full.size(), 4096U);
  const std::vector<std::uint8_t> truncated(full.begin(), full.begin() + 4096);

  FontStore store;
  for (const std::vector<std::uint8_t>& bytes : {std::vector<std::uint8_t>{}, truncated}) {
    const auto result = store.load(bytes);
    ASSERT_FALSE(result.has_value());
    for (const char c : result.error().message) {
      EXPECT_LT(static_cast<unsigned char>(c), 0x80U)
          << "message は英語（ASCII）で書く: " << result.error().message;
    }
  }
}

TEST(TextFontStore, FailedLoadDoesNotDisturbEarlierFonts) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  EXPECT_FALSE(store.load({}).has_value());

  EXPECT_EQ(store.size(), 1U);
  EXPECT_EQ(store.family(jp), "Noto Sans JP");
  EXPECT_EQ(*store.load(noto_sans()), 1U);
}

TEST(TextFontStore, InvalidFontIdIsSafe) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());

  EXPECT_FALSE(store.contains(99));
  EXPECT_TRUE(store.family(99).empty());
  EXPECT_EQ(store.weight(99), 400);
  EXPECT_FALSE(store.is_italic(99));
  EXPECT_EQ(store.units_per_em(99), 0);
  EXPECT_EQ(store.glyph_for(99, U'あ'), 0);
  EXPECT_FALSE(store.has_glyph(99, U'あ'));
}

TEST(TextFontStore, IsMovable) {
  FontStore store;
  ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());

  FontStore moved(std::move(store));
  EXPECT_EQ(moved.size(), 1U);
  EXPECT_EQ(moved.family(0), "Noto Sans JP");
  EXPECT_TRUE(moved.has_glyph(0, U'あ'));

  FontStore assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.size(), 1U);
  EXPECT_TRUE(assigned.has_glyph(0, U'あ'));
}

}  // namespace
}  // namespace shashoku::text
