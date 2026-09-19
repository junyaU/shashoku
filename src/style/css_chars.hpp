#pragma once

#include <string>
#include <string_view>

// CSS の文字分類。<cctype> は使わない（ロケール依存を持ち込まないため。DESIGN.md §3-5）。

namespace shashoku::style {

inline bool is_css_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline bool is_css_digit(char c) { return c >= '0' && c <= '9'; }

// 非 ASCII（0x80 以上）は識別子に使える。`游ゴシック` のような裸のフォント名のため。
inline bool is_css_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' ||
         static_cast<unsigned char>(c) >= 0x80;
}

inline bool is_css_ident_char(char c) { return is_css_ident_start(c) || is_css_digit(c); }

inline char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string ascii_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    out.push_back(ascii_lower(c));
  }
  return out;
}

inline std::string_view trim_css_space(std::string_view s) {
  while (!s.empty() && is_css_space(s.front())) {
    s.remove_prefix(1);
  }
  while (!s.empty() && is_css_space(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

}  // namespace shashoku::style
