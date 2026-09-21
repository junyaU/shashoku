// z_stream::next_in を const ポインタにする（const_cast を避けるため zlib.h より前に置く）。
#define ZLIB_CONST
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/crc32.hpp"
#include "png/png.hpp"
#include "shashoku/error.hpp"

namespace shashoku::png {
namespace {

constexpr std::uint32_t kMaxChunkLength = 0x7FFFFFFFU;
constexpr std::size_t kChunkHeaderSize = 8;  // 長さ 4 + 型 4
constexpr std::size_t kIhdrSize = 13;
constexpr std::size_t kMaxPaletteEntries = 256;

std::unexpected<Error> bad_png(std::string message) {
  return fail(ErrorKind::ImageDecode, std::move(message));
}

// チャンク型を 4 バイトのビッグエンディアンとして 1 語に詰める（比較用）。
constexpr std::uint32_t fourcc(std::string_view s) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) << 24U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 8U) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[3]));
}

constexpr std::uint32_t kIhdr = fourcc("IHDR");
constexpr std::uint32_t kPlte = fourcc("PLTE");
constexpr std::uint32_t kIdat = fourcc("IDAT");
constexpr std::uint32_t kIend = fourcc("IEND");
constexpr std::uint32_t kTrns = fourcc("tRNS");

// エラーメッセージ用。印字できないバイトは \xNN で見せる（壊れた入力が来るので）。
std::string type_name(std::uint32_t type) {
  std::string out;
  for (const unsigned shift : {24U, 16U, 8U, 0U}) {
    const auto b = static_cast<std::uint8_t>((type >> shift) & 0xFFU);
    if (b >= 0x20 && b < 0x7F) {
      out.push_back(static_cast<char>(b));
    } else {
      out += std::format("\\x{:02X}", b);
    }
  }
  return out;
}

bool is_letter(std::uint8_t b) { return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z'); }

// 型の最初の文字が大文字 = critical chunk（PNG 仕様 §5.4）。知らない critical は読み飛ばせない。
bool is_critical(std::uint32_t type) { return ((type >> 24U) & 0x20U) == 0; }

enum class ColorType : std::uint8_t { Gray = 0, Rgb = 2, Palette = 3, GrayAlpha = 4, Rgba = 6 };

std::size_t channel_count(ColorType type) {
  switch (type) {
    case ColorType::Gray:
    case ColorType::Palette:
      return 1;
    case ColorType::GrayAlpha:
      return 2;
    case ColorType::Rgb:
      return 3;
    case ColorType::Rgba:
      return 4;
  }
  return 0;
}

// PNG 仕様 §11.2.2 の表（色型ごとに許される bit depth）。
bool is_valid_depth(ColorType type, std::uint8_t depth) {
  switch (type) {
    case ColorType::Gray:
      return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case ColorType::Palette:
      return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case ColorType::Rgb:
    case ColorType::GrayAlpha:
    case ColorType::Rgba:
      return depth == 8 || depth == 16;
  }
  return false;
}

struct Header {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint8_t bit_depth = 0;
  ColorType color_type = ColorType::Gray;
};

// 位置つきの境界チェック付きリーダ。span の外に出ないことをここに集約する。
class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::size_t position() const { return pos_; }
  [[nodiscard]] std::size_t remaining() const { return bytes_.size() - pos_; }

  [[nodiscard]] std::optional<std::span<const std::uint8_t>> take(std::size_t n) {
    if (n > remaining()) {
      return std::nullopt;
    }
    const std::span<const std::uint8_t> out = bytes_.subspan(pos_, n);
    pos_ += n;
    return out;
  }

  [[nodiscard]] std::optional<std::uint32_t> take_u32() {
    const std::optional<std::span<const std::uint8_t>> b = take(4);
    if (!b) {
      return std::nullopt;
    }
    return (static_cast<std::uint32_t>((*b)[0]) << 24U) |
           (static_cast<std::uint32_t>((*b)[1]) << 16U) |
           (static_cast<std::uint32_t>((*b)[2]) << 8U) | static_cast<std::uint32_t>((*b)[3]);
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t pos_ = 0;
};

std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t at) {
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(bytes[at]) << 8U) |
                                    static_cast<std::uint32_t>(bytes[at + 1]));
}

