#pragma once

#include <optional>
#include <string_view>

#include "style/css_tokens.hpp"
#include "style/declaration.hpp"

// CSS の色値。対応するのは ARCHITECTURE.md §3.7 の範囲:
//   #rgb / #rgba / #rrggbb / #rrggbbaa
//   rgb() / rgba()（`rgb(255, 0, 0)` の旧構文と `rgb(255 0 0 / 50%)` の新構文の両方）
//   CSS の色名 148 色 / transparent / currentColor
// hsl() / lab() / color() などは対応しない（呼び出し側が UnsupportedValue にする）。

namespace shashoku::style {

// 色として読めなければ nullopt（エラーメッセージはプロパティ名を知る呼び出し側が作る）。
std::optional<SpecColor> parse_color_token(const ValueToken& token);

// 色名 → 色。`transparent` を含む。見つからなければ nullopt。
std::optional<Color> lookup_color_name(std::string_view lower_name);

}  // namespace shashoku::style
