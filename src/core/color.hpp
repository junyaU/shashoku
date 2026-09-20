#pragma once

#include <cstdint>

namespace shashoku {

// sRGB・ストレートアルファ（非乗算済み）の 8bit RGBA。PNG にそのまま書ける形。
// 合成は sRGB 空間のまま行う（ブラウザと同じ。リニア化しない）。
struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  [[nodiscard]] bool transparent() const { return a == 0; }

  bool operator==(const Color&) const = default;
};

inline constexpr Color kTransparent{0, 0, 0, 0};
inline constexpr Color kBlack{0, 0, 0, 255};
inline constexpr Color kWhite{255, 255, 255, 255};

}  // namespace shashoku
