#include "core/json_writer.hpp"

#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace shashoku {
namespace {

constexpr std::size_t kIndentWidth = 2;

// std::to_chars / std::format はロケールに依存しない（DESIGN.md §3-5 の決定性）。
// 数値の桁数は double の最短往復表現（最長 24 文字程度）に十分な大きさをとる。
using NumberBuffer = std::array<char, 48>;

template <class T>
void append_number(std::string& out, T value) {
  NumberBuffer buf{};
  const std::to_chars_result res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
  assert(res.ec == std::errc{});
  out.append(buf.data(), res.ptr);
}

// 非有限値は JSON で表せないので null にする（ヘッダのコメント）。
template <class T>
void append_float(std::string& out, T value) {
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }
  // 引数なしの to_chars は「元の値に戻せる最短表現」を、整数値なら小数点なしで書く。
  append_number(out, value);
}

// JSON が要求するのは " ・\ ・U+0000..U+001F のエスケープだけ。非 ASCII は UTF-8 のまま出す
// （DEL や C1 制御文字も JSON 上はエスケープ不要なのでそのまま通す）。
void append_quoted(std::string& out, std::string_view s) {
  out += '"';
  for (const char c : s) {
    const auto b = static_cast<unsigned char>(c);
    switch (b) {
      case '"':
        out += R"(\")";
        break;
      case '\\':
        out += R"(\\)";
        break;
      case '\b':
        out += R"(\b)";
        break;
      case '\f':
        out += R"(\f)";
        break;
      case '\n':
        out += R"(\n)";
        break;
      case '\r':
        out += R"(\r)";
        break;
      case '\t':
        out += R"(\t)";
        break;
      default:
        if (b < 0x20) {
          out += std::format(R"(\u{:04X})", b);
        } else {
          out += c;
        }
        break;
    }
  }
  out += '"';
}

}  // namespace

void JsonWriter::newline_indent() {
  out_ += '\n';
  out_.append(stack_.size() * kIndentWidth, ' ');
}

void JsonWriter::before_value() {
  if (stack_.empty()) {
    // トップレベルに書ける値は 1 つだけ
    assert(out_.empty());
    return;
  }
  Frame& frame = stack_.back();
  if (frame.scope == Scope::Object) {
    // object の中では key() が区切りとインデントを済ませている
    assert(after_key_);
    after_key_ = false;
    return;
  }
  if (frame.has_items) {
    out_ += ',';
  }
  frame.has_items = true;
  newline_indent();
}

JsonWriter& JsonWriter::begin_object() {
  before_value();
  out_ += '{';
  stack_.push_back(Frame{.scope = Scope::Object});
  return *this;
}

JsonWriter& JsonWriter::end_object() {
  assert(!stack_.empty());
  assert(stack_.back().scope == Scope::Object);
  assert(!after_key_);  // key() の直後に end_object() はできない
  const bool has_items = stack_.back().has_items;
  stack_.pop_back();
  if (has_items) {
    newline_indent();
  }
  out_ += '}';
  return *this;
}

JsonWriter& JsonWriter::begin_array() {
  before_value();
  out_ += '[';
  stack_.push_back(Frame{.scope = Scope::Array});
  return *this;
}

JsonWriter& JsonWriter::end_array() {
  assert(!stack_.empty());
  assert(stack_.back().scope == Scope::Array);
  const bool has_items = stack_.back().has_items;
  stack_.pop_back();
  if (has_items) {
    newline_indent();
  }
  out_ += ']';
  return *this;
}

JsonWriter& JsonWriter::key(std::string_view name) {
  assert(!stack_.empty());
  assert(stack_.back().scope == Scope::Object);
  assert(!after_key_);  // key() を 2 回続けて呼べない
  Frame& frame = stack_.back();
  if (frame.has_items) {
    out_ += ',';
  }
  frame.has_items = true;
  newline_indent();
  append_quoted(out_, name);
  out_ += ": ";
  after_key_ = true;
  return *this;
}

JsonWriter& JsonWriter::value(std::string_view s) {
  before_value();
  append_quoted(out_, s);
  return *this;
}

JsonWriter& JsonWriter::value(const char* s) {
  assert(s != nullptr);
  return value(std::string_view{s});
}

JsonWriter& JsonWriter::value(float f) {
  before_value();
  append_float(out_, f);
  return *this;
}

JsonWriter& JsonWriter::value(double f) {
  before_value();
  append_float(out_, f);
  return *this;
}

JsonWriter& JsonWriter::value(std::int64_t i) {
  before_value();
  append_number(out_, i);
  return *this;
}

JsonWriter& JsonWriter::value(std::uint64_t i) {
  before_value();
  append_number(out_, i);
  return *this;
}

JsonWriter& JsonWriter::value(int i) {
  before_value();
  append_number(out_, i);
  return *this;
}

JsonWriter& JsonWriter::value(unsigned i) {
  before_value();
  append_number(out_, i);
  return *this;
}

JsonWriter& JsonWriter::value(bool b) {
  before_value();
  out_ += b ? "true" : "false";
  return *this;
}

JsonWriter& JsonWriter::null() {
  before_value();
  out_ += "null";
  return *this;
}

std::string JsonWriter::str() && {
  assert(stack_.empty());  // 閉じていない object / array がある
  assert(!after_key_);
  return std::move(out_);
}

const std::string& JsonWriter::str() const& {
  assert(stack_.empty());
  assert(!after_key_);
  return out_;
}

}  // namespace shashoku
