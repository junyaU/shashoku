#pragma once

#include <cstdint>

namespace shashoku {

// 重いデータ（フォント実体・画像ピクセル）は所有者が 1 箇所に持ち、ツリーや
// ディスプレイリストには ID だけを載せる（DESIGN.md §3-2）。
using FontId = std::uint32_t;   // text::FontStore が払い出す
using ImageId = std::uint32_t;  // render() に渡された画像テーブルの添字
using GlyphId = std::uint16_t;  // フォント内ローカル。OpenType のグリフ ID は 16bit

}  // namespace shashoku