// チャンクを読み終えた時点の状態。IDAT は連結して持つ。
struct Image {
  Header header;
  std::vector<std::uint8_t> palette;  // RGBA 4 バイト/エントリ（tRNS 反映済み）
  std::optional<std::array<std::uint16_t, 3>> transparent_sample;  // color type 0 / 2 の tRNS
  std::vector<std::uint8_t> idat;
};

Result<Header> parse_ihdr(std::span<const std::uint8_t> data, std::uint64_t max_pixels) {
  if (data.size() != kIhdrSize) {
    return bad_png(std::format("IHDR must be {} bytes but is {}", kIhdrSize, data.size()));
  }
  Header header;
  header.width = (static_cast<std::uint32_t>(data[0]) << 24U) |
                 (static_cast<std::uint32_t>(data[1]) << 16U) |
                 (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
  header.height = (static_cast<std::uint32_t>(data[4]) << 24U) |
                  (static_cast<std::uint32_t>(data[5]) << 16U) |
                  (static_cast<std::uint32_t>(data[6]) << 8U) | static_cast<std::uint32_t>(data[7]);
  header.bit_depth = data[8];
  const std::uint8_t color_type = data[9];
  const std::uint8_t compression = data[10];
  const std::uint8_t filter_method = data[11];
  const std::uint8_t interlace = data[12];

  if (header.width == 0 || header.height == 0) {
    return bad_png(
        std::format("IHDR declares a zero-sized image: {}x{}", header.width, header.height));
  }
  if (header.width > kMaxChunkLength || header.height > kMaxChunkLength) {
    return bad_png(std::format("IHDR dimensions must be at most {} but are {}x{}", kMaxChunkLength,
                               header.width, header.height));
  }
  // 画素を 1 バイトも確保する前に判定する（A25 の検査点 (c)）。
  const std::uint64_t pixels = std::uint64_t{header.width} * header.height;
  if (pixels > max_pixels) {
    return fail(ErrorKind::LimitExceeded,
                std::format("image is too large: {}x{} = {} pixels, the limit is {}", header.width,
                            header.height, pixels, max_pixels));
  }
  if (color_type != 0 && color_type != 2 && color_type != 3 && color_type != 4 && color_type != 6) {
    return bad_png(std::format("IHDR has an invalid color type {}", color_type));
  }
  header.color_type = static_cast<ColorType>(color_type);
  if (!is_valid_depth(header.color_type, header.bit_depth)) {
    return bad_png(std::format("IHDR has bit depth {}, which is invalid for color type {}",
                               header.bit_depth, color_type));
  }
  if (header.bit_depth < 8) {
    return bad_png(
        std::format("bit depth {} is not supported (only 8 and 16 are)", header.bit_depth));
  }
  if (compression != 0) {
    return bad_png(
        std::format("IHDR has compression method {}, only 0 (deflate) is defined", compression));
  }
  if (filter_method != 0) {
    return bad_png(std::format("IHDR has filter method {}, only 0 is defined", filter_method));
  }
  if (interlace == 1) {
    return bad_png("interlaced (Adam7) PNG is not supported");
  }
  if (interlace != 0) {
    return bad_png(
        std::format("IHDR has interlace method {}, only 0 and 1 are defined", interlace));
  }
  return header;
}

Result<void> parse_plte(Image& image, std::span<const std::uint8_t> data) {
  if (image.header.color_type == ColorType::Gray ||
      image.header.color_type == ColorType::GrayAlpha) {
    return bad_png(std::format("PLTE is not allowed for color type {}",
                               static_cast<int>(image.header.color_type)));
  }
  if (data.size() % 3 != 0) {
    return bad_png(std::format("PLTE length {} is not a multiple of 3", data.size()));
  }
  const std::size_t entries = data.size() / 3;
  if (entries == 0 || entries > kMaxPaletteEntries) {
    return bad_png(
        std::format("PLTE has {} entries, must be between 1 and {}", entries, kMaxPaletteEntries));
  }
  image.palette.assign(entries * 4, 255);
  for (std::size_t i = 0; i < entries; ++i) {
    image.palette[i * 4] = data[i * 3];
    image.palette[(i * 4) + 1] = data[(i * 3) + 1];
    image.palette[(i * 4) + 2] = data[(i * 3) + 2];
  }
  return {};
}

Result<void> parse_trns(Image& image, std::span<const std::uint8_t> data) {
  switch (image.header.color_type) {
    case ColorType::Palette: {
      if (image.palette.empty()) {
        return bad_png("tRNS appears before PLTE in a palette image");
      }
      const std::size_t entries = image.palette.size() / 4;
      if (data.size() > entries) {
        return bad_png(
            std::format("tRNS has {} entries but the palette has only {}", data.size(), entries));
      }
      for (std::size_t i = 0; i < data.size(); ++i) {
        image.palette[(i * 4) + 3] = data[i];
      }
      return {};
    }
    case ColorType::Gray: {
      if (data.size() != 2) {
        return bad_png(std::format("tRNS must be 2 bytes for grayscale but is {}", data.size()));
      }
      image.transparent_sample = std::array<std::uint16_t, 3>{read_be16(data, 0), 0, 0};
      return {};
    }
    case ColorType::Rgb: {
      if (data.size() != 6) {
        return bad_png(std::format("tRNS must be 6 bytes for truecolor but is {}", data.size()));
      }
      image.transparent_sample =
          std::array<std::uint16_t, 3>{read_be16(data, 0), read_be16(data, 2), read_be16(data, 4)};
      return {};
    }
    case ColorType::GrayAlpha:
    case ColorType::Rgba:
      return bad_png(
          std::format("tRNS is not allowed for color type {} (it already has an "
                      "alpha channel)",
                      static_cast<int>(image.header.color_type)));
  }
  return bad_png("tRNS with an unknown color type");
}

// チャンクの順序・重複を見張る状態。
struct ChunkOrder {
  bool seen_ihdr = false;
  bool seen_plte = false;
  bool seen_trns = false;
  bool idat_started = false;
  bool idat_done = false;  // IDAT の後に別のチャンクが来た
};

// 長さと CRC を検証したあとの 1 チャンク。
struct RawChunk {
  std::size_t at = 0;  // チャンクの先頭（長さフィールド）のバイト位置
  std::uint32_t type = 0;
  std::span<const std::uint8_t> data;
};

// 1 チャンクぶん読み進める。境界と CRC をここで全部見る。
Result<RawChunk> read_chunk(Reader& reader, std::span<const std::uint8_t> bytes) {
  const std::size_t at = reader.position();
  if (reader.remaining() == 0) {
    return bad_png("the PNG ends without an IEND chunk");
  }
  const std::optional<std::uint32_t> length = reader.take_u32();
  if (!length) {
    return bad_png(
        std::format("truncated PNG: a chunk header at byte {} needs {} bytes but only {} remain",
                    at, kChunkHeaderSize, bytes.size() - at));
  }
  const std::optional<std::uint32_t> type = reader.take_u32();
  if (!type) {
    return bad_png(
        std::format("truncated PNG: a chunk header at byte {} needs {} bytes but only {} remain",
                    at, kChunkHeaderSize, bytes.size() - at));
  }
  if (*length > kMaxChunkLength) {
    return bad_png(std::format("chunk '{}' at byte {} declares length {}, above the limit of {}",
                               type_name(*type), at, *length, kMaxChunkLength));
  }
  for (const unsigned shift : {24U, 16U, 8U, 0U}) {
    if (!is_letter(static_cast<std::uint8_t>((*type >> shift) & 0xFFU))) {
      return bad_png(std::format("chunk type '{}' at byte {} is not four ASCII letters",
                                 type_name(*type), at));
    }
  }

  const std::optional<std::span<const std::uint8_t>> data = reader.take(*length);
  if (!data) {
    return bad_png(
        std::format("truncated PNG: chunk '{}' at byte {} needs {} byte(s) of data plus a "
                    "4-byte CRC but only {} remain",
                    type_name(*type), at, *length, bytes.size() - at - kChunkHeaderSize));
  }
  const std::optional<std::uint32_t> stored_crc = reader.take_u32();
  if (!stored_crc) {
    return bad_png(
        std::format("truncated PNG: chunk '{}' at byte {} needs {} byte(s) of data plus a "
                    "4-byte CRC but only {} remain",
                    type_name(*type), at, *length, bytes.size() - at - kChunkHeaderSize));
  }
  // CRC は「型 + データ」に掛かる（長さフィールドは含まない）。
  const std::uint32_t actual_crc = crc32(bytes.subspan(at + 4, std::size_t{*length} + 4));
  if (actual_crc != *stored_crc) {
    return bad_png(
        std::format("CRC mismatch in chunk '{}' at byte {}: stored 0x{:08X}, computed 0x{:08X}",
                    type_name(*type), at, *stored_crc, actual_crc));
  }
  return RawChunk{.at = at, .type = *type, .data = *data};
}

// 先頭チャンク（IHDR でなければならない）。
Result<void> begin_image(Image& image, ChunkOrder& order, const RawChunk& chunk,
                         std::uint64_t max_pixels) {
  if (chunk.type != kIhdr) {
    return bad_png(std::format("the first chunk must be IHDR but is '{}'", type_name(chunk.type)));
  }
  Result<Header> header = parse_ihdr(chunk.data, max_pixels);
  if (!header) {
    return std::unexpected(std::move(header).error());
  }
  image.header = *header;
  order.seen_ihdr = true;
  return {};
}

// PLTE と tRNS。どちらも「1 個だけ」「IDAT より前」という同じ制約を持つ。
Result<void> handle_palette_chunk(Image& image, ChunkOrder& order, const RawChunk& chunk) {
  const bool is_plte = chunk.type == kPlte;
  const std::string_view name = is_plte ? "PLTE" : "tRNS";
  bool& seen = is_plte ? order.seen_plte : order.seen_trns;
  if (seen) {
    return bad_png(std::format("duplicate {} chunk at byte {}", name, chunk.at));
  }
  if (order.idat_started) {
    return bad_png(std::format("{} at byte {} appears after IDAT", name, chunk.at));
  }
  seen = true;
  return is_plte ? parse_plte(image, chunk.data) : parse_trns(image, chunk.data);
}

// 1 チャンクを状態に取り込む。IEND に達したら true を返す。
Result<bool> handle_chunk(Image& image, ChunkOrder& order, const RawChunk& chunk,
                          std::uint64_t max_pixels) {
  if (!order.seen_ihdr) {
    Result<void> begun = begin_image(image, order, chunk, max_pixels);
    if (!begun) {
      return std::unexpected(std::move(begun).error());
    }
    return false;
  }
  if (chunk.type == kIhdr) {
    return bad_png(std::format("duplicate IHDR chunk at byte {}", chunk.at));
  }
  if (chunk.type == kIend) {
    if (!chunk.data.empty()) {
      return bad_png(std::format("IEND must be empty but declares {} byte(s)", chunk.data.size()));
    }
    return true;
  }
  if (chunk.type == kPlte || chunk.type == kTrns) {
    Result<void> handled = handle_palette_chunk(image, order, chunk);
    if (!handled) {
      return std::unexpected(std::move(handled).error());
    }
    return false;
  }
  if (chunk.type == kIdat) {
    if (order.idat_done) {
      return bad_png(
          std::format("IDAT chunk at byte {} is not consecutive with the previous IDAT", chunk.at));
    }
    order.idat_started = true;
    image.idat.insert(image.idat.end(), chunk.data.begin(), chunk.data.end());
    return false;
  }
  // ここに来るのは補助チャンク（gAMA / tEXt / pHYs …）か未知のチャンク。
  if (order.idat_started) {
    order.idat_done = true;
  }
  if (is_critical(chunk.type)) {
    return bad_png(
        std::format("unknown critical chunk '{}' at byte {}", type_name(chunk.type), chunk.at));
  }
  return false;
}

// IEND まで読んだあとの全体の辻褄合わせ。
Result<Image> finish_image(Image image, const ChunkOrder& order, std::size_t trailing) {
  if (trailing != 0) {
    return bad_png(std::format("{} byte(s) follow the IEND chunk", trailing));
  }
  if (!order.idat_started) {
    return bad_png("the PNG has no IDAT chunk");
  }
  if (image.header.color_type == ColorType::Palette && image.palette.empty()) {
    return bad_png("a palette image (color type 3) has no PLTE chunk");
  }
  return image;
}

// IHDR から IEND まで読み、IDAT を連結して返す。
Result<Image> read_chunks(std::span<const std::uint8_t> bytes, std::uint64_t max_pixels) {
  Reader reader(bytes);
  const std::optional<std::span<const std::uint8_t>> signature = reader.take(kSignature.size());
  if (!signature) {
    return bad_png(std::format("input is only {} byte(s); the PNG signature needs {}", bytes.size(),
                               kSignature.size()));
  }
  if (!std::ranges::equal(*signature, kSignature)) {
    return bad_png("not a PNG: the 8-byte signature does not match 89 50 4E 47 0D 0A 1A 0A");
  }

  Image image;
  ChunkOrder order;
  bool seen_iend = false;
  while (!seen_iend) {
    Result<RawChunk> chunk = read_chunk(reader, bytes);
    if (!chunk) {
      return std::unexpected(std::move(chunk).error());
    }
    Result<bool> done = handle_chunk(image, order, *chunk, max_pixels);
    if (!done) {
      return std::unexpected(std::move(done).error());
    }
    seen_iend = *done;
  }
  return finish_image(std::move(image), order, reader.remaining());
}

// IDAT の zlib ストリームを「必要なぶんだけ」展開する。
// 展開後の全体をメモリに持たないので、巨大な寸法を宣言した入力でも消費は行 2 本ぶんで済む。
class Inflater {
 public:
  // stream_ は既定メンバ初期化子で 0 クリア済み（宣言順で initialized_ より前）。
  explicit Inflater(std::span<const std::uint8_t> input)
      : input_(input), initialized_(inflateInit(&stream_) == Z_OK) {}
  Inflater(const Inflater&) = delete;
  Inflater& operator=(const Inflater&) = delete;
  Inflater(Inflater&&) = delete;
  Inflater& operator=(Inflater&&) = delete;
  ~Inflater() {
    if (initialized_) {
      inflateEnd(&stream_);
    }
  }

  [[nodiscard]] bool initialized() const { return initialized_; }
  [[nodiscard]] bool failed() const { return failed_; }
  [[nodiscard]] bool stream_ended() const { return ended_; }
  [[nodiscard]] const std::string& failure() const { return failure_; }
  // zlib ストリームの終端より後ろに残っている IDAT のバイト数。
  [[nodiscard]] std::size_t unread_input() const {
    return input_.size() - consumed_ + stream_.avail_in;
  }

  // dest を埋める。返り値は実際に書けたバイト数（dest.size() 未満ならそこで尽きた）。
  std::size_t fill(std::span<std::uint8_t> dest) {
    std::size_t written = 0;
    while (written < dest.size() && !failed_ && !ended_) {
      if (stream_.avail_in == 0 && consumed_ < input_.size()) {
        const std::size_t n = std::min(input_.size() - consumed_, kMaxStep);
        stream_.next_in = reinterpret_cast<const Bytef*>(input_.data() + consumed_);
        stream_.avail_in = static_cast<uInt>(n);
        consumed_ += n;
      }
      const std::size_t want = std::min(dest.size() - written, kMaxStep);
      stream_.next_out = reinterpret_cast<Bytef*>(dest.data() + written);
      stream_.avail_out = static_cast<uInt>(want);
      const int rc = inflate(&stream_, Z_NO_FLUSH);
      written += want - stream_.avail_out;
      if (rc == Z_STREAM_END) {
        ended_ = true;
        break;
      }
      if (rc == Z_BUF_ERROR) {
        // 出力先は必ず空けてあるので、進めないのは入力が尽きたとき（= 切り詰め）だけ。
        break;
      }
      if (rc != Z_OK) {
        failed_ = true;
        failure_ = std::format("zlib inflate failed with code {}{}", rc,
                               stream_.msg != nullptr ? std::format(" ({})", stream_.msg) : "");
        break;
      }
    }
    return written;
  }

 private:
  static constexpr std::size_t kMaxStep = std::size_t{1} << 30U;  // uInt に収まる刻み

  std::span<const std::uint8_t> input_;
  z_stream stream_{};
  std::size_t consumed_ = 0;
  bool initialized_ = false;
  bool failed_ = false;
  bool ended_ = false;
  std::string failure_;
};

// PNG 仕様 §9.4 の PaethPredictor。
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

// 1 行をその場で復元する（PNG 仕様 §9.2 の逆変換）。
Result<void> unfilter_row(std::uint8_t filter, std::span<std::uint8_t> row,
                          std::span<const std::uint8_t> prior, std::size_t bpp, std::uint32_t y) {
  if (filter > 4) {
    return bad_png(std::format("row {} has filter type {}, only 0-4 are defined", y, filter));
  }
  for (std::size_t i = 0; i < row.size(); ++i) {
    const std::uint8_t a = i >= bpp ? row[i - bpp] : 0;
    const std::uint8_t b = prior[i];
    const std::uint8_t c = i >= bpp ? prior[i - bpp] : 0;
    std::uint8_t pred = 0;
    switch (filter) {
      case 0:
        pred = 0;
        break;
      case 1:
        pred = a;
        break;
      case 2:
        pred = b;
        break;
      case 3:
        pred = static_cast<std::uint8_t>((unsigned{a} + unsigned{b}) / 2U);
        break;
      default:
        pred = paeth_predictor(a, b, c);
        break;
    }
    row[i] = static_cast<std::uint8_t>(int{row[i]} + int{pred});
  }
  return {};
}

// 1 行ぶんのピクセルを RGBA8 に変換して out に書く。
struct RowConverter {
  Header header;
  std::size_t channels = 0;
  const std::vector<std::uint8_t>* palette = nullptr;
  std::optional<std::array<std::uint16_t, 3>> transparent_sample;

  // bit depth 8 / 16 のサンプルを 16bit 幅で読む（8bit は下位 8bit に入る）。
  [[nodiscard]] std::uint16_t sample(std::span<const std::uint8_t> row, std::size_t index) const {
    if (header.bit_depth == 8) {
      return row[index];
    }
    return read_be16(row, index * 2);
  }

  // 16bit は上位 8bit に落とす（ARCHITECTURE.md §3.2）。
  [[nodiscard]] std::uint8_t to8(std::uint16_t v) const {
    return header.bit_depth == 8 ? static_cast<std::uint8_t>(v)
                                 : static_cast<std::uint8_t>(v >> 8U);
  }

  [[nodiscard]] bool is_transparent(std::uint16_t s0, std::uint16_t s1, std::uint16_t s2) const {
    return transparent_sample.has_value() && (*transparent_sample)[0] == s0 &&
           (*transparent_sample)[1] == s1 && (*transparent_sample)[2] == s2;
  }

  [[nodiscard]] Result<void> convert(std::span<const std::uint8_t> row, std::uint32_t y,
                                     std::span<std::uint8_t> out) const {
    for (std::uint32_t x = 0; x < header.width; ++x) {
      const std::size_t base = std::size_t{x} * channels;
      Color c{};
      switch (header.color_type) {
        case ColorType::Gray: {
          const std::uint16_t g = sample(row, base);
          const std::uint8_t v = to8(g);
          c = Color{v, v, v, is_transparent(g, 0, 0) ? std::uint8_t{0} : std::uint8_t{255}};
          break;
        }
        case ColorType::GrayAlpha: {
          const std::uint8_t v = to8(sample(row, base));
          c = Color{v, v, v, to8(sample(row, base + 1))};
          break;
        }
        case ColorType::Rgb: {
          const std::uint16_t r = sample(row, base);
          const std::uint16_t g = sample(row, base + 1);
          const std::uint16_t b = sample(row, base + 2);
          c = Color{to8(r), to8(g), to8(b),
                    is_transparent(r, g, b) ? std::uint8_t{0} : std::uint8_t{255}};
          break;
        }
        case ColorType::Rgba: {
          c = Color{to8(sample(row, base)), to8(sample(row, base + 1)), to8(sample(row, base + 2)),
                    to8(sample(row, base + 3))};
          break;
        }
        case ColorType::Palette: {
          const std::size_t index = row[base];
          const std::size_t entries = palette->size() / 4;
          if (index >= entries) {
            return bad_png(
                std::format("palette index {} at ({}, {}) is out of range; the palette "
                            "has {} entries",
                            index, x, y, entries));
          }
          c = Color{(*palette)[index * 4], (*palette)[(index * 4) + 1], (*palette)[(index * 4) + 2],
                    (*palette)[(index * 4) + 3]};
          break;
        }
      }
      const std::size_t at = std::size_t{x} * 4;
      out[at] = c.r;
      out[at + 1] = c.g;
      out[at + 2] = c.b;
      out[at + 3] = c.a;
    }
    return {};
  }
};

Result<Bitmap> decode_pixels(const Image& image) {
  const Header& header = image.header;
  const std::size_t channels = channel_count(header.color_type);
  const std::size_t bpp = (channels * header.bit_depth) / 8;  // bit depth は 8 か 16
  const std::size_t stride = static_cast<std::size_t>(header.width) * bpp;

  Inflater inflater(image.idat);
  if (!inflater.initialized()) {
    return fail(ErrorKind::Internal, "zlib inflateInit failed (out of memory?)");
  }

  Bitmap bitmap(header.width, header.height);
  std::vector<std::uint8_t> current(stride);
  std::vector<std::uint8_t> previous(stride, 0);
  const RowConverter converter{.header = header,
                               .channels = channels,
                               .palette = &image.palette,
                               .transparent_sample = image.transparent_sample};

  for (std::uint32_t y = 0; y < header.height; ++y) {
    std::array<std::uint8_t, 1> filter_byte{};
    if (inflater.fill(filter_byte) != 1) {
      if (inflater.failed()) {
        return bad_png(std::format("{} while reading row {}", inflater.failure(), y));
      }
      return bad_png(
          std::format("IDAT is truncated: it expands to fewer than {} byte(s), the "
                      "filter byte of row {} is missing",
                      (stride + 1) * std::size_t{header.height}, y));
    }
    const std::size_t got = inflater.fill(current);
    if (got != stride) {
      if (inflater.failed()) {
        return bad_png(std::format("{} while reading row {}", inflater.failure(), y));
      }
      return bad_png(
          std::format("IDAT is truncated: row {} needs {} byte(s) but only {} were "
                      "available",
                      y, stride, got));
    }
    Result<void> unfiltered = unfilter_row(filter_byte[0], current, previous, bpp, y);
    if (!unfiltered) {
      return std::unexpected(std::move(unfiltered).error());
    }
    const std::size_t out_stride = std::size_t{header.width} * 4;
    const std::span<std::uint8_t> out_row =
        std::span<std::uint8_t>(bitmap.rgba).subspan(std::size_t{y} * out_stride, out_stride);
    Result<void> converted = converter.convert(current, y, out_row);
    if (!converted) {
      return std::unexpected(std::move(converted).error());
    }
    std::swap(current, previous);
  }

  // 余分な出力が残っていないこと、ストリームがきちんと終わっていることを確認する。
  std::array<std::uint8_t, 1> extra{};
  const std::size_t leftover = inflater.fill(extra);
  if (inflater.failed()) {
    return bad_png(std::format("{} after the last row", inflater.failure()));
  }
  if (leftover != 0) {
    return bad_png(std::format("IDAT expands to more than the {} byte(s) the image needs",
                               (stride + 1) * std::size_t{header.height}));
  }
  if (!inflater.stream_ended()) {
    return bad_png("the IDAT zlib stream has no end-of-stream marker");
  }
  if (inflater.unread_input() != 0) {
    return bad_png(
        std::format("{} byte(s) follow the end of the IDAT zlib stream", inflater.unread_input()));
  }
  return bitmap;
}

}  // namespace

Result<Bitmap> decode(std::span<const std::uint8_t> bytes, std::uint64_t max_pixels) {
  Result<Image> image = read_chunks(bytes, max_pixels);
  if (!image) {
    return std::unexpected(std::move(image).error());
  }
  return decode_pixels(*image);
}

}  // namespace shashoku::png
