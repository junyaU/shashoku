#include "style/css_tokens.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/result.hpp"
#include "style/css_chars.hpp"

namespace shashoku::style {
namespace {

// `((((…` のような入力でスタックを使い切らないための上限。実用上の入れ子は 1 段だけ。
constexpr int kMaxFunctionDepth = 8;

// 10^0 .. 10^22 は double で厳密に表せる。この範囲なら
// 「仮数（2^53 以下の整数）× または ÷ 10^k」の 1 回の演算が正しく丸められる
// （strtod の高速経路と同じ理屈）。libc++-18 には浮動小数点の std::from_chars がなく、
// std::strtod はロケール依存なので、自前で書く。
constexpr std::array<double, 23> kPow10 = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};
constexpr int kMaxExactPow10 = 22;
// 15 桁までなら仮数は 2^53 未満に収まる（= 1 回の乗除算で正しく丸まる）。
// CSS の長さでこれ以上の有効桁が要ることはない。超えた桁は指数に繰り上げるか捨てる。
constexpr int kMaxMantissaDigits = 15;

// double が表せる 10 の冪の範囲（10^309 で無限大、10^-324 で 0）。外に出たら丸めるだけ。
constexpr int kExponentLimit = 400;

double scale_by_pow10(double mantissa, int exponent) {
  if (exponent == 0 || mantissa == 0) {
    return mantissa;
  }
  if (exponent > kExponentLimit) {
    return mantissa * std::numeric_limits<double>::infinity();
  }
  if (exponent < -kExponentLimit) {
    return 0;
  }
  double value = mantissa;
  int left = exponent > 0 ? exponent : -exponent;
  while (left > kMaxExactPow10) {
    value = exponent > 0 ? value * kPow10[kMaxExactPow10] : value / kPow10[kMaxExactPow10];
    left -= kMaxExactPow10;
  }
  const double factor = kPow10[static_cast<std::size_t>(left)];
  return exponent > 0 ? value * factor : value / factor;
}

// `[+-]?(\d+(\.\d+)?|\.\d+)([eE][+-]?\d+)?` に一致する接頭辞を読む。
// 呼び出し側が先に starts_number() で確かめているので、数字は必ず 1 つ以上ある。
struct DecimalScan {
  double value = 0;
  std::size_t length = 0;
};

// 仮数を作りながら 10 の冪を数える状態。
struct Mantissa {
  std::uint64_t value = 0;
  int digits = 0;    // 仮数に取り込んだ有効桁数
  int exponent = 0;  // 仮数に対する 10 の冪
  bool seen_dot = false;

  void push(char c) {
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (digits >= kMaxMantissaDigits) {
      // 仮数に入りきらない整数部の桁は指数で表す。小数部の桁は捨ててよい
      exponent += seen_dot ? 0 : 1;
      return;
    }
    // 先頭の 0 は有効桁に数えない（0.0001 でも有効桁を使い切らないように）
    if (value != 0 || digit != 0) {
      value = (value * 10) + digit;
      ++digits;
    }
    exponent -= seen_dot ? 1 : 0;
  }
};

// 数字列（小数点を 1 つ含みうる）を読む。読んだ長さを返す。
std::size_t read_digits(std::string_view text, Mantissa& mantissa) {
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '.') {
      // 小数点は 1 つだけ。直後に数字がなければ数値の一部ではない
      if (mantissa.seen_dot || i + 1 >= text.size() || !is_css_digit(text[i + 1])) {
        break;
      }
      mantissa.seen_dot = true;
      ++i;
      continue;
    }
    if (!is_css_digit(text[i])) {
      break;
    }
    mantissa.push(text[i]);
    ++i;
  }
  return i;
}

// `e+12` のような指数部を読む。指数部でなければ長さ 0。
struct ExponentScan {
  int value = 0;
  std::size_t length = 0;
};

ExponentScan read_exponent(std::string_view text) {
  if (text.empty() || (text[0] != 'e' && text[0] != 'E')) {
    return {};
  }
  std::size_t i = 1;
  bool negative = false;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }
  if (i >= text.size() || !is_css_digit(text[i])) {
    return {};  // `1em` の `e` のように、指数部ではない
  }
  std::int64_t value = 0;
  while (i < text.size() && is_css_digit(text[i])) {
    // 桁あふれは無限大 / 0 に落ちるので、適当なところで頭打ちにしてよい
    value = value < 100000 ? (value * 10) + (text[i] - '0') : value;
    ++i;
  }
  return {.value = static_cast<int>(negative ? -value : value), .length = i};
}

DecimalScan parse_decimal(std::string_view text) {
  std::size_t i = 0;
  bool negative = false;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }

  Mantissa mantissa;
  const std::size_t digits = read_digits(text.substr(i), mantissa);
  if (digits == 0) {
    return {};
  }
  i += digits;

  const ExponentScan exponent = read_exponent(text.substr(i));
  i += exponent.length;

  const double magnitude =
      scale_by_pow10(static_cast<double>(mantissa.value), mantissa.exponent + exponent.value);
  return {.value = negative ? -magnitude : magnitude, .length = i};
}

struct Tokenizer {
  std::string_view text;
  SourceLocation location;
  std::size_t pos = 0;

  [[nodiscard]] bool eof() const { return pos >= text.size(); }
  [[nodiscard]] char at(std::size_t i) const { return i < text.size() ? text[i] : '\0'; }
  [[nodiscard]] char peek() const { return at(pos); }

  [[nodiscard]] std::unexpected<Error> bad(std::string message) const {
    return fail(ErrorKind::CssParse, std::move(message), location);
  }

