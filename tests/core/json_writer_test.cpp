#include "core/json_writer.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace shashoku {
namespace {

// 値 1 つだけを書いた結果を得る（数値の書式を確かめるための最短経路）。
template <class T>
std::string scalar(T value) {
  JsonWriter writer;
  writer.value(value);
  return std::move(writer).str();
}

std::string quoted(std::string_view s) {
  JsonWriter writer;
  writer.value(s);
  return std::move(writer).str();
}

// ---- 構造 -----------------------------------------------------------------

TEST(JsonWriter, EmptyObject) {
  JsonWriter writer;
  writer.begin_object().end_object();
  EXPECT_EQ(std::move(writer).str(), "{}");
}

TEST(JsonWriter, EmptyArray) {
  JsonWriter writer;
  writer.begin_array().end_array();
  EXPECT_EQ(std::move(writer).str(), "[]");
}

// json_writer.hpp の冒頭コメントにある例そのもの
TEST(JsonWriter, HeaderExample) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("tag").value("div");
  writer.key("rect").begin_array().value(0.0F).value(12.5F).end_array();
  writer.end_object();

  EXPECT_EQ(std::move(writer).str(),
            "{\n"
            "  \"tag\": \"div\",\n"
            "  \"rect\": [\n"
            "    0,\n"
            "    12.5\n"
            "  ]\n"
            "}");
}

TEST(JsonWriter, IndentsNestedContainersByTwoSpaces) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("a").begin_object().end_object();
  writer.key("b").begin_array().end_array();
  writer.key("c").begin_array().begin_object().key("x").value(1).end_object().end_array();
  writer.end_object();

  EXPECT_EQ(std::move(writer).str(),
            "{\n"
            "  \"a\": {},\n"
            "  \"b\": [],\n"
            "  \"c\": [\n"
            "    {\n"
            "      \"x\": 1\n"
            "    }\n"
            "  ]\n"
            "}");
}

TEST(JsonWriter, NestedArrays) {
  JsonWriter writer;
  writer.begin_array();
  writer.begin_array().value(1).value(2).end_array();
  writer.begin_array().end_array();
  writer.end_array();

  EXPECT_EQ(std::move(writer).str(),
            "[\n"
            "  [\n"
            "    1,\n"
            "    2\n"
            "  ],\n"
            "  []\n"
            "]");
}

// キーは呼び出し順のまま（並べ替えない）: ダンプの決定性の根拠
TEST(JsonWriter, KeepsKeyOrder) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("z").value(1);
  writer.key("a").value(2);
  writer.key("m").value(3);
  writer.end_object();

  EXPECT_EQ(std::move(writer).str(),
            "{\n"
            "  \"z\": 1,\n"
            "  \"a\": 2,\n"
            "  \"m\": 3\n"
            "}");
}

TEST(JsonWriter, TopLevelScalar) {
  EXPECT_EQ(scalar(42), "42");
  JsonWriter writer;
  writer.null();
  EXPECT_EQ(std::move(writer).str(), "null");
}

TEST(JsonWriter, StrIsReadableWithoutMoving) {
  JsonWriter writer;
  writer.begin_object().end_object();
  const JsonWriter& const_writer = writer;
  EXPECT_EQ(const_writer.str(), "{}");
  EXPECT_EQ(const_writer.str(), "{}");  // 読んでも壊れない
}

TEST(JsonWriter, KeysAreEscapedToo) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("a\"b\n").value(0);
  writer.end_object();
  EXPECT_EQ(std::move(writer).str(), "{\n  \"a\\\"b\\n\": 0\n}");
}

// ---- 文字列のエスケープ ---------------------------------------------------

TEST(JsonWriterString, EscapesQuoteAndBackslash) {
  const std::string out = quoted("a\"b\\c");
  // " a \ " b \ \ c " の 9 文字（バックスラッシュは 2 文字に増える）
  EXPECT_EQ(out, "\"a\\\"b\\\\c\"");
  EXPECT_EQ(out.size(), 9U);
}

TEST(JsonWriterString, EscapesNamedControlCharacters) {
  const std::string out = quoted("\b\f\n\r\t");
  EXPECT_EQ(out, "\"\\b\\f\\n\\r\\t\"");
  EXPECT_EQ(out.size(), 12U);
}

// 名前のない制御文字は \u00XX（4 桁・大文字）で書く
TEST(JsonWriterString, EscapesOtherControlCharactersAsHex) {
  std::string input;
  input.push_back('\0');
  input.push_back('\x01');
  input.push_back('\x0B');
  input.push_back('\x1F');
  EXPECT_EQ(quoted(input), "\"\\u0000\\u0001\\u000B\\u001F\"");
}

TEST(JsonWriterString, LeavesPrintableAsciiAlone) {
  EXPECT_EQ(quoted(" !#$%&'()*+,-./09:;<=>?@AZ[]^_`az{|}~"),
            "\" !#$%&'()*+,-./09:;<=>?@AZ[]^_`az{|}~\"");
  // U+007F (DEL) は JSON 上エスケープ不要なのでそのまま通す
  EXPECT_EQ(quoted("\x7F"), "\"\x7F\"");
}

