#include "core/utf8.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/result.hpp"
#include "shashoku/error.hpp"

namespace shashoku {
namespace {

constexpr char32_t kMaxCodePoint = 0x10FFFF;
constexpr char32_t kSurrogateFirst = 0xD800;
constexpr char32_t kSurrogateLast = 0xDFFF;
constexpr char32_t kReplacementChar = 0xFFFD;

// 系列長ごとの最小コードポイント（冗長表現の検出用）。添字は 1..4。
constexpr std::array<char32_t, 5> kMinForLength{0, 0, 0x80, 0x800, 0x10000};

bool is_continuation(unsigned char b) { return (b & 0xC0) == 0x80; }

bool is_surrogate(char32_t cp) { return cp >= kSurrogateFirst && cp <= kSurrogateLast; }

// 1 コードポイントの復号結果（cp と、それが占めるバイト数）。
struct Decoded {
  char32_t cp = 0;
  std::size_t length = 0;
};

// 先頭バイトから系列長と上位ビットを取り出す。UTF-8 の先頭バイトでなければ nullopt。
std::optional<Decoded> decode_lead(unsigned char b) {
  if (b < 0x80) {
    return Decoded{b, 1};
  }
  if ((b & 0xE0) == 0xC0) {
    return Decoded{static_cast<char32_t>(b & 0x1F), 2};
  }
  if ((b & 0xF0) == 0xE0) {
    return Decoded{static_cast<char32_t>(b & 0x0F), 3};
  }
  if ((b & 0xF8) == 0xF0) {
    return Decoded{static_cast<char32_t>(b & 0x07), 4};
  }
  return std::nullopt;
}

// message にはバイトオフセットを必ず入れる。location は行・桁での報告用（error.hpp）。
std::unexpected<Error> invalid_utf8(std::string_view bytes, std::size_t offset,
                                    const std::string& reason) {
  return fail(ErrorKind::InvalidUtf8, std::format("invalid UTF-8 at byte {}: {}", offset, reason),
              locate(bytes, offset));
}

Result<Decoded> decode_one(std::string_view bytes, std::size_t at) {
  const auto lead = static_cast<unsigned char>(bytes[at]);
  const std::optional<Decoded> start = decode_lead(lead);
  if (!start) {
    if (is_continuation(lead)) {
      return invalid_utf8(bytes, at, std::format("unexpected continuation byte 0x{:02X}", lead));
    }
    return invalid_utf8(bytes, at, std::format("invalid lead byte 0x{:02X}", lead));
  }

  const std::size_t length = start->length;
  const std::size_t available = bytes.size() - at;
  if (available < length) {
    return invalid_utf8(
        bytes, at,
        std::format("truncated {}-byte sequence ({} byte(s) available)", length, available));
  }

  char32_t cp = start->cp;
  for (std::size_t k = 1; k < length; ++k) {
    const auto b = static_cast<unsigned char>(bytes[at + k]);
    if (!is_continuation(b)) {
      return invalid_utf8(
          bytes, at,
          std::format("expected a continuation byte at offset {}, got 0x{:02X}", at + k, b));
    }
    cp = (cp << 6) | static_cast<char32_t>(b & 0x3F);
  }

  if (cp < kMinForLength[length]) {
    return invalid_utf8(bytes, at,
                        std::format("overlong {}-byte encoding of U+{:04X}", length,
                                    static_cast<std::uint32_t>(cp)));
  }
  if (is_surrogate(cp)) {
    return invalid_utf8(
        bytes, at, std::format("surrogate code point U+{:04X}", static_cast<std::uint32_t>(cp)));
  }
  if (cp > kMaxCodePoint) {
    return invalid_utf8(
        bytes, at,
        std::format("code point U+{:04X} is above U+10FFFF", static_cast<std::uint32_t>(cp)));
  }
  return Decoded{cp, length};
}

}  // namespace

Result<std::u32string> decode_utf8(std::string_view bytes) {
  std::u32string out;
  out.reserve(bytes.size());
  std::size_t i = 0;
  while (i < bytes.size()) {
    Result<Decoded> decoded = decode_one(bytes, i);
    if (!decoded) {
      return std::unexpected(std::move(decoded).error());
    }
    out.push_back(decoded->cp);
    i += decoded->length;
  }
  return out;
}

void append_utf8(std::string& out, char32_t cp) {
  // スカラー値でないものを渡すのは呼び出し側のバグ。ここでは落とさず U+FFFD を書く。
  const char32_t scalar = (cp > kMaxCodePoint || is_surrogate(cp)) ? kReplacementChar : cp;
  if (scalar < 0x80) {
    out.push_back(static_cast<char>(scalar));
    return;
  }
  if (scalar < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (scalar >> 6)));
    out.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
    return;
  }
  if (scalar < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (scalar >> 12)));
    out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
    return;
  }
  out.push_back(static_cast<char>(0xF0 | (scalar >> 18)));
  out.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3F)));
  out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3F)));
  out.push_back(static_cast<char>(0x80 | (scalar & 0x3F)));
}

std::string encode_utf8(std::u32string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char32_t cp : text) {
    append_utf8(out, cp);
  }
  return out;
}

SourceLocation locate(std::string_view source, std::size_t byte_offset) {
  // 範囲外のオフセットは末尾に丸める（呼び出し側のバグだが、位置報告で落ちない方がよい）。
  const std::size_t limit = std::min(byte_offset, source.size());

  std::uint32_t line = 1;
  std::uint32_t column = 1;
  for (std::size_t i = 0; i < limit; ++i) {
    const auto b = static_cast<unsigned char>(source[i]);
    if (b == '\n') {
      ++line;
      column = 1;
    } else if (b == '\r') {
      ++line;
      column = 1;
      // CRLF は 1 つの改行として数える（LF をこの場で読み飛ばす）
      if (i + 1 < limit && source[i + 1] == '\n') {
        ++i;
      }
    } else if (!is_continuation(b)) {
      // 継続バイトは桁に数えない（桁はコードポイント単位）
      ++column;
    }
  }

  return SourceLocation{.offset = static_cast<std::uint32_t>(std::min<std::size_t>(
                            limit, std::numeric_limits<std::uint32_t>::max())),
                        .line = line,
                        .column = column};
}

}  // namespace shashoku
