#pragma once

#include <cstdint>
#include <vector>

#include "core/ids.hpp"
#include "core/result.hpp"

namespace shashoku::raster {

// グリフ 1 個ぶんの被覆率ビットマップ（8bit、0 = 透明 / 255 = 完全に覆う）。
// 位置の約束は FreeType と同じ: グリフ原点から見て、ビットマップ左端が +left px（右が正）、
// 上端が +top px（上が正）。つまり左上ピクセルのデバイス座標は (origin.x + left, origin.y - top)。
// 不変条件: coverage.size() == width * height。
struct GlyphBitmap {
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> coverage;  // width * height、行優先。空白グリフは空

  bool operator==(const GlyphBitmap&) const = default;
};

// ラスタライザに注入するグリフ供給元。ラスタライザは FreeType もフォントも知らない
// （DESIGN.md §7: 「被覆率 AA の自作ラスタライザに差し替えられるようインターフェースを切る」）。
// 実装は text モジュール（FreeType）。ラスタライザのテストは手書きビットマップを返す偽物を使う。
class GlyphSource {
 public:
  GlyphSource() = default;
  GlyphSource(const GlyphSource&) = delete;
  GlyphSource& operator=(const GlyphSource&) = delete;
  // 注入点なのでコピーも移動もしない（明示しないと cppcoreguidelines-special-member-functions）
  GlyphSource(GlyphSource&&) = delete;
  GlyphSource& operator=(GlyphSource&&) = delete;
  virtual ~GlyphSource() = default;

  // pixel_size はデバイスピクセル単位（= DrawGlyphs::size * scale）。**有限かつ 0 より大きい**
  // 値を渡すのは呼び出し側の責務（ラスタライザは非有限・非正のコマンドを読み飛ばす）。
  // 破ったときの扱いは実装に任せる（FreeTypeGlyphSource は Internal エラーを返す）。
  // sideways = true なら時計回りに 90° 回したビットマップを返す（left / top も回転後の値）。
  // ヒンティングなし・埋め込みビットマップなしで、同じ引数には必ず同じ結果を返すこと。
  //
  // 成功で空のビットマップ（width == height == 0・coverage も空）を返してよいのは
  // **本当に描くものがないグリフ**（空白。輪郭はあるが塗る面積が 0）だけ。
  // 描けなかったとき（フォントが引けない・グリフが読めない・未対応の形式）は必ずエラーを返す。
  // 失敗を空白として返すと、文字の抜けた PNG が「成功」として出る（issue #3）。
  //   Internal  … 呼び出し側の契約違反（不正な FontId、非有限・非正の pixel_size）
  //   FontLoad  … フォント / グリフをラスタライズできない
  virtual Result<GlyphBitmap> rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                        bool sideways) = 0;
};

}  // namespace shashoku::raster
