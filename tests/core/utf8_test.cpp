#include "core/utf8.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/result.hpp"
#include "shashoku/error.hpp"

namespace shashoku {
namespace {

using ::testing::HasSubstr;

// エスケープの解釈で事故らないよう、不正なバイト列はバイト値で組み立てる。
std::string bytes(std::initializer_list<int> values) {
  std::string out;
  out.reserve(values.size());
  for (const int v : values) {
    out.push_back(static_cast<char>(static_cast<unsigned char>(v)));
  }
  return out;
}

// U+0000 のように文字列リテラルで書けないコードポイントがあるので 1 文字の文字列を明示的に作る。
std::u32string one(char32_t cp) {
  std::u32string out;
  out.push_back(cp);
  return out;
}

std::u32string decoded(std::string_view input) {
  Result<std::u32string> result = decode_utf8(input);
  if (!result) {
    ADD_FAILURE() << "decode_utf8 failed: " << to_string(result.error());
    return {};
  }
  return std::move(*result);
}

Error decode_failure(std::string_view input) {
  Result<std::u32string> result = decode_utf8(input);
  if (result) {
    ADD_FAILURE() << "decode_utf8 unexpectedly succeeded";
    return Error{};
  }
  Error error = std::move(result).error();
  EXPECT_EQ(error.kind, ErrorKind::InvalidUtf8);
  return error;
}

// ---- decode_utf8: 正常系 --------------------------------------------------

TEST(Utf8Decode, Empty) { EXPECT_EQ(decoded(""), U""); }

TEST(Utf8Decode, Ascii) { EXPECT_EQ(decoded("hello, world"), U"hello, world"); }

// 1 バイト表現の下限 U+0000 と上限 U+007F
TEST(Utf8Decode, OneByteBoundaries) {
  EXPECT_EQ(decoded(bytes({0x00})), one(U'\0'));
  EXPECT_EQ(decoded(bytes({0x7F})), one(U'\x7F'));
}

// 2 バイト表現の下限 U+0080 と上限 U+07FF
TEST(Utf8Decode, TwoByteBoundaries) {
  EXPECT_EQ(decoded(bytes({0xC2, 0x80})), one(U'\x80'));
  EXPECT_EQ(decoded(bytes({0xDF, 0xBF})), one(U'\x7FF'));
}

// 3 バイト表現の下限 U+0800 と上限 U+FFFF
TEST(Utf8Decode, ThreeByteBoundaries) {
  EXPECT_EQ(decoded(bytes({0xE0, 0xA0, 0x80})), one(U'\x800'));
  EXPECT_EQ(decoded(bytes({0xEF, 0xBF, 0xBF})), one(U'\xFFFF'));
}

// 4 バイト表現の下限 U+10000 と上限 U+10FFFF
TEST(Utf8Decode, FourByteBoundaries) {
  EXPECT_EQ(decoded(bytes({0xF0, 0x90, 0x80, 0x80})), one(U'\x10000'));
  EXPECT_EQ(decoded(bytes({0xF4, 0x8F, 0xBF, 0xBF})), one(U'\x10FFFF'));
}

// サロゲート領域の両隣は正当なスカラー値
TEST(Utf8Decode, SurrogateNeighboursAreValid) {
  EXPECT_EQ(decoded(bytes({0xED, 0x9F, 0xBF})), one(U'\xD7FF'));
  EXPECT_EQ(decoded(bytes({0xEE, 0x80, 0x80})), one(U'\xE000'));
}

TEST(Utf8Decode, Japanese) {
  EXPECT_EQ(decoded("日本語の組版"), U"日本語の組版");
  EXPECT_EQ(decoded("、。「」ー゛"), U"、。「」ー゛");
}

// 結合文字・異体字セレクタ・SMP の文字が混在しても 1 コードポイントずつ取り出せる。
// 結合列はソース上で 1 文字に見えてしまうので、入力はバイト列で書く。
TEST(Utf8Decode, MixedPlanes) {
  const std::u32string text = decoded(bytes({
      0xE3, 0x81, 0x8B,        // か       U+304B
      0xE3, 0x82, 0x99,        // 濁点     U+3099（結合文字）
      0xE8, 0x91, 0x9B,        // 葛       U+845B
      0xF3, 0xA0, 0x84, 0x80,  // VS17     U+E0100（異体字セレクタ）
      0xF0, 0x9F, 0x8D, 0xA3,  // 🍣       U+1F363
  }));
  ASSERT_EQ(text.size(), 5U);
  EXPECT_EQ(text[0], U'\x304B');
  EXPECT_EQ(text[1], U'\x3099');
  EXPECT_EQ(text[2], U'\x845B');
  EXPECT_EQ(text[3], U'\xE0100');
  EXPECT_EQ(text[4], U'\x1F363');
}

TEST(Utf8Decode, CountsCodePointsNotBytes) { EXPECT_EQ(decoded("a あ 🍣").size(), 5U); }

// ---- decode_utf8: 異常系 --------------------------------------------------

TEST(Utf8Decode, RejectsLoneContinuationByte) {
  EXPECT_THAT(decode_failure(bytes({0x80})).message,
              HasSubstr("unexpected continuation byte 0x80"));
  EXPECT_THAT(decode_failure(bytes({0xBF})).message,
              HasSubstr("unexpected continuation byte 0xBF"));
  // 正しい 3 バイト列の後ろにぶら下がった継続バイト
  EXPECT_THAT(decode_failure(bytes({0xE3, 0x81, 0x82, 0x80})).message, HasSubstr("at byte 3"));
}

TEST(Utf8Decode, RejectsInvalidLeadByte) {
  for (const int lead : {0xF8, 0xF9, 0xFC, 0xFE, 0xFF}) {
    EXPECT_THAT(decode_failure(bytes({lead, 0x80, 0x80, 0x80})).message,
                HasSubstr("invalid lead byte"));
  }
}

TEST(Utf8Decode, RejectsTruncatedSequence) {
  EXPECT_THAT(decode_failure(bytes({0xC2})).message, HasSubstr("truncated 2-byte sequence"));
  EXPECT_THAT(decode_failure(bytes({0xE3, 0x81})).message, HasSubstr("truncated 3-byte sequence"));
  EXPECT_THAT(decode_failure(bytes({0xF0, 0x9F, 0x8D})).message,
              HasSubstr("truncated 4-byte sequence"));
}

TEST(Utf8Decode, RejectsBrokenContinuation) {
  // 3 バイト列の途中に ASCII が割り込む
  EXPECT_THAT(decode_failure(bytes({0xE3, 0x81, 0x41})).message,
              HasSubstr("expected a continuation byte at offset 2"));
  EXPECT_THAT(decode_failure(bytes({0xF0, 0x9F, 0xC2, 0x80})).message,
              HasSubstr("expected a continuation byte at offset 2"));
}

TEST(Utf8Decode, RejectsOverlongEncoding) {
  EXPECT_THAT(decode_failure(bytes({0xC0, 0x80})).message, HasSubstr("overlong"));
  EXPECT_THAT(decode_failure(bytes({0xC1, 0xBF})).message, HasSubstr("overlong"));
  EXPECT_THAT(decode_failure(bytes({0xE0, 0x80, 0x80})).message, HasSubstr("overlong"));
  EXPECT_THAT(decode_failure(bytes({0xE0, 0x9F, 0xBF})).message, HasSubstr("overlong"));
  EXPECT_THAT(decode_failure(bytes({0xF0, 0x80, 0x80, 0x80})).message, HasSubstr("overlong"));
  EXPECT_THAT(decode_failure(bytes({0xF0, 0x8F, 0xBF, 0xBF})).message, HasSubstr("overlong"));
}

TEST(Utf8Decode, RejectsSurrogates) {
  EXPECT_THAT(decode_failure(bytes({0xED, 0xA0, 0x80})).message,
              HasSubstr("surrogate code point U+D800"));
  EXPECT_THAT(decode_failure(bytes({0xED, 0xBF, 0xBF})).message,
              HasSubstr("surrogate code point U+DFFF"));
  // CESU-8 / WTF-8 のサロゲートペアも受け付けない
  EXPECT_THAT(decode_failure(bytes({0xED, 0xA0, 0xBD, 0xED, 0xB3, 0xA3})).message,
              HasSubstr("surrogate"));
}

TEST(Utf8Decode, RejectsAboveMaxCodePoint) {
  EXPECT_THAT(decode_failure(bytes({0xF4, 0x90, 0x80, 0x80})).message,
              HasSubstr("U+110000 is above U+10FFFF"));
  EXPECT_THAT(decode_failure(bytes({0xF5, 0x80, 0x80, 0x80})).message,
              HasSubstr("is above U+10FFFF"));
  EXPECT_THAT(decode_failure(bytes({0xF7, 0xBF, 0xBF, 0xBF})).message,
              HasSubstr("is above U+10FFFF"));
}

// message には必ず不正な系列のバイトオフセットが入る
TEST(Utf8Decode, ErrorMessageCarriesByteOffset) {
  EXPECT_THAT(decode_failure(bytes({0xFF})).message, HasSubstr("at byte 0"));
  EXPECT_THAT(decode_failure("abc" + bytes({0xFF})).message, HasSubstr("at byte 3"));
  // 日本語 1 文字（3 バイト）の後ろ
  EXPECT_THAT(decode_failure("あ" + bytes({0xC0, 0x80})).message, HasSubstr("at byte 3"));
}

TEST(Utf8Decode, ErrorCarriesLocation) {
  const Error error = decode_failure("あ\nい" + bytes({0xFF}));
  EXPECT_EQ(error.location,
            std::make_optional(SourceLocation{.offset = 7, .line = 2, .column = 2}));
}

// ---- append_utf8 / encode_utf8 -------------------------------------------

TEST(Utf8Encode, LengthBoundaries) {
  EXPECT_EQ(encode_utf8(one(U'\0')), bytes({0x00}));
  EXPECT_EQ(encode_utf8(one(U'\x7F')), bytes({0x7F}));
  EXPECT_EQ(encode_utf8(one(U'\x80')), bytes({0xC2, 0x80}));
  EXPECT_EQ(encode_utf8(one(U'\x7FF')), bytes({0xDF, 0xBF}));
  EXPECT_EQ(encode_utf8(one(U'\x800')), bytes({0xE0, 0xA0, 0x80}));
  EXPECT_EQ(encode_utf8(one(U'\xD7FF')), bytes({0xED, 0x9F, 0xBF}));
  EXPECT_EQ(encode_utf8(one(U'\xE000')), bytes({0xEE, 0x80, 0x80}));
  EXPECT_EQ(encode_utf8(one(U'\xFFFF')), bytes({0xEF, 0xBF, 0xBF}));
  EXPECT_EQ(encode_utf8(one(U'\x10000')), bytes({0xF0, 0x90, 0x80, 0x80}));
  EXPECT_EQ(encode_utf8(one(U'\x10FFFF')), bytes({0xF4, 0x8F, 0xBF, 0xBF}));
}

TEST(Utf8Encode, Empty) { EXPECT_EQ(encode_utf8(U""), ""); }

TEST(Utf8Encode, RoundTripsJapanese) {
  const std::string source = "日本語の組版、および約物「」の扱い。🍣";
  EXPECT_EQ(encode_utf8(decoded(source)), source);
}

TEST(Utf8Encode, AppendAccumulates) {
  std::string out = "x";
  append_utf8(out, U'あ');
  append_utf8(out, U'A');
  EXPECT_EQ(out, "xあA");
}

// スカラー値でない引数は呼び出し側のバグ。落とさず U+FFFD を書く（ヘッダのコメント）。
TEST(Utf8Encode, AppendWritesReplacementForNonScalar) {
  const std::string replacement = bytes({0xEF, 0xBF, 0xBD});
  for (const char32_t cp : {U'\xD800', U'\xDC00', U'\xDFFF', U'\x110000', U'\xFFFFFFFF'}) {
    std::string out;
    append_utf8(out, cp);
    EXPECT_EQ(out, replacement);
  }
}

// ---- locate ---------------------------------------------------------------

TEST(Utf8Locate, StartOfInput) {
  EXPECT_EQ(locate("abc", 0), (SourceLocation{.offset = 0, .line = 1, .column = 1}));
}

TEST(Utf8Locate, EmptySource) {
  EXPECT_EQ(locate("", 0), (SourceLocation{.offset = 0, .line = 1, .column = 1}));
}

// 桁はバイトではなくコードポイントで数える
TEST(Utf8Locate, ColumnsAreCodePoints) {
  const std::string source = "日本語";  // 3 バイト × 3 文字
  EXPECT_EQ(locate(source, 0).column, 1U);
  EXPECT_EQ(locate(source, 3).column, 2U);
  EXPECT_EQ(locate(source, 6).column, 3U);
  EXPECT_EQ(locate(source, 9).column, 4U);
}

TEST(Utf8Locate, LineFeed) {
  const std::string source = "abc\ndef";
  EXPECT_EQ(locate(source, 3), (SourceLocation{.offset = 3, .line = 1, .column = 4}));
  EXPECT_EQ(locate(source, 4), (SourceLocation{.offset = 4, .line = 2, .column = 1}));
  EXPECT_EQ(locate(source, 6), (SourceLocation{.offset = 6, .line = 2, .column = 3}));
}

TEST(Utf8Locate, CarriageReturnLineFeedIsOneLine) {
  const std::string source = "abc\r\ndef";
  EXPECT_EQ(locate(source, 3), (SourceLocation{.offset = 3, .line = 1, .column = 4}));
  EXPECT_EQ(locate(source, 5), (SourceLocation{.offset = 5, .line = 2, .column = 1}));
  EXPECT_EQ(locate(source, 7), (SourceLocation{.offset = 7, .line = 2, .column = 3}));
}

TEST(Utf8Locate, CarriageReturnAloneIsOneLine) {
  const std::string source = "abc\rdef";
  EXPECT_EQ(locate(source, 4), (SourceLocation{.offset = 4, .line = 2, .column = 1}));
  EXPECT_EQ(locate(source, 6), (SourceLocation{.offset = 6, .line = 2, .column = 3}));
}

TEST(Utf8Locate, MixedLineEndings) {
  const std::string source = "a\r\nb\rc\nd";
  EXPECT_EQ(locate(source, 3), (SourceLocation{.offset = 3, .line = 2, .column = 1}));  // b
  EXPECT_EQ(locate(source, 5), (SourceLocation{.offset = 5, .line = 3, .column = 1}));  // c
  EXPECT_EQ(locate(source, 7), (SourceLocation{.offset = 7, .line = 4, .column = 1}));  // d
}

TEST(Utf8Locate, MultibyteAcrossLines) {
  const std::string source = "あa\nい";  // 0-2:あ / 3:a / 4:LF / 5-7:い
  EXPECT_EQ(locate(source, 3), (SourceLocation{.offset = 3, .line = 1, .column = 2}));
  EXPECT_EQ(locate(source, 4), (SourceLocation{.offset = 4, .line = 1, .column = 3}));
  EXPECT_EQ(locate(source, 5), (SourceLocation{.offset = 5, .line = 2, .column = 1}));
  EXPECT_EQ(locate(source, 8), (SourceLocation{.offset = 8, .line = 2, .column = 2}));
}

TEST(Utf8Locate, TrailingNewline) {
  const std::string source = "abc\n";
  EXPECT_EQ(locate(source, source.size()), (SourceLocation{.offset = 4, .line = 2, .column = 1}));
}

TEST(Utf8Locate, ClampsOutOfRangeOffset) {
  const std::string source = "abc";
  const SourceLocation end = locate(source, source.size());
  EXPECT_EQ(end, (SourceLocation{.offset = 3, .line = 1, .column = 4}));
  EXPECT_EQ(locate(source, 999), end);
}

// 改行の種類が混ざっていても、各行の先頭は必ず桁 1 になる
TEST(Utf8Locate, EveryLineStartsAtColumnOne) {
  const std::string source = "一行目\n二行目\r\n三行目\r四行目";
  std::size_t offset = 0;
  std::uint32_t expected_line = 1;
  while (true) {
    EXPECT_EQ(locate(source, offset), (SourceLocation{.offset = static_cast<std::uint32_t>(offset),
                                                      .line = expected_line,
                                                      .column = 1}));
    const std::size_t newline = source.find_first_of("\r\n", offset);
    if (newline == std::string::npos) {
      break;
    }
    offset = newline + (source.compare(newline, 2, "\r\n") == 0 ? 2 : 1);
    ++expected_line;
  }
  EXPECT_EQ(expected_line, 4U);
}

}  // namespace
}  // namespace shashoku
