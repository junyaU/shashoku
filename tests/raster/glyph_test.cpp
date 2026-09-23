// DrawGlyphs。偽の GlyphSource に手書きのビットマップを返させて、位置・色・丸め・
// 引数の受け渡しを検査する（ARCHITECTURE.md A8 と glyph_source.hpp の位置の約束）。

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"
#include "raster/raster_test_util.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster::test {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

Target square(float size, float scale = 1) {
  Target t;
  t.width = size;
  t.height = size;
  t.scale = scale;
  return t;
}

DrawGlyphs one_glyph(GlyphId id, Point origin, Color color = kBlack) {
  DrawGlyphs cmd;
  cmd.font = 7;
  cmd.size = 10;
  cmd.color = color;
  cmd.glyphs = {GlyphInstance{id, origin}};
  return cmd;
}

std::uint32_t alpha_at(const Bitmap& b, std::uint32_t x, std::uint32_t y) {
  return b.pixel(x, y).a;
}

// glyph_source.hpp: 左上ピクセルのデバイス座標は (origin.x + left, origin.y - top)。
TEST(RasterGlyph, LeftAndTopPlaceTheBitmapRelativeToTheOrigin) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(/*left=*/1, /*top=*/2, /*width=*/2, /*height=*/2, 255));
  const Bitmap b = must_rasterize({one_glyph(1, Point{3, 5})}, square(8), glyphs);
  // 左上は (3 + 1, 5 - 2) = (4, 3)
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 8; ++x) {
      const bool inside = x >= 4 && x <= 5 && y >= 3 && y <= 4;
      EXPECT_TRUE(pixel_is(b, x, y, inside ? kBlack : kTransparent));
    }
  }
}

// 負の left / top も約束どおり（top が負なら原点より下に出る）。
TEST(RasterGlyph, NegativeLeftAndTopAreHonoured) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(/*left=*/-1, /*top=*/-1, 1, 1, 255));
  const Bitmap b = must_rasterize({one_glyph(1, Point{3, 3})}, square(8), glyphs);
  EXPECT_EQ(alpha_at(b, 2, 4), 255U);  // (3 - 1, 3 + 1)
  EXPECT_EQ(alpha_at(b, 3, 3), 0U);
}

TEST(RasterGlyph, CoverageIsMultipliedByTheColorAlpha) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 1, 1, 128));
  glyphs.set(2, solid_glyph(0, 0, 1, 1, 255));

  // 被覆率 128 × 不透明 → 128
  EXPECT_TRUE(
      pixel_is(must_rasterize({one_glyph(1, Point{0, 0}, rgba(255, 0, 0, 255))}, square(1), glyphs),
               0, 0, rgba(255, 0, 0, 128)));
  // 被覆率 255 × a=128 → 128
  EXPECT_TRUE(
      pixel_is(must_rasterize({one_glyph(2, Point{0, 0}, rgba(255, 0, 0, 128))}, square(1), glyphs),
               0, 0, rgba(255, 0, 0, 128)));
  // 被覆率 128 × a=128 → round(128 * 128 / 255) = 64
  EXPECT_TRUE(
      pixel_is(must_rasterize({one_glyph(1, Point{0, 0}, rgba(255, 0, 0, 128))}, square(1), glyphs),
               0, 0, rgba(255, 0, 0, 64)));
}

// A8: 原点 × scale をデバイスピクセルの整数に四捨五入してから置く。
TEST(RasterGlyph, OriginIsRoundedToAnIntegerDevicePixel) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 1, 1, 255));
  struct Case {
    float origin_x;
    float scale;
    std::uint32_t want_x;
  };
  for (const Case& c : std::vector<Case>{
           {2.0F, 1, 2},
           {2.4F, 1, 2},
           {2.5F, 1, 3},
           {2.6F, 1, 3},
           {1.2F, 2, 2},  // round(2.4) = 2
           {1.3F, 2, 3},  // round(2.6) = 3
       }) {
    Target t = square(8, c.scale);
    t.width = 8 / c.scale;
    t.height = 8 / c.scale;
    const Bitmap b = must_rasterize({one_glyph(1, Point{c.origin_x, 0})}, t, glyphs);
    EXPECT_EQ(alpha_at(b, c.want_x, 0), 255U) << "origin " << c.origin_x << " @" << c.scale;
    if (c.want_x > 0) {
      EXPECT_EQ(alpha_at(b, c.want_x - 1, 0), 0U);
    }
  }
}

