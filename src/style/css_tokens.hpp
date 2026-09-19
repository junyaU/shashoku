#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"

// プロパティの値（`1px solid red` や `rgb(255 0 0 / 50%)`）を成分値の列に分解する。
// 宣言ブロックの構造（`{}` `;` `:`）を読むのは css_parser.hpp の仕事で、こちらは値の中だけを見る。

namespace shashoku::style {

struct ValueToken {
  enum class Kind : std::uint8_t {
    Ident,       // red / solid / "游ゴシック" でない裸の識別子
    String,      // "..." / '...'（text は中身）
    Number,      // 12 / -0.5
    Percentage,  // 50%
    Dimension,   // 12px（unit は小文字化済み）
    Hash,        // #ff0000（text は `#` を除いた部分）
    Comma,
    Slash,
    Function,  // rgb(...)（text は小文字化した関数名、args が中身）
    Delim,     // 上のどれでもない 1 文字（text に入る）
  };

  Kind kind = Kind::Delim;
  bool space_before = false;  // font-family の裸の名前をつなぐのに要る
  std::string text;
  double number = 0;
  std::string unit;
  std::vector<ValueToken> args;
};

// 値の文字列をトークン列にする。コメント `/* */` は読み飛ばす。
// 閉じていない文字列 / コメント / `(` は CssParse。location はエラーに付ける位置。
Result<std::vector<ValueToken>> tokenize_value(std::string_view text, SourceLocation location);

}  // namespace shashoku::style
