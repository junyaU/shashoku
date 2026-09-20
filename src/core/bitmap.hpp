#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/color.hpp"

namespace shashoku {

// ⑤b の出力 / ⑥ の入力。ストレートアルファの RGBA8、行優先、パディングなし。
// rgba.size() == width * height * 4 が不変条件。
struct Bitmap {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;

  Bitmap() = default;
  Bitmap(std::uint32_t w, std::uint32_t h, Color fill = kTransparent)
      : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4) {
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i] = fill.r;
      rgba[i + 1] = fill.g;
      rgba[i + 2] = fill.b;
      rgba[i + 3] = fill.a;
    }
  }

  [[nodiscard]] std::size_t offset(std::uint32_t x, std::uint32_t y) const {
    return (static_cast<std::size_t>(y) * width + x) * 4;
  }

  [[nodiscard]] Color pixel(std::uint32_t x, std::uint32_t y) const {
    const std::size_t i = offset(x, y);
    return Color{rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
  }

  void set_pixel(std::uint32_t x, std::uint32_t y, Color c) {
    const std::size_t i = offset(x, y);
    rgba[i] = c.r;
    rgba[i + 1] = c.g;
    rgba[i + 2] = c.b;
    rgba[i + 3] = c.a;
  }

  bool operator==(const Bitmap&) const = default;
};

}  // namespace shashoku
