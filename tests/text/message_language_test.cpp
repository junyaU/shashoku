// エラーメッセージの言語は英語にそろえる（#20）。
//
// text モジュールだけが日本語で、試用者が受け取る文面が混ざっていた
// （`error[font-load]: font #0: フォントを解釈できません ...`）。ここでは text の
// 失敗経路を一通り踏んで、message に ASCII 以外が混ざっていないことを見張る。
//
// 実行時に踏めない経路（輪郭を持たないグリフ、未対応の pixel_mode、HarfBuzz の確保失敗など）は
// このテストでは踏めないので、`src/text/` 全体を grep して日本語が残っていないことも確かめてある。
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/glyph_source.hpp"
#include "shashoku/error.hpp"
#include "text/font_store.hpp"
#include "text/freetype_glyph_source.hpp"
#include "text/shaper.hpp"
#include "text/test_fonts.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {
namespace {

using assets::noto_sans_jp_regular;

void expect_ascii(const Error& error, std::string_view what) {
  for (const char c : error.message) {
    ASSERT_LT(static_cast<unsigned char>(c), 0x80U)
        << what << ": message は英語（ASCII）で書く: " << error.message;
  }
  EXPECT_FALSE(error.message.empty()) << what;
}

TEST(TextMessageLanguage, FontStoreErrorsAreAscii) {
  FontStore store;

  const auto empty = store.load({});
  ASSERT_FALSE(empty.has_value());
  expect_ascii(empty.error(), "load({})");

  const std::vector<std::uint8_t>& full = noto_sans_jp_regular();
  ASSERT_GT(full.size(), 4096U);
  const std::vector<std::uint8_t> truncated(full.begin(), full.begin() + 4096);
  const auto broken = store.load(truncated);
  ASSERT_FALSE(broken.has_value());
  expect_ascii(broken.error(), "load(truncated)");
}

TEST(TextMessageLanguage, GlyphSourceErrorsAreAscii) {
  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  const GlyphId glyph = store.glyph_for(jp, U'あ');
  FreeTypeGlyphSource glyphs(store);

  struct Case {
    FontId font;
    GlyphId glyph;
    float pixel_size;
    const char* what;
  };
  const std::array<Case, 5> cases{{
      {99, 1, 32.0F, "不正な FontId"},
      {jp, 65535, 32.0F, "範囲外の glyph_id（FT_Load_Glyph の失敗）"},
      {jp, glyph, 0.0F, "pixel_size = 0"},
      {jp, glyph, std::numeric_limits<float>::quiet_NaN(), "pixel_size = NaN"},
      {jp, glyph, 1.0e30F, "pixel_size が上限超え"},
  }};
  for (const Case& c : cases) {
    const Result<raster::GlyphBitmap> result =
        glyphs.rasterize(c.font, c.glyph, c.pixel_size, false);
    ASSERT_FALSE(result.has_value()) << c.what;
    expect_ascii(result.error(), c.what);
  }
}

TEST(TextMessageLanguage, ShaperErrorsAreAscii) {
  {  // フォントが 1 つも無い FontStore（呼び出し側の契約違反）
    const FontStore empty_store;
    Shaper shaper(empty_store);
    TextStyle style;
    style.font_size = 32.0F;

    const Result<ShapedText> shaped = shaper.shape(U"あ", style);
    ASSERT_FALSE(shaped.has_value());
    expect_ascii(shaped.error(), "shape() without fonts");

    const Result<FontMetrics> metrics = shaper.metrics(style);
    ASSERT_FALSE(metrics.has_value());
    expect_ascii(metrics.error(), "metrics() without fonts");
  }
  {  // 有限でない font_size
    FontStore store;
    ASSERT_TRUE(store.load(noto_sans_jp_regular()).has_value());
    Shaper shaper(store);
    TextStyle style;
    style.font_size = std::numeric_limits<float>::infinity();

    const Result<ShapedText> shaped = shaper.shape(U"あ", style);
    ASSERT_FALSE(shaped.has_value());
    expect_ascii(shaped.error(), "shape() with an infinite font_size");
  }
}

// 非有限の値を文面に入れるときは number_text() を通す（core/number_text.hpp）。
// NaN の符号ビットは CPU によって違う（x86 の SSE は inf/inf で負の NaN、ARM は正の NaN）
// ので、そのまま出すと同じ入力でも文面が `-nan` になったり `nan` になったりする。
TEST(TextMessageLanguage, NonFiniteValuesAreWrittenTheSameWayWhateverTheirSignBit) {
  const float quiet = std::numeric_limits<float>::quiet_NaN();
  const float positive = std::copysign(quiet, 1.0F);
  const float negative = std::copysign(quiet, -1.0F);
  ASSERT_FALSE(std::signbit(positive));
  ASSERT_TRUE(std::signbit(negative));

  FontStore store;
  const FontId jp = *store.load(noto_sans_jp_regular());
  const GlyphId glyph = store.glyph_for(jp, U'あ');
  FreeTypeGlyphSource glyphs(store);
  Shaper shaper(store);

  // (1) シェーピング: font_size が有限でない（text_measurer.hpp の契約違反）
  const auto shape_message = [&shaper](float size) {
    TextStyle style;
    style.font_size = size;
    const Result<ShapedText> shaped = shaper.shape(U"あ", style);
    EXPECT_FALSE(shaped.has_value());
    return shaped.has_value() ? std::string{} : shaped.error().message;
  };
  // (2) グリフのラスタライズ: pixel_size が有限でない（glyph_source.hpp の契約違反）
  const auto rasterize_message = [&glyphs, jp, glyph](float size) {
    const Result<raster::GlyphBitmap> result = glyphs.rasterize(jp, glyph, size, false);
    EXPECT_FALSE(result.has_value());
    return result.has_value() ? std::string{} : result.error().message;
  };

  const auto expect_same_text = [](const std::string& from_positive,
                                   const std::string& from_negative) {
    EXPECT_EQ(from_positive, from_negative) << from_positive << " / " << from_negative;
    EXPECT_NE(from_positive.find("NaN"), std::string::npos) << from_positive;
    // 環境で変わる表記（`nan` / `-nan`）が残っていないこと
    EXPECT_EQ(from_positive.find("nan"), std::string::npos) << from_positive;
  };
  expect_same_text(shape_message(positive), shape_message(negative));
  expect_same_text(rasterize_message(positive), rasterize_message(negative));

  // 無限大の符号は入力で決まる（環境に依らない）のでそのまま出す。
  constexpr float kInf = std::numeric_limits<float>::infinity();
  EXPECT_NE(shape_message(kInf).find("inf"), std::string::npos);
  EXPECT_NE(rasterize_message(-kInf).find("-inf"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::text
