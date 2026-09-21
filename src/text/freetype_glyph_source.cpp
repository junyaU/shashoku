#include "text/freetype_glyph_source.hpp"

#include <ft2build.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <freetype/freetype.h>
#include <freetype/ftoutln.h>

#include "core/ids.hpp"
#include "core/number_text.hpp"
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

// 実行ごとのグリフキャッシュの容量（A34）。**RenderLimits には入れない**: A25 の上限は
// すべて「入力の一部」で、同じ入力 + 同じ上限なら同じ出力、という性質を持つ。キャッシュの
// 容量は出力に一切影響しないので、混ぜると「上限」の意味が 2 種類になる。
// 16 MiB は font_size_device_px の既定（2048 px → 2048^2 = 4 MiB）のグリフが数個入る大きさ。
constexpr std::size_t kGlyphCacheBytes = std::size_t{16} * 1024 * 1024;

// どのグリフで何が起きたかを必ず message に入れる（DESIGN.md §3-6 fail loudly）。
std::string where(FontId font, GlyphId glyph_id, float pixel_size) {
  // 値は number_text() を通す: NaN の符号ビットは CPU によって違うので、そのまま出すと
  // 同じ入力でも文面が環境で変わる（core/number_text.hpp）。
  return std::format("FontId {}, glyph {}, {} px", font, glyph_id, number_text(pixel_size));
}

std::string ft_failure(std::string_view function, FT_Error error, FontId font, GlyphId glyph_id,
                       float pixel_size) {
  return std::format("cannot rasterize the glyph ({}): {} failed: {} (0x{:02X})",
                     where(font, glyph_id, pixel_size), function, detail::ft_error_text(error),
                     static_cast<unsigned int>(error));
}

}  // namespace

namespace detail {

// キャッシュの鍵。pixel_size は**ビット列**で持つ（浮動小数の等値比較を避ける）。
// -0.0 と +0.0 が別の鍵になるが、pixel_size が有限かつ 0 より大きいことは呼び出し側の
// 責務（glyph_source.hpp / A19）で、そうでない値はキャッシュを引く前に弾いている。
struct GlyphKey {
  FontId font = 0;
  GlyphId glyph = 0;
  std::uint32_t pixel_size_bits = 0;
  std::uint8_t sideways = 0;  // bool を既定比較に混ぜないための 0/1

  [[nodiscard]] auto operator<=>(const GlyphKey&) const = default;
};

// 1 項目のメモリ見積もり。被覆率のバイト数に鍵とヘッダの分を足すので、空白グリフ
// （被覆率 0 バイト）でも項目数が自然に頭打ちになる（std::map のノードの実費は数えない）。
inline std::size_t entry_cost(const raster::GlyphBitmap& bitmap) {
  return bitmap.coverage.size() + sizeof(GlyphKey) + sizeof(raster::GlyphBitmap);
}

// 1 回の render ぶんの FreeType の状態とグリフキャッシュ。1 スレッド専用（A34）。
struct GlyphRuntime {
  const FontStore* fonts = nullptr;
  FT_Library library = nullptr;
  FT_Error library_error = 0;
  std::vector<FT_Face> faces;  // FontId → FT_Face（遅延生成）
  // 反復しないので順序は出力に影響しないが、決定的な容器を使う（DESIGN.md §3-5）。
  std::map<GlyphKey, raster::GlyphBitmap> cache;
  std::size_t capacity_bytes = kGlyphCacheBytes;
  std::size_t cached_bytes = 0;
  std::size_t hits = 0;
  std::size_t misses = 0;

  // library は宣言順で library_error より先に nullptr に初期化されるので、
  // library_error の初期化子の中で FT_Init_FreeType に渡してよい。
  GlyphRuntime(const FontStore& store, std::size_t capacity)
      : fonts(&store), library_error(FT_Init_FreeType(&library)), capacity_bytes(capacity) {
    if (library_error != 0) {
      library = nullptr;
    }
  }
  GlyphRuntime(const GlyphRuntime&) = delete;
  GlyphRuntime& operator=(const GlyphRuntime&) = delete;
  GlyphRuntime(GlyphRuntime&&) = delete;
  GlyphRuntime& operator=(GlyphRuntime&&) = delete;

  ~GlyphRuntime() {
    for (FT_Face face : faces) {  // face は library より先に解放する
      if (face != nullptr) {
        FT_Done_Face(face);
      }
    }
    if (library != nullptr) {
      FT_Done_FreeType(library);
    }
  }

  // 共有資源のバイト列から、この実行だけの FT_Face を作る（初回だけ）。
  Result<FT_Face> face(const FontEntry& entry, FontId font, GlyphId glyph_id, float pixel_size) {
    if (library == nullptr) {
      return fail(ErrorKind::Internal,
                  std::format("cannot rasterize the glyph ({}): cannot initialize FreeType: {}",
                              where(font, glyph_id, pixel_size), ft_error_text(library_error)));
    }
    if (faces.size() <= font) {
      faces.resize(std::size_t{font} + 1, nullptr);
    }
    if (faces[font] != nullptr) {
      return faces[font];
    }
    FT_Face created = nullptr;
    const FT_Error error =
        FT_New_Memory_Face(library, entry.bytes->data(), static_cast<FT_Long>(entry.bytes->size()),
                           entry.face_index, &created);
    if (error != 0 || created == nullptr) {
      return fail(ErrorKind::FontLoad,
                  ft_failure("FT_New_Memory_Face", error, font, glyph_id, pixel_size));
    }
    faces[font] = created;
    return created;
  }

