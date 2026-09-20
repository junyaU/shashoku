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
// zlib の圧縮レベルの範囲（Z_NO_COMPRESSION 〜 Z_BEST_COMPRESSION）。
// Z_DEFAULT_COMPRESSION（-1）は「zlib に任せる」を意味する別物なので受け付けない
// （同じ値でも zlib の版によって実際のレベルが変わりうる。決定性のため）。
constexpr int kMinCompressionLevel = 0;
constexpr int kMaxCompressionLevel = 9;

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

// F の予測値（PNG 仕様 §9.2）。a=左, b=上, c=左上。
template <Filter F>
unsigned predictor(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  if constexpr (F == Filter::Sub) {
    return a;
  } else if constexpr (F == Filter::Up) {
    return b;
  } else if constexpr (F == Filter::Average) {
    return (unsigned{a} + unsigned{b}) / 2U;
  } else if constexpr (F == Filter::Paeth) {
    return paeth_predictor(a, b, c);
  } else {
    return 0U;  // None
  }
}

// 1 バイトぶんの残差。差は 8bit の剰余で取る（PNG 仕様 §9.2）。
template <Filter F>
std::uint8_t residual(std::uint8_t x, std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  return static_cast<std::uint8_t>(unsigned{x} - predictor<F>(a, b, c));
}

// 1 行に F を適用して out に書く。raw / prior / out は同じ長さ。
//
// フィルタ種別が**テンプレート引数**なのが速さの肝（A33）。種別を実行時の引数にして
// 1 バイトごとに switch していたときは、繰り返しの中の分岐のせいでベクトル化できず、
// 行走査だけで 12.9 ms かかっていた（1200x630）。種別ごとに関数を分けると 5.3 ms になる。
//
// 先頭 1 画素ぶんは左（a）と左上（c）が無いので 0 とみなす（PNG 仕様 §9.2）。そこだけを
// 別の繰り返しに切り出してあるので、残りの繰り返しには境界の条件分岐が入らない。
template <Filter F>
void filter_row(std::span<const std::uint8_t> raw, std::span<const std::uint8_t> prior,
                std::span<std::uint8_t> out) {
  const std::size_t size = raw.size();
  const std::size_t head = std::min(size, kBytesPerPixel);
  for (std::size_t i = 0; i < head; ++i) {
    out[i] = residual<F>(raw[i], 0, prior[i], 0);
  }
  for (std::size_t i = head; i < size; ++i) {
    out[i] = residual<F>(raw[i], raw[i - kBytesPerPixel], prior[i], prior[i - kBytesPerPixel]);
  }
}

// 種別で呼び分ける。分岐はここ（1 行に 1 回）だけで、行の中には持ち込まない。
void filter_row(Filter filter, std::span<const std::uint8_t> raw,
                std::span<const std::uint8_t> prior, std::span<std::uint8_t> out) {
  switch (filter) {
    case Filter::None:
      filter_row<Filter::None>(raw, prior, out);
      return;
    case Filter::Sub:
      filter_row<Filter::Sub>(raw, prior, out);
      return;
    case Filter::Up:
      filter_row<Filter::Up>(raw, prior, out);
      return;
    case Filter::Average:
      filter_row<Filter::Average>(raw, prior, out);
      return;
    case Filter::Paeth:
      filter_row<Filter::Paeth>(raw, prior, out);
      return;
  }
}

// PNG 仕様 §12.8 のヒューリスティック: 符号つきバイトとみなした絶対値の和。
// 残差を書く繰り返しとは分けてある（融合すると 8bit のまま進めなくなって遅くなる。A33）。
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
  // 候補は 1 行ぶんの作業バッファで使い回す（5 本持っても速くならず、広い絵ではキャッシュに
  // 載らなくなるだけ）。勝ったフィルタだけを、最後にもう一度だけ出力の位置へ適用する。
  std::vector<std::uint8_t> scratch(stride);

  const std::span<const std::uint8_t> pixels(bitmap.rgba);
  const std::span<std::uint8_t> out(raw);
  std::size_t out_pos = 0;
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    const std::span<const std::uint8_t> row = pixels.subspan(std::size_t{y} * stride, stride);
    const std::span<const std::uint8_t> prior =
        y == 0 ? std::span<const std::uint8_t>(zero_row)
               : pixels.subspan((std::size_t{y} - 1) * stride, stride);

    std::size_t best = 0;
    std::uint64_t best_cost = 0;
    for (std::size_t f = 0; f < kFilterCount; ++f) {
      filter_row(static_cast<Filter>(f), row, prior, scratch);
      const std::uint64_t cost = filter_cost(scratch);
      // 同点なら番号の小さいフィルタを選ぶ（仕様は同点の扱いを決めていないので自分で固定する）。
      if (f == 0 || cost < best_cost) {
        best_cost = cost;
        best = f;
      }
    }

    raw[out_pos] = static_cast<std::uint8_t>(best);
    ++out_pos;
    filter_row(static_cast<Filter>(best), row, prior, out.subspan(out_pos, stride));
    out_pos += stride;
  }
  return raw;
}

}  // namespace

Result<std::vector<std::uint8_t>> encode(const Bitmap& bitmap, int compression_level) {
  if (bitmap.width == 0 || bitmap.height == 0) {
    return fail(ErrorKind::InvalidOption,
                std::format("cannot encode a PNG with zero {}: the bitmap is {}x{}",
                            bitmap.width == 0 ? "width" : "height", bitmap.width, bitmap.height));
  }
  if (compression_level < kMinCompressionLevel || compression_level > kMaxCompressionLevel) {
    return fail(ErrorKind::InvalidOption,
                std::format("compression level must be between {} and {} (got {})",
                            kMinCompressionLevel, kMaxCompressionLevel, compression_level));
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

  // レベル以外の zlib の設定は固定する（ストラテジ・windowBits・memLevel を変えると
  // 出力バイト列が変わる）。compress2 は deflateInit 相当なので、それらはすべて既定値になる。
  // レベルは入力の一部（A33）: 同じレベルなら常に同じバイト列が出る。
  uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> compressed(compressed_size);
  const int rc = compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressed_size,
                           reinterpret_cast<const Bytef*>(raw.data()),
                           static_cast<uLong>(raw.size()), compression_level);
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