// pixel_size = size * scale をそのまま GlyphSource に渡す。
TEST(RasterGlyph, PixelSizeIsSizeTimesScale) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 1, 1, 255));
  DrawGlyphs cmd = one_glyph(1, Point{0, 0});
  cmd.size = 12;
  const Target t = square(4, 2);
  must_rasterize({cmd}, t, glyphs);
  ASSERT_EQ(glyphs.calls().size(), 1U);
  EXPECT_EQ(glyphs.calls()[0].pixel_size, 24.0F);
  EXPECT_EQ(glyphs.calls()[0].font, 7U);
  EXPECT_EQ(glyphs.calls()[0].glyph_id, 1U);
  EXPECT_FALSE(glyphs.calls()[0].sideways);
}

TEST(RasterGlyph, SidewaysFlagIsForwarded) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 1, 1, 255));
  DrawGlyphs cmd = one_glyph(1, Point{0, 0});
  cmd.sideways = true;
  must_rasterize({cmd}, square(4), glyphs);
  ASSERT_EQ(glyphs.calls().size(), 1U);
  EXPECT_TRUE(glyphs.calls()[0].sideways);
}

TEST(RasterGlyph, EveryGlyphInTheRunIsDrawn) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 1, 1, 255));
  DrawGlyphs cmd = one_glyph(1, Point{0, 0});
  cmd.glyphs = {GlyphInstance{1, Point{0, 0}}, GlyphInstance{1, Point{2, 0}},
                GlyphInstance{1, Point{4, 0}}};
  const Bitmap b = must_rasterize({cmd}, square(8), glyphs);
  EXPECT_EQ(alpha_at(b, 0, 0), 255U);
  EXPECT_EQ(alpha_at(b, 1, 0), 0U);
  EXPECT_EQ(alpha_at(b, 2, 0), 255U);
  EXPECT_EQ(alpha_at(b, 4, 0), 255U);
  EXPECT_EQ(glyphs.calls().size(), 3U);
}

// 行ごとに被覆率を書いたビットマップが、そのままの向きで置かれること。
TEST(RasterGlyph, RowsArePlacedInRowMajorOrder) {
  FakeGlyphSource glyphs;
  glyphs.set(1, glyph_rows(0, 0, {{0, 64}, {128, 255}}));
  const Bitmap b = must_rasterize({one_glyph(1, Point{0, 0})}, square(2), glyphs);
  EXPECT_EQ(alpha_at(b, 0, 0), 0U);
  EXPECT_EQ(alpha_at(b, 1, 0), 64U);
  EXPECT_EQ(alpha_at(b, 0, 1), 128U);
  EXPECT_EQ(alpha_at(b, 1, 1), 255U);
}

TEST(RasterGlyph, GlyphPartlyOutsideTheTargetIsCropped) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 4, 4, 255));
  // 原点 (-2, 2) → 左上 (-2, 2)。x = 0, 1 と y = 2, 3 だけが残る。
  const Bitmap b = must_rasterize({one_glyph(1, Point{-2, 2})}, square(4), glyphs);
  EXPECT_EQ(alpha_at(b, 0, 2), 255U);
  EXPECT_EQ(alpha_at(b, 1, 3), 255U);
  EXPECT_EQ(alpha_at(b, 2, 2), 0U);
  EXPECT_EQ(alpha_at(b, 0, 1), 0U);
}

TEST(RasterGlyph, GlyphCompletelyOutsideTheTargetDrawsNothing) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 4, 4, 255));
  const DisplayList list = {
      one_glyph(1, Point{100, 100}),
      one_glyph(1, Point{-100, -100}),
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, square(4), glyphs)), 0.0);
}

// 巨大・非有限な原点でも落ちない（そのグリフは無視する）。
TEST(RasterGlyph, ExtremeOriginsAreIgnored) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 2, 2, 255));
  const DisplayList list = {
      one_glyph(1, Point{kNaN, 0}),          one_glyph(1, Point{0, kNaN}),
      one_glyph(1, Point{kInf, kInf}),       one_glyph(1, Point{-kInf, 0}),
      one_glyph(1, Point{1.0e30F, 1.0e30F}),
  };
  EXPECT_EQ(coverage_sum(must_rasterize(list, square(4), glyphs)), 0.0);
}

