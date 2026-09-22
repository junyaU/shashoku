// カラーフォント（COLR）を render() まで通した end-to-end（issue #27 / A-new）。
//
// 元の不具合: 色レイヤーだけで絵を作る COLR フォントでは、その文字が警告も豆腐も無いまま
// 消え、**全ピクセルが透明な PNG が終了コード 0 で返って**いた。ここで見るのは
//   1. 警告が MissingGlyph で 1 件出て、位置がテキストノードの先頭を指すこと
//   2. detail に COLR が原因だと出ること（WarningKind は増やさない）
//   3. PNG が透明なだけの画像にならないこと（□ が実際に描かれる）

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"
#include "text/colr_test_font.hpp"

namespace shashoku::test {
namespace {

// 色レイヤーだけの 'A' を持つ 704 バイトのフォント（tests/text/colr_test_font.hpp）。
FontSet color_only_font() {
  FontSet fonts;
  fonts.add(text::assets::colr_font_empty_base());
  return fonts;
}

std::size_t opaque_pixels(const Bitmap& bitmap) {
  std::size_t count = 0;
  for (std::size_t i = 3; i < bitmap.rgba.size(); i += 4) {
    if (bitmap.rgba[i] != 0) {
      ++count;
    }
  }
  return count;
}

//                                            1         2         3
//                                   1234567890123456789012345678901
constexpr std::string_view kColrHtml = R"(<p style="font-size: 64px">AAA</p>)";

TEST(ColorFont, ColorOnlyGlyphsWarnAndAreDrawnAsTofu) {
  const auto result = render(kColrHtml, color_only_font(), options_for(400));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());

  // 同じテキストノードの 3 文字なので 1 件（A31 の重複除去）。
  ASSERT_EQ(result->warnings.size(), 1U);
  const Warning& warning = result->warnings.front();
  EXPECT_EQ(warning.kind, WarningKind::MissingGlyph);  // 種類は増やさない
  EXPECT_EQ(warning.codepoint, U'A');
  ASSERT_TRUE(warning.location.has_value());
  EXPECT_EQ(warning.location->line, 1U);
  EXPECT_EQ(warning.location->column, 28U);  // テキストノードの先頭
  EXPECT_EQ(warning.detail,
            "the glyph for U+0041 has only color layers (COLR); drawn as tofu at 1:28");

  // 絵が出ていること。修正前はここが 0（全ピクセル透明）だった。
  const Bitmap bitmap = render_bitmap(kColrHtml, color_only_font(), options_for(400));
  EXPECT_GT(opaque_pixels(bitmap), 0U);
}

// 段落を 2 つに分けると 2 件（位置ごとに 1 件）。cmap に無い文字は従来の文面のまま。
TEST(ColorFont, ReasonIsPerLocationAndOtherTofuKeepsItsMessage) {
  constexpr std::string_view kHtml = "<p>A</p>\n<p>AB</p>";
  const auto result = render(kHtml, color_only_font(), options_for(400));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 3U);

  // 並びは入力位置の昇順 → コードポイントの昇順（A31）。
  EXPECT_EQ(result->warnings[0].detail,
            "the glyph for U+0041 has only color layers (COLR); drawn as tofu at 1:4");
  EXPECT_EQ(result->warnings[1].detail,
            "the glyph for U+0041 has only color layers (COLR); drawn as tofu at 2:4");
  // 'B' は cmap に無い従来の豆腐。**文面は 1 文字も変わらない**。
  EXPECT_EQ(result->warnings[2].detail, "no font has a glyph for U+0042 at 2:4");
  for (const Warning& warning : result->warnings) {
    EXPECT_EQ(warning.kind, WarningKind::MissingGlyph);
  }
}

// COLR を持たないフォントでは警告も出ず、文面も従来どおり（回帰の見張り）。
TEST(ColorFont, OrdinaryFontsAreUnaffected) {
  const auto result = render("<p>こんにちは😀</p>", japanese_fonts(), options_for(400));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 1U);
  EXPECT_EQ(result->warnings[0].detail, "no font has a glyph for U+1F600 at 1:4");
}

}  // namespace
}  // namespace shashoku::test
