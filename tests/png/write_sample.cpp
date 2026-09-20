// encode の出力を外部の実装（pngcheck）に検査させるための小さな書き出しツール。
//   png_write_sample <pattern> <output.png> [圧縮レベル 0-9]
// CTest からは tests/png/pngcheck.cmake 経由で呼ばれる（全レベルを通す。A32）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
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
  // 以下はフィルタ選択の最適化（A32）の前後でバイト列が一致することを確かめるための大きめの絵。
  // 5 種のフィルタがどれも選ばれるように、傾き・平坦・ノイズ・極端な縦横比を混ぜてある。
  if (pattern == "big-gradient") {
    return make_gradient(1200, 630);
  }
  if (pattern == "solid-big") {
    return {1200, 630, Color{0x12, 0x26, 0x3F, 0xFF}};
  }
  if (pattern == "photo") {
    // 写真風: なめらかな 2 次元の傾きに弱いノイズを乗せる（Paeth が勝ちやすい絵）。
    Bitmap bitmap(800, 600);
    Rng rng(4649);
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
      for (std::uint32_t x = 0; x < bitmap.width; ++x) {
        const int base = 40 + static_cast<int>((x / 3U) % 160U) + static_cast<int>((y / 5U) % 60U);
        const int noise = static_cast<int>(rng.byte() % 11U) - 5;
        const auto v = static_cast<std::uint8_t>(std::clamp(base + noise, 0, 255));
        bitmap.set_pixel(
            x, y,
            Color{v, static_cast<std::uint8_t>(255 - v), static_cast<std::uint8_t>(v / 2), 0xFF});
      }
    }
    return bitmap;
  }
  if (pattern == "wide") {
    // 幅が極端に広い絵（1 行が 16 KB を超える）。
    Bitmap bitmap(4096, 3);
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
      for (std::uint32_t x = 0; x < bitmap.width; ++x) {
        const auto v = static_cast<std::uint8_t>((x * 7U + (y * 53U)) & 0xFFU);
        bitmap.set_pixel(x, y, Color{v, static_cast<std::uint8_t>(x & 0xFFU), 0x10, 0xFF});
      }
    }
    return bitmap;
  }
  if (pattern == "tall") {
    // 高さだけが大きい絵（行の本数が多く、行ごとの固定費が効く）。
    Bitmap bitmap(3, 4096);
    Rng rng(20260920);
    for (std::uint8_t& v : bitmap.rgba) {
      v = rng.byte();
    }
    return bitmap;
  }
  return make_gradient(120, 80);
}

// "0"〜"9" だけを読む（from_chars を引っ張ってくるほどの用は無い）。
std::optional<int> parse_level(std::string_view text) {
  if (text.size() != 1 || text[0] < '0' || text[0] > '9') {
    return std::nullopt;
  }
  return text[0] - '0';
}

int run(std::span<char*> args) {
  if (args.size() != 3 && args.size() != 4) {
    std::cerr << "usage: png_write_sample <pattern> <output.png> [compression level 0-9]\n";
    return 2;
  }

  int level = shashoku::png::kDefaultCompressionLevel;
  if (args.size() == 4) {
    const std::optional<int> parsed = parse_level(args[3]);
    if (!parsed) {
      std::cerr << "compression level must be 0-9: " << args[3] << "\n";
      return 2;
    }
    level = *parsed;
  }

  const Bitmap bitmap = make_sample(args[1]);
  const shashoku::Result<std::vector<std::uint8_t>> encoded = shashoku::png::encode(bitmap, level);
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
