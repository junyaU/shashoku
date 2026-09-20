#include "text/freetype_glyph_source.hpp"

#include <ft2build.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <freetype/freetype.h>
#include <freetype/ftoutln.h>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/glyph_source.hpp"
#include "shashoku/error.hpp"
#include "text/font_store.hpp"
#include "text/font_store_impl.hpp"

namespace shashoku::text {
namespace {

constexpr FT_Fixed kFixedOne16 = 0x10000;  // FT_Matrix の 16.16 固定小数の 1.0
constexpr FT_UInt kDpi = 72;  // 72 dpi なら 26.6 の「ポイント数」= ピクセル数

// 26.6 固定小数に収まる上限（raster の 1 辺あたりの最大デバイスピクセル数と同じ 2^26）。
// これを超える pixel_size は FreeType に渡せない（lround の結果が long に収まらない）。
constexpr float kMaxPixelSize = 67108864.0F;

// 時計回りに 90°（画面座標。FreeType の輪郭は y 上向きなので (X, Y) → (Y, -X)）。
constexpr FT_Matrix kClockwiseQuarterTurn{0, kFixedOne16, -kFixedOne16, 0};

// どのグリフで何が起きたかを必ず message に入れる（DESIGN.md §3-6 fail loudly）。
std::string where(FontId font, GlyphId glyph_id, float pixel_size) {
  return std::format("FontId {}, glyph {}, {} px", font, glyph_id, pixel_size);
}

std::string ft_failure(std::string_view function, FT_Error error, FontId font, GlyphId glyph_id,
                       float pixel_size) {
  return std::format("グリフをラスタライズできません ({}): {} が失敗しました: {} (0x{:02X})",
                     where(font, glyph_id, pixel_size), function, detail::ft_error_text(error),
                     static_cast<unsigned int>(error));
}

}  // namespace

Result<raster::GlyphBitmap> FreeTypeGlyphSource::rasterize(FontId font, GlyphId glyph_id,
                                                           float pixel_size, bool sideways) {
  const detail::FontEntry* entry = detail::FontStoreAccess::impl(*fonts_).at(font);
  if (entry == nullptr || entry->ft_face == nullptr) {
    return fail(ErrorKind::Internal,
                std::format("グリフをラスタライズできません ({}): FontStore にその FontId が"
                            "ありません",
                            where(font, glyph_id, pixel_size)));
  }
  // pixel_size の有限性・正値は呼び出し側の責務（glyph_source.hpp）。破られたらバグ。
  if (!std::isfinite(pixel_size) || pixel_size <= 0.0F) {
    return fail(ErrorKind::Internal,
                std::format("グリフをラスタライズできません ({}): pixel_size は有限で 0 より"
                            "大きいこと",
                            where(font, glyph_id, pixel_size)));
  }
  if (pixel_size > kMaxPixelSize) {
    return fail(ErrorKind::FontLoad,
                std::format("グリフをラスタライズできません ({}): pixel_size が上限 {} px を"
                            "超えています",
                            where(font, glyph_id, pixel_size), kMaxPixelSize));
  }

  // 小数のピクセルサイズは 26.6 固定小数のまま FreeType に渡す（A8 の丸めは呼び出し側）。
  const auto size = static_cast<FT_F26Dot6>(std::lround(static_cast<double>(pixel_size) * 64.0));
  if (size <= 0) {
    // 1/128 px 未満。26.6 では 0 になり FreeType に渡せないが、この解像度では
    // どのみち描くものがない（= 空白グリフと同じ扱い）。
    return raster::GlyphBitmap{};
  }

  FT_Face face = entry->ft_face;
  if (const FT_Error error = FT_Set_Char_Size(face, size, size, kDpi, kDpi); error != 0) {
    return fail(ErrorKind::FontLoad,
                ft_failure("FT_Set_Char_Size", error, font, glyph_id, pixel_size));
  }
  if (const FT_Error error = FT_Load_Glyph(face, glyph_id, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
      error != 0) {
    return fail(ErrorKind::FontLoad,
                ft_failure("FT_Load_Glyph", error, font, glyph_id, pixel_size));
  }

  FT_GlyphSlot slot = face->glyph;
  if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
    // 輪郭を持たないグリフ（埋め込みビットマップ・カラーグリフ）は描けないし回せない。
    // 黙って空白にすると文字の抜けた PNG が「成功」になる（issue #3）。
    return fail(ErrorKind::FontLoad,
                std::format("グリフをラスタライズできません ({}): グリフが輪郭を持ちません"
                            "（埋め込みビットマップ / カラーグリフ）",
                            where(font, glyph_id, pixel_size)));
  }
  if (sideways) {
    // ビットマップではなく輪郭を回す（ARCHITECTURE.md §3.5）。
    FT_Outline_Transform(&slot->outline, &kClockwiseQuarterTurn);
  }
  if (const FT_Error error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL); error != 0) {
    return fail(ErrorKind::FontLoad,
                ft_failure("FT_Render_Glyph", error, font, glyph_id, pixel_size));
  }

  const FT_Bitmap& bitmap = slot->bitmap;
  if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.buffer == nullptr) {
    return raster::GlyphBitmap{};  // 空白グリフ（輪郭はあるが塗る面積がない）
  }
  if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
    return fail(ErrorKind::FontLoad,
                std::format("グリフをラスタライズできません ({}): 未対応の pixel_mode {} です"
                            "（8bit グレースケールのみ）",
                            where(font, glyph_id, pixel_size),
                            static_cast<unsigned int>(bitmap.pixel_mode)));
  }

  raster::GlyphBitmap out;
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