  // 容量を超えたら**新規登録をやめるだけ**で、既にある項目は追い出さない。
  // 追い出すと「何が残っているか」が呼ばれ方に依存するが、残す方は依存しない
  // （どちらにしても出力は変わらないが、説明が簡単で再現しやすい方を採る）。
  void remember(const GlyphKey& key, const raster::GlyphBitmap& bitmap) {
    const std::size_t cost = entry_cost(bitmap);
    if (cost > capacity_bytes || cached_bytes > capacity_bytes - cost) {
      return;
    }
    cache.emplace(key, bitmap);
    cached_bytes += cost;
  }
};

}  // namespace detail

FreeTypeGlyphSource::FreeTypeGlyphSource(const FontStore& fonts)
    : FreeTypeGlyphSource(fonts, kGlyphCacheBytes) {}

FreeTypeGlyphSource::FreeTypeGlyphSource(const FontStore& fonts, std::size_t cache_capacity_bytes)
    : impl_(std::make_unique<detail::GlyphRuntime>(fonts, cache_capacity_bytes)) {}

FreeTypeGlyphSource::~FreeTypeGlyphSource() = default;

FreeTypeGlyphSource::CacheStats FreeTypeGlyphSource::cache_stats() const noexcept {
  return CacheStats{.hits = impl_->hits,
                    .misses = impl_->misses,
                    .entries = impl_->cache.size(),
                    .bytes = impl_->cached_bytes};
}

Result<raster::GlyphBitmap> FreeTypeGlyphSource::rasterize(FontId font, GlyphId glyph_id,
                                                           float pixel_size, bool sideways) {
  const detail::FontEntry* entry = detail::FontStoreAccess::impl(*impl_->fonts).at(font);
  if (entry == nullptr) {
    return fail(ErrorKind::Internal,
                std::format("cannot rasterize the glyph ({}): no such FontId in the FontStore",
                            where(font, glyph_id, pixel_size)));
  }
  // pixel_size の有限性・正値は呼び出し側の責務（glyph_source.hpp）。破られたらバグ。
  if (!std::isfinite(pixel_size) || pixel_size <= 0.0F) {
    return fail(
        ErrorKind::Internal,
        std::format("cannot rasterize the glyph ({}): pixel_size must be finite and greater than 0",
                    where(font, glyph_id, pixel_size)));
  }
  if (pixel_size > kMaxPixelSize) {
    return fail(
        ErrorKind::FontLoad,
        std::format("cannot rasterize the glyph ({}): pixel_size exceeds the limit of {} px",
                    where(font, glyph_id, pixel_size), kMaxPixelSize));
  }

  // 小数のピクセルサイズは 26.6 固定小数のまま FreeType に渡す（A8 の丸めは呼び出し側）。
  const auto size = static_cast<FT_F26Dot6>(std::lround(static_cast<double>(pixel_size) * 64.0));
  if (size <= 0) {
    // 1/128 px 未満。26.6 では 0 になり FreeType に渡せないが、この解像度では
    // どのみち描くものがない（= 空白グリフと同じ扱い）。
    return raster::GlyphBitmap{};
  }

  // 契約違反とエラーを先に判定してからキャッシュを引く（キャッシュの有無で
  // エラーの出方まで変わらないようにする）。
  const detail::GlyphKey key{.font = font,
                             .glyph = glyph_id,
                             .pixel_size_bits = std::bit_cast<std::uint32_t>(pixel_size),
                             .sideways = sideways ? std::uint8_t{1} : std::uint8_t{0}};
  if (const auto it = impl_->cache.find(key); it != impl_->cache.end()) {
    ++impl_->hits;
    return it->second;
  }
  ++impl_->misses;

  const Result<FT_Face> found = impl_->face(*entry, font, glyph_id, pixel_size);
  if (!found) {
    return std::unexpected(found.error());
  }
  FT_Face face = *found;
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
                std::format("cannot rasterize the glyph ({}): the glyph has no outline "
                            "(embedded bitmap / color glyph)",
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

  raster::GlyphBitmap out;
  const FT_Bitmap& bitmap = slot->bitmap;
  if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.buffer == nullptr) {
    // 空白グリフ（輪郭はあるが塗る面積がない）。これも覚えておく（FT_Load_Glyph を省ける）。
    impl_->remember(key, out);
    return out;
  }
  if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
    return fail(ErrorKind::FontLoad,
                std::format("cannot rasterize the glyph ({}): unsupported pixel_mode {} "
                            "(8-bit grayscale only)",
                            where(font, glyph_id, pixel_size),
                            static_cast<unsigned int>(bitmap.pixel_mode)));
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
  impl_->remember(key, out);
  return out;
}

}  // namespace shashoku::text
