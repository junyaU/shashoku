#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <span>
#include <string_view>
#include <vector>

#include "core/bitmap.hpp"
#include "core/result.hpp"
#include "png/crc32.hpp"
#include "png/png.hpp"
#include "shashoku/error.hpp"

namespace shashoku::png {
namespace {

constexpr std::size_t kBytesPerPixel = 4;  // color type 6 / bit depth 8
constexpr std::uint8_t kColorTypeRgba = 6;
constexpr std::uint8_t kBitDepth = 8;
constexpr std::uint32_t kMaxChunkLength = 0x7FFFFFFFU;  // PNG のチャンク長は 31bit

// フィルタ種別（PNG 仕様 §9.2）。番号がそのままフィルタバイトになる。
enum class Filter : std::uint8_t { None = 0, Sub = 1, Up = 2, Average = 3, Paeth = 4 };
constexpr std::size_t kFilterCount = 5;

// PNG 仕様 §9.4 の PaethPredictor。a=左, b=上, c=左上。
std::uint8_t paeth_predictor(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  const int p = int{a} + int{b} - int{c};
  const int pa = std::abs(p - int{a});
  const int pb = std::abs(p - int{b});
  const int pc = std::abs(p - int{c});
  if (pa <= pb && pa <= pc) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

// 1 行に filter を適用して out に書く。raw / prior / out は同じ長さ。
void apply_filter(Filter filter, std::span<const std::uint8_t> raw,
                  std::span<const std::uint8_t> prior, std::span<std::uint8_t> out) {
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const std::uint8_t a = i >= kBytesPerPixel ? raw[i - kBytesPerPixel] : 0;
    const std::uint8_t b = prior[i];
    const std::uint8_t c = i >= kBytesPerPixel ? prior[i - kBytesPerPixel] : 0;
    std::uint8_t pred = 0;
    switch (filter) {
      case Filter::None:
        pred = 0;
        break;
      case Filter::Sub:
        pred = a;
        break;
      case Filter::Up:
        pred = b;
        break;
      case Filter::Average:
        pred = static_cast<std::uint8_t>((unsigned{a} + unsigned{b}) / 2U);
        break;
      case Filter::Paeth:
        pred = paeth_predictor(a, b, c);
        break;
    }
    // 差は 8bit の剰余で取る（PNG 仕様 §9.2）。
    out[i] = static_cast<std::uint8_t>(int{raw[i]} - int{pred});
  }
}

// PNG 仕様 §12.8 のヒューリスティック: 符号つきバイトとみなした絶対値の和。
std::uint64_t filter_cost(std::span<const std::uint8_t> filtered) {
  std::uint64_t sum = 0;
  for (const std::uint8_t v : filtered) {
    sum += static_cast<std::uint64_t>(std::abs(int{static_cast<std::int8_t>(v)}));
  }
  return sum;
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

// 長さ + 型 + データ + CRC を 1 チャンクとして追記する。CRC は「型 + データ」に掛ける。
void write_chunk(std::vector<std::uint8_t>& out, std::string_view type,
                 std::span<const std::uint8_t> data) {
  write_u32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t crc_start = out.size();
  for (const char c : type) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
  out.insert(out.end(), data.begin(), data.end());
  write_u32(out, crc32(std::span<const std::uint8_t>(out).subspan(crc_start)));
}

std::array<std::uint8_t, 13> make_ihdr(std::uint32_t width, std::uint32_t height) {
  std::array<std::uint8_t, 13> ihdr{};
  const auto put = [&ihdr](std::size_t at, std::uint32_t v) {
    ihdr[at] = static_cast<std::uint8_t>((v >> 24U) & 0xFFU);
    ihdr[at + 1] = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
    ihdr[at + 2] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
    ihdr[at + 3] = static_cast<std::uint8_t>(v & 0xFFU);
  };
  put(0, width);
  put(4, height);
  ihdr[8] = kBitDepth;
  ihdr[9] = kColorTypeRgba;
  ihdr[10] = 0;  // compression method: deflate
  ihdr[11] = 0;  // filter method: 適応フィルタ
  ihdr[12] = 0;  // interlace method: なし
  return ihdr;
}

// 全行にフィルタを掛け、「フィルタバイト + フィルタ済み行」を並べた IDAT の展開前データを作る。
std::vector<std::uint8_t> filter_image(const Bitmap& bitmap, std::size_t stride) {
  std::vector<std::uint8_t> raw((stride + 1) * bitmap.height);
  const std::vector<std::uint8_t> zero_row(stride, 0);

  std::array<std::vector<std::uint8_t>, kFilterCount> candidates;
  for (std::vector<std::uint8_t>& candidate : candidates) {
    candidate.resize(stride);
  }

  const std::span<const std::uint8_t> pixels(bitmap.rgba);
  std::size_t out_pos = 0;
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    const std::span<const std::uint8_t> row = pixels.subspan(std::size_t{y} * stride, stride);
    const std::span<const std::uint8_t> prior =
        y == 0 ? std::span<const std::uint8_t>(zero_row)
               : pixels.subspan((std::size_t{y} - 1) * stride, stride);

    std::size_t best = 0;
    std::uint64_t best_cost = 0;
    for (std::size_t f = 0; f < kFilterCount; ++f) {
      apply_filter(static_cast<Filter>(f), row, prior, candidates[f]);
      const std::uint64_t cost = filter_cost(candidates[f]);
      // 同点なら番号の小さいフィルタを選ぶ（仕様は同点の扱いを決めていないので自分で固定する）。
      if (f == 0 || cost < best_cost) {
        best_cost = cost;
        best = f;
      }
    }

    raw[out_pos] = static_cast<std::uint8_t>(best);
    ++out_pos;
    std::ranges::copy(candidates[best], raw.begin() + static_cast<std::ptrdiff_t>(out_pos));
    out_pos += stride;
  }
  return raw;
}

}  // namespace

Result<std::vector<std::uint8_t>> encode(const Bitmap& bitmap) {
  if (bitmap.width == 0 || bitmap.height == 0) {
    return fail(ErrorKind::InvalidOption,
                std::format("cannot encode a PNG with zero {}: the bitmap is {}x{}",
                            bitmap.width == 0 ? "width" : "height", bitmap.width, bitmap.height));
  }
  const std::uint64_t needed =
      std::uint64_t{bitmap.width} * bitmap.height * std::uint64_t{kBytesPerPixel};
  if (bitmap.rgba.size() != needed) {
    return fail(ErrorKind::InvalidOption,
                std::format("a {}x{} bitmap needs {} RGBA byte(s) but rgba holds {}", bitmap.width,
                            bitmap.height, needed, bitmap.rgba.size()));
  }

  const std::size_t stride = static_cast<std::size_t>(bitmap.width) * kBytesPerPixel;
  const std::vector<std::uint8_t> raw = filter_image(bitmap, stride);

  // zlib の設定は固定する（レベル・ストラテジ・windowBits・memLevel を変えると
  // 出力バイト列が変わり、ゴールデンテストが崩れる）。compress2 は deflateInit 相当なので
  // ストラテジ / windowBits / memLevel はすべて既定値になる。
  uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> compressed(compressed_size);
  const int rc = compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressed_size,
                           reinterpret_cast<const Bytef*>(raw.data()),
                           static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION);
  if (rc != Z_OK) {
    return fail(ErrorKind::Internal,
                std::format("zlib compress2 failed with code {} while encoding a {}x{} PNG", rc,
                            bitmap.width, bitmap.height));
  }
  compressed.resize(static_cast<std::size_t>(compressed_size));

  if (compressed.size() > kMaxChunkLength) {
    return fail(ErrorKind::InvalidOption,
                std::format("the {}x{} bitmap is too large to encode: IDAT would be {} bytes, "
                            "above the PNG chunk limit of {}",
                            bitmap.width, bitmap.height, compressed.size(), kMaxChunkLength));
  }

  std::vector<std::uint8_t> out;
  out.reserve(kSignature.size() + 25 + 12 + compressed.size() + 12);
  out.insert(out.end(), kSignature.begin(), kSignature.end());
  const std::array<std::uint8_t, 13> ihdr = make_ihdr(bitmap.width, bitmap.height);
  write_chunk(out, "IHDR", ihdr);
  write_chunk(out, "IDAT", compressed);
  write_chunk(out, "IEND", {});
  return out;
}

}  // namespace shashoku::png
