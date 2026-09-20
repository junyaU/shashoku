#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster::test {

Color rgba(int r, int g, int b, int a);

// 手書きのビットマップを返す偽の GlyphSource。呼ばれた引数を記録する。
class FakeGlyphSource : public GlyphSource {
 public:
  struct Call {
    FontId font = 0;
    GlyphId glyph_id = 0;
    float pixel_size = 0;
    bool sideways = false;

    bool operator==(const Call&) const = default;
  };

  void set(GlyphId glyph_id, GlyphBitmap bitmap);
  // このグリフを要求されたら失敗を返す（本物の GlyphSource の失敗を模す）。
  void set_error(GlyphId glyph_id, Error error);
  Result<GlyphBitmap> rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                bool sideways) override;

  [[nodiscard]] const std::vector<Call>& calls() const { return calls_; }

 private:
  // 反復順が決まる map を使う（DESIGN.md §3-5: unordered_map の反復順を出力に影響させない）
  std::map<GlyphId, GlyphBitmap> glyphs_;
  std::map<GlyphId, Error> errors_;
  std::vector<Call> calls_;
};

// 被覆率が一様なグリフビットマップ。
GlyphBitmap solid_glyph(std::int32_t left, std::int32_t top, std::uint32_t width,
                        std::uint32_t height, std::uint8_t coverage);

// 行ごとに被覆率を書いたグリフビットマップ。
GlyphBitmap glyph_rows(std::int32_t left, std::int32_t top,
                       std::initializer_list<std::vector<std::uint8_t>> rows);

// 行優先のピクセル列から Bitmap を作る。
Bitmap make_image(std::uint32_t width, std::uint32_t height, const std::vector<Color>& pixels);

// rasterize() が成功することを確かめて Bitmap を返す（失敗したらテストを落として空を返す）。
Bitmap must_rasterize(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                      std::span<const Bitmap> images = {});
Bitmap must_rasterize(const DisplayList& list, const Target& target);

// rasterize() が失敗することを確かめてエラーを返す。
Error must_fail(const DisplayList& list, const Target& target);
Error must_fail(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                std::span<const Bitmap> images = {});

// ピクセルの一致を読みやすいメッセージ付きで検査する。
testing::AssertionResult pixel_is(const Bitmap& bitmap, std::uint32_t x, std::uint32_t y,
                                  Color want);

// 全ピクセルのアルファの合計 / 255（= 被覆率の総和）。
double coverage_sum(const Bitmap& bitmap);

}  // namespace shashoku::raster::test
