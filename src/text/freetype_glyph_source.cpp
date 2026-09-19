#include "text/freetype_glyph_source.hpp"

#include <ft2build.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <freetype/freetype.h>
#include <freetype/ftoutln.h>

#include "core/ids.hpp"
#include "raster/glyph_source.hpp"
#include "text/font_store.hpp"
#include "text/font_store_impl.hpp"

namespace shashoku::text {
namespace {

constexpr FT_Fixed kFixedOne16 = 0x10000;  // FT_Matrix の 16.16 固定小数の 1.0
constexpr FT_UInt kDpi = 72;  // 72 dpi なら 26.6 の「ポイント数」= ピクセル数

// 時計回りに 90°（画面座標。FreeType の輪郭は y 上向きなので (X, Y) → (Y, -X)）。
constexpr FT_Matrix kClockwiseQuarterTurn{0, kFixedOne16, -kFixedOne16, 0};

}  // namespace

raster::GlyphBitmap FreeTypeGlyphSource::rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                                   bool sideways) {
  raster::GlyphBitmap out;

  const detail::FontEntry* entry = detail::FontStoreAccess::impl(*fonts_).at(font);
  if (entry == nullptr || entry->ft_face == nullptr) {
    return out;
  }
  // 小数のピクセルサイズは 26.6 固定小数のまま FreeType に渡す（A8 の丸めは呼び出し側）。
  const auto size = static_cast<FT_F26Dot6>(std::lround(pixel_size * 64.0F));
  if (size <= 0) {
    return out;
  }

  FT_Face face = entry->ft_face;
  if (FT_Set_Char_Size(face, size, size, kDpi, kDpi) != 0) {
    return out;
  }
  if (FT_Load_Glyph(face, glyph_id, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
    return out;
  }

  FT_GlyphSlot slot = face->glyph;
  if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
    return out;  // 輪郭を持たないグリフは扱わない（回転もできない）
  }
  if (sideways) {
    // ビットマップではなく輪郭を回す（ARCHITECTURE.md §3.5）。
    FT_Outline_Transform(&slot->outline, &kClockwiseQuarterTurn);
  }
  if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
    return out;
  }

  const FT_Bitmap& bitmap = slot->bitmap;
  if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.buffer == nullptr) {
    return out;  // 空白グリフ
  }
  if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
    return out;
  }

  out.left = slot->bitmap_left;
  out.top = slot->bitmap_top;
  out.width = bitmap.width;
  out.height = bitmap.rows;
  out.coverage.resize(static_cast<std::size_t>(bitmap.width) * bitmap.rows);

  // pitch は負（下の行から並ぶ）になりうる。
  const std::ptrdiff_t pitch = bitmap.pitch;
  for (unsigned int y = 0; y < bitmap.rows; ++y) {
    const std::ptrdiff_t row = pitch >= 0
                                   ? static_cast<std::ptrdiff_t>(y) * pitch
                                   : (static_cast<std::ptrdiff_t>(bitmap.rows - 1 - y)) * -pitch;
    const unsigned char* src = bitmap.buffer + row;
    std::copy(src, src + bitmap.width,
              out.coverage.begin() + static_cast<std::ptrdiff_t>(y) * bitmap.width);
  }
  return out;
}

}  // namespace shashoku::text
