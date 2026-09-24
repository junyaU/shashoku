# ② スタイル解決: DOM → スタイル付きツリー（ARCHITECTURE.md §3.7）。
# 依存は core と html/dom.hpp・html/tags.hpp（ヘッダのみ）だけ。
shashoku_add_module(style
  SOURCES
    css_tokens.cpp
    css_color.cpp
    css_parser.cpp
    value_parser.cpp
    ua_stylesheet.cpp
    resolver.cpp
    dump_json.cpp
  DEPS shashoku::core)
