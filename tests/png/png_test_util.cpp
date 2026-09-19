#include "png/png_test_util.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "png/png.hpp"

namespace shashoku::png::test {
namespace {

std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
         (static_cast<std::uint32_t>(bytes[at + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[at + 2]) << 8U) |
         static_cast<std::uint32_t>(bytes[at + 3]);
}

}  // namespace

std::vector<std::uint8_t> be32(std::uint32_t v) {
  return {static_cast<std::uint8_t>((v >> 24U) & 0xFFU),
          static_cast<std::uint8_t>((v >> 16U) & 0xFFU),
          static_cast<std::uint8_t>((v >> 8U) & 0xFFU), static_cast<std::uint8_t>(v & 0xFFU)};
}

void append_chunk(std::vector<std::uint8_t>& out, std::string_view type,
                  std::span<const std::uint8_t> data) {
  const std::vector<std::uint8_t> length = be32(static_cast<std::uint32_t>(data.size()));
  out.insert(out.end(), length.begin(), length.end());

  std::vector<std::uint8_t> body;
  body.reserve(4 + data.size());
  for (const char c : type) {
    body.push_back(static_cast<std::uint8_t>(c));
  }
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());

  // 期待値は zlib の crc32（別実装）で作る。自前の crc32 と突き合わせる意味がある。
  const uLong crc = ::crc32(::crc32(0, nullptr, 0), reinterpret_cast<const Bytef*>(body.data()),
                            static_cast<uInt>(body.size()));
  const std::vector<std::uint8_t> crc_bytes = be32(static_cast<std::uint32_t>(crc));
  out.insert(out.end(), crc_bytes.begin(), crc_bytes.end());
}

std::vector<std::uint8_t> zlib_compress(std::span<const std::uint8_t> data) {
  uLongf size = compressBound(static_cast<uLong>(data.size()));
  std::vector<std::uint8_t> out(size == 0 ? 1 : size);
  const int rc = compress2(reinterpret_cast<Bytef*>(out.data()), &size,
                           reinterpret_cast<const Bytef*>(data.data()),
                           static_cast<uLong>(data.size()), Z_BEST_COMPRESSION);
  if (rc != Z_OK) {
    return {};
  }
  out.resize(static_cast<std::size_t>(size));
  return out;
}

std::vector<std::uint8_t> build_png(const ImageSpec& spec) {
  std::vector<std::uint8_t> out(kSignature.begin(), kSignature.end());

  std::vector<std::uint8_t> ihdr;
  const std::vector<std::uint8_t> w = be32(spec.width);
  const std::vector<std::uint8_t> h = be32(spec.height);
  ihdr.insert(ihdr.end(), w.begin(), w.end());
  ihdr.insert(ihdr.end(), h.begin(), h.end());
  ihdr.push_back(spec.bit_depth);
  ihdr.push_back(spec.color_type);
  ihdr.push_back(spec.compression);
  ihdr.push_back(spec.filter_method);
  ihdr.push_back(spec.interlace);
  append_chunk(out, "IHDR", ihdr);

  if (!spec.palette.empty()) {
    append_chunk(out, "PLTE", spec.palette);
  }
  if (spec.has_trns) {
    append_chunk(out, "tRNS", spec.trns);
  }
  for (const ExtraChunk& extra : spec.before_idat) {
    append_chunk(out, extra.first, extra.second);
  }

  const std::vector<std::uint8_t> compressed =
      spec.raw_is_compressed ? spec.raw : zlib_compress(spec.raw);
  const std::size_t pieces = std::max<std::size_t>(spec.idat_pieces, 1);
  std::size_t at = 0;
  for (std::size_t i = 0; i < pieces; ++i) {
    if (i != 0) {
      for (const ExtraChunk& extra : spec.between_idat) {
        append_chunk(out, extra.first, extra.second);
      }
    }
    const std::size_t remaining = compressed.size() - at;
    const std::size_t take = i + 1 == pieces ? remaining : remaining / (pieces - i);
    append_chunk(out, "IDAT", std::span<const std::uint8_t>(compressed).subspan(at, take));
    at += take;
  }

  for (const ExtraChunk& extra : spec.after_idat) {
    append_chunk(out, extra.first, extra.second);
  }
  if (spec.write_iend) {
    append_chunk(out, "IEND", {});
  }
  return out;
}