TEST(RasterGlyph, BlankGlyphDrawsNothing) {
  FakeGlyphSource glyphs;
  glyphs.set(1, GlyphBitmap{});  // 空白グリフ
  EXPECT_EQ(coverage_sum(must_rasterize({one_glyph(1, Point{0, 0})}, square(4), glyphs)), 0.0);
}

TEST(RasterGlyph, NonPositiveOrNonFiniteSizeDrawsNothing) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 2, 2, 255));
  for (const float size : {0.0F, -1.0F, kNaN, kInf}) {
    DrawGlyphs cmd = one_glyph(1, Point{0, 0});
    cmd.size = size;
    EXPECT_EQ(coverage_sum(must_rasterize({cmd}, square(4), glyphs)), 0.0) << "size " << size;
  }
}

// 契約（coverage.size() == width * height）を破ったビットマップは Internal エラー。
TEST(RasterGlyph, MalformedGlyphBitmapIsInternalError) {
  FakeGlyphSource glyphs;
  GlyphBitmap broken;
  broken.width = 4;
  broken.height = 4;
  broken.coverage.assign(3, 255);
  glyphs.set(1, broken);
  const Error e = must_fail({one_glyph(1, Point{0, 0})}, square(4), glyphs);
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_NE(e.message.find("GlyphSource"), std::string::npos);
}

// issue #3: GlyphSource の失敗は「空白グリフ」に化けさせず、そのまま伝播する。
TEST(RasterGlyph, GlyphSourceFailureIsPropagated) {
  FakeGlyphSource glyphs;
  glyphs.set_error(1, Error{.kind = ErrorKind::FontLoad,
                            .message = "text: グリフを読めません",
                            .location = std::nullopt,
                            .hint = {},
                            .warning = std::nullopt});
  const Error e = must_fail({one_glyph(1, Point{0, 0})}, square(4), glyphs);
  EXPECT_EQ(e.kind, ErrorKind::FontLoad);
  EXPECT_EQ(e.message, "text: グリフを読めません");
}

// 途中のグリフが失敗したら、そこで止めてエラーを返す（描けたぶんだけ返さない）。
TEST(RasterGlyph, FailureInTheMiddleOfARunStopsTheWholeRasterization) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 1, 1, 255));
  glyphs.set_error(2, Error{.kind = ErrorKind::Internal,
                            .message = "text: FontId がありません",
                            .location = std::nullopt,
                            .hint = {},
                            .warning = std::nullopt});
  DrawGlyphs cmd = one_glyph(1, Point{0, 0});
  cmd.glyphs = {GlyphInstance{1, Point{0, 0}}, GlyphInstance{2, Point{1, 0}},
                GlyphInstance{1, Point{2, 0}}};
  const Error e = must_fail({cmd}, square(4), glyphs);
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_EQ(glyphs.calls().size(), 2U) << "失敗したグリフより後ろを読んでいる";
}

// 空のビットマップなのに被覆率が入っている（契約違反）ものも Internal。
TEST(RasterGlyph, EmptyBitmapWithCoverageIsInternalError) {
  FakeGlyphSource glyphs;
  GlyphBitmap broken;  // width = height = 0 なのに coverage がある
  broken.coverage.assign(4, 255);
  glyphs.set(1, broken);
  const Error e = must_fail({one_glyph(1, Point{0, 0})}, square(4), glyphs);
  EXPECT_EQ(e.kind, ErrorKind::Internal);
  EXPECT_NE(e.message.find("GlyphSource"), std::string::npos);
}

TEST(RasterGlyph, ClipAppliesToGlyphs) {
  FakeGlyphSource glyphs;
  glyphs.set(1, solid_glyph(0, 0, 4, 4, 255));
  const DisplayList list = {
      PushClip{Rect{0, 0, 2, 4}, 0},
      one_glyph(1, Point{0, 0}),
      PopClip{},
  };
  const Bitmap b = must_rasterize(list, square(4), glyphs);
  EXPECT_EQ(alpha_at(b, 1, 1), 255U);
  EXPECT_EQ(alpha_at(b, 2, 1), 0U);
}

}  // namespace
}  // namespace shashoku::raster::test
