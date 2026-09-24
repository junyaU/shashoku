#pragma once

#include <algorithm>
#include <array>
#include <string_view>

// 対応タグの表（ARCHITECTURE.md §3.6）。**ヘッダだけ**で完結させる（リンクする関数は置かない）。
//
// ① html 自身（`parser.cpp`）だけでなく ② style も引く: タイプセレクタが対応外のタグを
// 名指ししていたら、① はそのタグを要素にしない（透過するか `UnsupportedTag`）ので、
// その規則は**決して一致しない**（A55）。表を写すと食い違ったときに style が誤報するので、
// dom.hpp と同じく「ヘッダのみの依存」で共有する。

namespace shashoku::html {

// エラーメッセージに並べるので辞書順に持つ。
inline constexpr std::array<std::string_view, 15> kSupportedTags{
    "br",  "div", "h1", "h2", "h3",   "h4",   "h5",   "h6",
    "img", "p",   "rp", "rt", "ruby", "span", "style"};

constexpr bool is_supported_tag(std::string_view tag) {
  return std::ranges::find(kSupportedTags, tag) != kSupportedTags.end();
}

}  // namespace shashoku::html