// 非 ASCII は UTF-8 のまま出す（\u エスケープにしない）
TEST(JsonWriterString, PassesJapaneseThrough) {
  EXPECT_EQ(quoted("日本語の組版"), "\"日本語の組版\"");
  EXPECT_EQ(quoted("「約物」、。゛゜"), "\"「約物」、。゛゜\"");
  EXPECT_EQ(quoted("絵文字🍣も"), "\"絵文字🍣も\"");
  // 出力に含まれる日本語のバイト数は入力そのまま（エスケープで膨らまない）
  EXPECT_EQ(quoted("日本語").size(), std::string("日本語").size() + 2U);
}

TEST(JsonWriterString, EmptyString) { EXPECT_EQ(quoted(""), "\"\""); }

TEST(JsonWriterString, AcceptsCharPointer) {
  JsonWriter writer;
  writer.value("div");  // const char* が bool に化けない
  EXPECT_EQ(std::move(writer).str(), "\"div\"");
}

// ---- 数値 -----------------------------------------------------------------

// 整数値の float は小数点なし
TEST(JsonWriterNumber, IntegralFloatsHaveNoDecimalPoint) {
  EXPECT_EQ(scalar(0.0F), "0");
  EXPECT_EQ(scalar(12.0F), "12");
  EXPECT_EQ(scalar(-3.0F), "-3");
  EXPECT_EQ(scalar(0.0), "0");
  EXPECT_EQ(scalar(12.0), "12");
}

// 最短往復表現（float は float の精度で、double は double の精度で）
TEST(JsonWriterNumber, ShortestRoundTripRepresentation) {
  EXPECT_EQ(scalar(12.5F), "12.5");
  EXPECT_EQ(scalar(-2.25F), "-2.25");
  EXPECT_EQ(scalar(0.1F), "0.1");
  EXPECT_EQ(scalar(1.0F / 3.0F), "0.33333334");
  EXPECT_EQ(scalar(0.1), "0.1");
  EXPECT_EQ(scalar(1.0 / 3.0), "0.3333333333333333");
}

TEST(JsonWriterNumber, NegativeZeroKeepsSign) { EXPECT_EQ(scalar(-0.0F), "-0"); }

// NaN / Inf は JSON で表せないので null
TEST(JsonWriterNumber, NonFiniteBecomesNull) {
  EXPECT_EQ(scalar(std::numeric_limits<float>::quiet_NaN()), "null");
  EXPECT_EQ(scalar(std::numeric_limits<float>::infinity()), "null");
  EXPECT_EQ(scalar(-std::numeric_limits<float>::infinity()), "null");
  EXPECT_EQ(scalar(std::numeric_limits<double>::quiet_NaN()), "null");
  EXPECT_EQ(scalar(std::numeric_limits<double>::infinity()), "null");
  EXPECT_EQ(scalar(-std::numeric_limits<double>::infinity()), "null");
}

TEST(JsonWriterNumber, Integers) {
  EXPECT_EQ(scalar(0), "0");
  EXPECT_EQ(scalar(-42), "-42");
  EXPECT_EQ(scalar(2147483647), "2147483647");
  EXPECT_EQ(scalar(4294967295U), "4294967295");
  EXPECT_EQ(scalar(std::numeric_limits<std::int64_t>::min()), "-9223372036854775808");
  EXPECT_EQ(scalar(std::numeric_limits<std::int64_t>::max()), "9223372036854775807");
  EXPECT_EQ(scalar(std::numeric_limits<std::uint64_t>::max()), "18446744073709551615");
}

TEST(JsonWriterNumber, Booleans) {
  EXPECT_EQ(scalar(true), "true");
  EXPECT_EQ(scalar(false), "false");
}

// ---- 実際のダンプに近い形 -------------------------------------------------

TEST(JsonWriter, DumpLikeTree) {
  JsonWriter writer;
  writer.begin_object();
  writer.key("tag").value("p");
  writer.key("rect").begin_array().value(0.0F).value(0.0F).value(320.0F).value(28.5F).end_array();
  writer.key("children").begin_array();
  writer.begin_object();
  writer.key("text").value("日本語");
  writer.key("baseline").value(22.0F);
  writer.key("truncated").value(false);
  writer.end_object();
  writer.end_array();
  writer.key("font").null();
  writer.end_object();

  EXPECT_EQ(std::move(writer).str(),
            "{\n"
            "  \"tag\": \"p\",\n"
            "  \"rect\": [\n"
            "    0,\n"
            "    0,\n"
            "    320,\n"
            "    28.5\n"
            "  ],\n"
            "  \"children\": [\n"
            "    {\n"
            "      \"text\": \"日本語\",\n"
            "      \"baseline\": 22,\n"
            "      \"truncated\": false\n"
            "    }\n"
            "  ],\n"
            "  \"font\": null\n"
            "}");
}

}  // namespace
}  // namespace shashoku
