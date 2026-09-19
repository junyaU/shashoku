// encode の出力を外部の実装（pngcheck）に検査させるための小さな書き出しツール。
//   png_write_sample <pattern> <output.png>
// CTest からは tests/png/pngcheck.cmake 経由で呼ばれる。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/png.hpp"
#include "shashoku/error.hpp"

namespace {

using shashoku::Bitmap;
using shashoku::Color;

// 決定的な擬似乱数（xorshift64*）。
class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed | 1U) {}
  std::uint8_t byte() {
    state_ ^= state_ >> 12U;
    state_ ^= state_ << 25U;
    state_ ^= state_ >> 27U;
    return static_cast<std::uint8_t>((state_ * 0x2545F4914F6CDD1DULL) >> 56U);
  }

 private:
  std::uint64_t state_;
};

Bitmap make_gradient(std::uint32_t width, std::uint32_t height) {
  Bitmap bitmap(width, height);
  const std::uint32_t last_x = std::max(width - 1, 1U);
  const std::uint32_t last_y = std::max(height - 1, 1U);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      bitmap.set_pixel(x, y,
                       Color{static_cast<std::uint8_t>(x * 255U / last_x),
                             static_cast<std::uint8_t>(y * 255U / last_y), 0x40, 255});
    }
  }
  return bitmap;
}

Bitmap make_sample(std::string_view pattern) {
  if (pattern == "tiny") {
    return {1, 1, Color{0x12, 0x34, 0x56, 0x78}};
  }
  if (pattern == "solid") {
    return {64, 48, Color{0x20, 0x40, 0x80, 0xFF}};
  }
  if (pattern == "odd") {
    Bitmap bitmap(7, 13, Color{0, 0, 0, 255});
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
      bitmap.set_pixel(y % bitmap.width, y, Color{255, 255, 255, 255});
    }
    return bitmap;
  }
  if (pattern == "alpha") {
    Bitmap bitmap(40, 40);
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
      for (std::uint32_t x = 0; x < bitmap.width; ++x) {
        bitmap.set_pixel(x, y,
                         Color{static_cast<std::uint8_t>(x * 6), 0x80,
                               static_cast<std::uint8_t>(y * 6), static_cast<std::uint8_t>(x * 6)});
      }
    }
    return bitmap;
  }
  if (pattern == "noise") {
    Bitmap bitmap(53, 31);
    Rng rng(20260919);
    for (std::uint8_t& v : bitmap.rgba) {
      v = rng.byte();
    }
    return bitmap;
  }
  return make_gradient(120, 80);
}

int run(std::span<char*> args) {
  if (args.size() != 3) {
    std::cerr << "usage: png_write_sample <pattern> <output.png>\n";
    return 2;
  }

  const Bitmap bitmap = make_sample(args[1]);
  const shashoku::Result<std::vector<std::uint8_t>> encoded = shashoku::png::encode(bitmap);
  if (!encoded) {
    std::cerr << to_string(encoded.error()) << "\n";
    return 1;
  }

  std::ofstream out(args[2], std::ios::binary);
  out.write(reinterpret_cast<const char*>(encoded->data()),
            static_cast<std::streamsize>(encoded->size()));
  if (!out) {
    std::cerr << "cannot write " << args[2] << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // 例外はライブラリ境界を越えさせない（CLAUDE.md）。ツールの入口で必ず受け止める。
  try {
    return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
  } catch (...) {
    std::cerr << "png_write_sample: unexpected exception\n";
    return 1;
  }
}