std::vector<std::uint8_t> with_none_filter(std::span<const std::uint8_t> pixels, std::size_t stride,
                                           std::uint32_t height) {
  std::vector<std::uint8_t> raw;
  raw.reserve((stride + 1) * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const std::span<const std::uint8_t> row = pixels.subspan(std::size_t{y} * stride, stride);
    raw.insert(raw.end(), row.begin(), row.end());
  }
  return raw;
}

std::vector<ChunkView> list_chunks(std::span<const std::uint8_t> png) {
  std::vector<ChunkView> chunks;
  std::size_t at = kSignature.size();
  while (at + 8 <= png.size()) {
    ChunkView chunk;
    chunk.offset = at;
    const std::uint32_t length = read_be32(png, at);
    for (std::size_t i = 0; i < 4; ++i) {
      chunk.type.push_back(static_cast<char>(png[at + 4 + i]));
    }
    if (at + 12 + length > png.size()) {
      break;
    }
    const std::span<const std::uint8_t> data = png.subspan(at + 8, length);
    chunk.data.assign(data.begin(), data.end());
    chunk.stored_crc = read_be32(png, at + 8 + length);
    chunks.push_back(std::move(chunk));
    at += 12 + std::size_t{length};
  }
  return chunks;
}

std::vector<std::uint8_t> extract_filters(std::span<const std::uint8_t> png) {
  const std::vector<ChunkView> chunks = list_chunks(png);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> compressed;
  for (const ChunkView& chunk : chunks) {
    if (chunk.type == "IHDR" && chunk.data.size() == 13) {
      width = read_be32(chunk.data, 0);
      height = read_be32(chunk.data, 4);
    } else if (chunk.type == "IDAT") {
      compressed.insert(compressed.end(), chunk.data.begin(), chunk.data.end());
    }
  }
  const std::size_t stride = std::size_t{width} * 4;  // encode は常に RGBA8
  uLongf raw_size = (stride + 1) * height;
  std::vector<std::uint8_t> raw(raw_size == 0 ? 1 : raw_size);
  if (uncompress(reinterpret_cast<Bytef*>(raw.data()), &raw_size,
                 reinterpret_cast<const Bytef*>(compressed.data()),
                 static_cast<uLong>(compressed.size())) != Z_OK) {
    return {};
  }
  std::vector<std::uint8_t> filters;
  filters.reserve(height);
  for (std::uint32_t y = 0; y < height; ++y) {
    filters.push_back(raw[std::size_t{y} * (stride + 1)]);
  }
  return filters;
}

std::uint32_t Rng::next() {
  // xorshift64*。libm にも <random> の実装差にも依存しない決定的な列。
  state_ ^= state_ >> 12U;
  state_ ^= state_ << 25U;
  state_ ^= state_ >> 27U;
  return static_cast<std::uint32_t>((state_ * 0x2545F4914F6CDD1DULL) >> 32U);
}

Bitmap make_solid(std::uint32_t w, std::uint32_t h, Color color) { return {w, h, color}; }

Bitmap make_gradient(std::uint32_t w, std::uint32_t h) {
  Bitmap bitmap(w, h);
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      bitmap.set_pixel(x, y,
                       Color{static_cast<std::uint8_t>(x * 255 / std::max(w - 1, 1U)),
                             static_cast<std::uint8_t>(y * 255 / std::max(h - 1, 1U)),
                             static_cast<std::uint8_t>((x + y) & 0xFFU), 255});
    }
  }
  return bitmap;
}

Bitmap make_noise(std::uint32_t w, std::uint32_t h, std::uint64_t seed) {
  Bitmap bitmap(w, h);
  Rng rng(seed);
  for (std::uint8_t& v : bitmap.rgba) {
    v = rng.byte();
  }
  return bitmap;
}

}  // namespace shashoku::png::test
