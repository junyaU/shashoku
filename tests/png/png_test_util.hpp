#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/bitmap.hpp"
#include "core/color.hpp"

// テストから PNG のバイト列を組み立てたり分解したりするための道具。
// デコーダのテストは「エンコーダが作れない PNG」（gray / palette / 16bit / 壊れた入力）を
// 相手にするので、ここで独立に組み立てる。圧縮には zlib をそのまま使う。

namespace shashoku::png::test {

std::vector<std::uint8_t> be32(std::uint32_t v);

// 長さ + 型 + データ + CRC を追記する（CRC は zlib の crc32 で計算する。
// エンコーダの自前実装との突き合わせになる）。
void append_chunk(std::vector<std::uint8_t>& out, std::string_view type,
                  std::span<const std::uint8_t> data);

std::vector<std::uint8_t> zlib_compress(std::span<const std::uint8_t> data);

using ExtraChunk = std::pair<std::string, std::vector<std::uint8_t>>;

// 組み立てたい PNG の指定。raw はフィルタバイトを含む「展開後の IDAT の中身」。
struct ImageSpec {
  std::uint32_t width = 1;
  std::uint32_t height = 1;
  std::uint8_t bit_depth = 8;
  std::uint8_t color_type = 6;
  std::uint8_t compression = 0;
  std::uint8_t filter_method = 0;
  std::uint8_t interlace = 0;
  std::vector<std::uint8_t> palette;  // PLTE の中身（RGB 3 バイト/エントリ）。空なら出さない
  bool has_trns = false;
  std::vector<std::uint8_t> trns;
  std::vector<std::uint8_t> raw;  // 圧縮前の IDAT
  bool raw_is_compressed = false;  // true なら raw をそのまま IDAT に入れる（壊れた zlib 用）
  std::vector<ExtraChunk> before_idat;
  std::vector<ExtraChunk> between_idat;  // IDAT の合間に挟むチャンク（順序違反のテスト用）
  std::vector<ExtraChunk> after_idat;
  std::size_t idat_pieces = 1;  // IDAT をいくつに割るか
  bool write_iend = true;
};

std::vector<std::uint8_t> build_png(const ImageSpec& spec);

// 各行に「フィルタ 0（None）」のバイトを足して IDAT の中身にする。
std::vector<std::uint8_t> with_none_filter(std::span<const std::uint8_t> pixels, std::size_t stride,
                                           std::uint32_t height);

struct ChunkView {
  std::string type;
  std::size_t offset = 0;  // チャンクの先頭（長さフィールド）のバイト位置
  std::vector<std::uint8_t> data;
  std::uint32_t stored_crc = 0;
};

// PNG のチャンクを順に取り出す（壊れていない入力だけを想定）。
std::vector<ChunkView> list_chunks(std::span<const std::uint8_t> png);

// エンコード済み PNG の IDAT を展開して、各行のフィルタ種別を取り出す。
std::vector<std::uint8_t> extract_filters(std::span<const std::uint8_t> png);

// 決定的な擬似乱数（テストに再現性を持たせるため。std::mt19937 でもよいが種を固定する）。
class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed | 1U) {}
  std::uint32_t next();
  std::uint8_t byte() { return static_cast<std::uint8_t>(next() >> 24U); }
  std::uint32_t below(std::uint32_t bound) { return next() % bound; }

 private:
  std::uint64_t state_;
};

Bitmap make_solid(std::uint32_t w, std::uint32_t h, Color color);
Bitmap make_gradient(std::uint32_t w, std::uint32_t h);
Bitmap make_noise(std::uint32_t w, std::uint32_t h, std::uint64_t seed);

}  // namespace shashoku::png::test
