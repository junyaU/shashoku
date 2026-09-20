#pragma once

#include <cstdint>
#include <span>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"

namespace shashoku::raster {

// 出力ビットマップのデバイス画素数の上限の既定（2^26 = 67,108,864。RGBA で 256 MB）。
//
// 利用者が調整する上限は `RenderLimits::device_pixels` に一本化してあり（A21）、api は
// 必ずそちらの値を Target に入れる。ここの定数は raster を単体で使うときの既定で、値は
// RenderLimits の既定と同じ（食い違わないことを src/api/render.cpp が static_assert する）。
inline constexpr std::uint64_t kMaxDevicePixels = std::uint64_t{1} << 26U;

// ラスタライズ先の指定（ARCHITECTURE.md §3.3）。
// デバイスピクセル数 = ceil(CSS px * scale)。
struct Target {
  float width = 0;   // CSS px
  float height = 0;  // CSS px
  float scale = 1;
  Color background = kTransparent;
  // 幅 x 高さ（デバイス画素）の上限。ピクセルバッファを確保する前に判定する。
  std::uint64_t max_device_pixels = kMaxDevicePixels;

  bool operator==(const Target&) const = default;
};

// ⑤b: ディスプレイリスト → Bitmap。
//
// 決定性が最優先（DESIGN.md §3-5 / ARCHITECTURE.md A9）: 同じ入力からは必ず同じ Bitmap が出る。
// 内部で使う浮動小数点演算は `+ - * /` / sqrt / floor,ceil,round / min,max,abs のみ。
// ピクセルの合成は 8bit 整数で行い、丸めはすべて四捨五入に統一する。
//
// 合成は source-over・ストレートアルファ・sRGB 空間のまま。アルファ 0 のピクセルの RGB は
// 0 に正規化される。被覆率（0..255）× クリップ × 色のアルファ → 実効アルファ、という経路は
// 1 本しかなく、矩形・角丸・枠線・グリフ・画像・クリップがすべてそこを通る。
//
// エラー:
//   InvalidOption  target の幅・高さ・scale が不正（非正 / 非有限）
//   LimitExceeded  デバイス画素数が target.max_device_pixels を超える
//   Internal       PushClip / PopClip の対応が取れていない、DrawImage の ID が範囲外、
//                  GlyphSource / images が契約を満たさないビットマップを返した
//
// 座標や寸法に NaN / Inf が混ざったコマンドは無視する（落ちない）。
// ターゲットの外・クリップの外へはみ出す描画も安全に切り落とす。
Result<Bitmap> rasterize(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                         std::span<const Bitmap> images = {});

}  // namespace shashoku::raster
