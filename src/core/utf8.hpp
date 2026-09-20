#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "core/result.hpp"

namespace shashoku {

// UTF-8 → UTF-32。不正なバイト列（不完全なシーケンス、冗長表現、サロゲート、
// U+10FFFF 超）は ErrorKind::InvalidUtf8 で、バイトオフセットを message に含めて返す。
// 置換文字 U+FFFD で黙って続行しない（fail loudly）。
Result<std::u32string> decode_utf8(std::string_view bytes);

// 1 コードポイントを UTF-8 で out に追記する。cp はスカラー値であること
// （サロゲートや範囲外を渡すのは呼び出し側のバグ: U+FFFD を書く）。
void append_utf8(std::string& out, char32_t cp);

std::string encode_utf8(std::u32string_view text);

// バイトオフセット → SourceLocation（行・桁はコードポイント単位、1 始まり）。
// 改行は LF / CRLF / CR のいずれも 1 行として数える。
SourceLocation locate(std::string_view source, std::size_t byte_offset);

}  // namespace shashoku
