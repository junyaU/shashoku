#pragma once

#include "core/ids.hpp"
#include "raster/glyph_source.hpp"

namespace shashoku::text {

class FontStore;

// ⑤b ラスタライザに注入するグリフ供給元（ARCHITECTURE.md §3.3 / §3.5）。
// FontStore を参照するだけで所有しない。FontStore はこれより長生きすること。
//
// ヒンティングなし・埋め込みビットマップなし（A8）。同じ引数には必ず同じ被覆率を返す。
class FreeTypeGlyphSource final : public raster::GlyphSource {
 public:
  explicit FreeTypeGlyphSource(const FontStore& fonts) : fonts_(&fonts) {}
  ~FreeTypeGlyphSource() override = default;
  FreeTypeGlyphSource(const FreeTypeGlyphSource&) = delete;
  FreeTypeGlyphSource& operator=(const FreeTypeGlyphSource&) = delete;
  FreeTypeGlyphSource(FreeTypeGlyphSource&&) = delete;
  FreeTypeGlyphSource& operator=(FreeTypeGlyphSource&&) = delete;

  // 不正な FontId / glyph_id、空白グリフはいずれも空のビットマップ（落ちない）。
  raster::GlyphBitmap rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                bool sideways) override;

 private:
  const FontStore* fonts_;
};

}  // namespace shashoku::text
