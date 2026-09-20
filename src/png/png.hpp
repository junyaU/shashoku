#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "core/bitmap.hpp"
#include "core/result.hpp"

// ⑥ PNG エンコーダ / デコーダ（ARCHITECTURE.md §3.2）。
// 依存は core と zlib だけ。zlib は実装（.cpp）の中に閉じていて、このヘッダには漏れない。

namespace shashoku::png {

// PNG のシグネチャ（89 50 4E 47 0D 0A 1A 0A）。
inline constexpr std::array<std::uint8_t, 8> kSignature{0x89, 0x50, 0x4E, 0x47,
                                                        0x0D, 0x0A, 0x1A, 0x0A};

// decode が受け付ける最大ピクセル数（幅 × 高さ）の既定。
// 「巨大な寸法を宣言しただけの入力」でメモリを食い潰さないための上限で、IHDR を読んだ
// 時点（= 画素を確保する前）に判定する。
//
// 利用者が調整する上限は `RenderLimits::image_pixels` に一本化してあり（A25）、api は
// 必ずそちらの値を decode() に渡す。ここの定数は png を単体で使うときの既定で、値は
// RenderLimits の既定と同じ（食い違わないことを src/api/render.cpp が static_assert する）。
inline constexpr std::uint64_t kMaxPixels = std::uint64_t{1} << 24U;

// Bitmap（RGBA8 ストレートアルファ）→ PNG バイト列。
// 出力は color type 6 / bit depth 8 / 非インターレース固定。フィルタは行ごとに
// 5 種（None / Sub / Up / Average / Paeth）を試し、符号つきバイトとみなした絶対値和が
// 最小のものを選ぶ（PNG 仕様 §12.8。同点なら番号の小さい方）。zlib の設定も固定なので、
// 同じ Bitmap からは常にバイト単位で同じ結果が出る（DESIGN.md §3-5）。
//
// 幅か高さが 0、または rgba の長さが width*height*4 でない Bitmap は InvalidOption。
Result<std::vector<std::uint8_t>> encode(const Bitmap& bitmap);

// PNG バイト列 → Bitmap（RGBA8）。<img> とゴールデンテストの比較用。
// 信頼できない入力を受ける前提で、どんなバイト列でも落ちずに ImageDecode を返す。
//
// 対応: bit depth 8 / 16（16bit は上位 8bit に落とす）の gray / gray+alpha / RGB / RGBA、
// および bit depth 8 のパレット。tRNS は全対応形式で反映する。非インターレースのみ。
// 補助チャンク（gAMA / iCCP / tEXt など）は読み飛ばす。ガンマと ICC は無視する。
//
// max_pixels（幅 × 高さ）を超える画像は、画素を確保する前に LimitExceeded で返す。
Result<Bitmap> decode(std::span<const std::uint8_t> bytes, std::uint64_t max_pixels = kMaxPixels);

}  // namespace shashoku::png
