#pragma once

#include <span>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"

namespace shashoku::raster {

// ラスタライズ先の指定（ARCHITECTURE.md §3.3）。
// デバイスピクセル数 = ceil(CSS px * scale)。
struct Target {
  float width = 0;   // CSS px
  float height = 0;  // CSS px
  float scale = 1;
  Color background = kTransparent;

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
//   InvalidOption  target の幅・高さ・scale が不正（非正 / 非有限 / デバイスピクセルが巨大すぎる）
//   Internal       PushClip / PopClip の対応が取れていない、DrawImage の ID が範囲外、
//                  GlyphSource / images が契約を満たさないビットマップを返した
//
// 座標や寸法に NaN / Inf が混ざったコマンドは無視する（落ちない）。
// ターゲットの外・クリップの外へはみ出す描画も安全に切り落とす。
Result<Bitmap> rasterize(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                         std::span<const Bitmap> images = {});

}  // namespace shashoku::raster
