#pragma once

#include "core/ids.hpp"
#include "core/result.hpp"
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

  // 成功して空のビットマップを返すのは空白グリフ（描くものがない）だけ。
  // 不正な FontId・契約違反の pixel_size は Internal、FreeType の失敗・輪郭を持たない
  // グリフ・未対応の pixel_mode は FontLoad（glyph_source.hpp の契約。issue #3）。
  Result<raster::GlyphBitmap> rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                        bool sideways) override;

 private:
  const FontStore* fonts_;
};

}  // namespace shashoku::text