  // 空白とコメントを読み飛ばす。読み飛ばしたものがあれば true。
  Result<bool> skip_trivia();
  std::string read_ident();
  Result<std::string> read_string();
  ValueToken read_number();
  Result<std::vector<ValueToken>> read_tokens(int depth, bool in_function);
};

Result<bool> Tokenizer::skip_trivia() {
  bool skipped = false;
  while (!eof()) {
    if (is_css_space(peek())) {
      ++pos;
      skipped = true;
      continue;
    }
    if (peek() != '/' || at(pos + 1) != '*') {
      break;
    }
    const std::size_t end = text.find("*/", pos + 2);
    if (end == std::string_view::npos) {
      return bad("unclosed comment `/*`");
    }
    pos = end + 2;
    skipped = true;
  }
  return skipped;
}

std::string Tokenizer::read_ident() {
  const std::size_t start = pos;
  while (!eof() && is_css_ident_char(peek())) {
    ++pos;
  }
  return std::string{text.substr(start, pos - start)};
}

Result<std::string> Tokenizer::read_string() {
  const char quote = peek();
  ++pos;
  const std::size_t start = pos;
  while (!eof() && peek() != quote && peek() != '\n') {
    ++pos;
  }
  if (eof() || peek() == '\n') {
    return bad("unclosed string in a CSS value");
  }
  std::string out{text.substr(start, pos - start)};
  ++pos;  // 閉じ引用符
  return out;
}

ValueToken Tokenizer::read_number() {
  const DecimalScan scan = parse_decimal(text.substr(pos));
  pos += scan.length;

  ValueToken token;
  token.number = scan.value;
  if (peek() == '%') {
    ++pos;
    token.kind = ValueToken::Kind::Percentage;
    return token;
  }
  if (is_css_ident_start(peek())) {
    token.kind = ValueToken::Kind::Dimension;
    token.unit = ascii_lower(read_ident());
    return token;
  }
  token.kind = ValueToken::Kind::Number;
  return token;
}

// 数値の始まり（`12` `-1` `.5` `+.5`）かどうか。`-red` のような識別子と区別する。
bool starts_number(const Tokenizer& tokenizer) {
  const char c = tokenizer.peek();
  if (is_css_digit(c)) {
    return true;
  }
  if (c == '.') {
    return is_css_digit(tokenizer.at(tokenizer.pos + 1));
  }
  if (c == '+' || c == '-') {
    const char next = tokenizer.at(tokenizer.pos + 1);
    return is_css_digit(next) || (next == '.' && is_css_digit(tokenizer.at(tokenizer.pos + 2)));
  }
  return false;
}

Result<ValueToken> read_one(Tokenizer& tokenizer, int depth);

// 関数記法 rgb(...) の入れ子をそのまま再帰で読む。深さは kMaxFunctionDepth で制限する。
// NOLINTNEXTLINE(misc-no-recursion)
Result<std::vector<ValueToken>> Tokenizer::read_tokens(int depth, bool in_function) {
  std::vector<ValueToken> out;
  while (true) {
    const Result<bool> space = skip_trivia();
    if (!space) {
      return std::unexpected(space.error());
    }
    if (eof()) {
      if (in_function) {
        return bad("unclosed `(` in a CSS value");
      }
      return out;
    }
    if (in_function && peek() == ')') {
      ++pos;
      return out;
    }
    Result<ValueToken> token = read_one(*this, depth);
    if (!token) {
      return std::unexpected(token.error());
    }
    token->space_before = *space;
    out.push_back(*std::move(token));
  }
}

// NOLINTNEXTLINE(misc-no-recursion): 同上（read_tokens との相互再帰）
Result<ValueToken> read_one(Tokenizer& tokenizer, int depth) {
  const char c = tokenizer.peek();
  ValueToken token;
  if (c == '"' || c == '\'') {
    Result<std::string> str = tokenizer.read_string();
    if (!str) {
      return std::unexpected(str.error());
    }
    token.kind = ValueToken::Kind::String;
    token.text = *std::move(str);
    return token;
  }
  if (c == ',' || c == '/') {
    ++tokenizer.pos;
    token.kind = c == ',' ? ValueToken::Kind::Comma : ValueToken::Kind::Slash;
    return token;
  }
  if (c == '#') {
    ++tokenizer.pos;
    token.kind = ValueToken::Kind::Hash;
    token.text = tokenizer.read_ident();
    return token;
  }
  if (starts_number(tokenizer)) {
    return tokenizer.read_number();
  }
  if (is_css_ident_start(c)) {
    std::string name = tokenizer.read_ident();
    if (tokenizer.peek() != '(') {
      token.kind = ValueToken::Kind::Ident;
      token.text = std::move(name);
      return token;
    }
    if (depth >= kMaxFunctionDepth) {
      return tokenizer.bad("CSS value has too many nested `(`");
    }
    ++tokenizer.pos;
    Result<std::vector<ValueToken>> args = tokenizer.read_tokens(depth + 1, true);
    if (!args) {
      return std::unexpected(args.error());
    }
    token.kind = ValueToken::Kind::Function;
    token.text = ascii_lower(name);
    token.args = *std::move(args);
    return token;
  }
  ++tokenizer.pos;
  token.kind = ValueToken::Kind::Delim;
  token.text = std::string(1, c);
  return token;
}

}  // namespace

Result<std::vector<ValueToken>> tokenize_value(std::string_view text, SourceLocation location) {
  Tokenizer tokenizer{.text = text, .location = location};
  return tokenizer.read_tokens(0, false);
}

}  // namespace shashoku::style
