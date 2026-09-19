#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// PNG 仕様 付録 D の CRC-32。zlib の crc32() は使わない
// （PNG エンコーダは自作範囲。ARCHITECTURE.md §3.2）。

namespace shashoku::png {
namespace detail {

// 多項式 x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x+1 の
// 反転表現 0xEDB88320 で作る 256 段のテーブル。実行時の初期化を避けるため consteval。
consteval std::array<std::uint32_t, 256> make_crc_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::size_t n = 0; n < table.size(); ++n) {
    auto c = static_cast<std::uint32_t>(n);
    for (int k = 0; k < 8; ++k) {
      c = ((c & 1U) != 0U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
    }
    table[n] = c;
  }
  return table;
}

inline constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

}  // namespace detail

// チャンクの「型 + データ」に対する CRC-32。初期値 0xFFFFFFFF、最後に全ビット反転。
[[nodiscard]] constexpr std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
  std::uint32_t c = 0xFFFFFFFFU;
  for (const std::uint8_t b : data) {
    c = detail::kCrcTable[(c ^ static_cast<std::uint32_t>(b)) & 0xFFU] ^ (c >> 8U);
  }
  return c ^ 0xFFFFFFFFU;
}

}  // namespace shashoku::png
