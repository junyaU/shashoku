# ③ の中核: UAX #14 + 禁則（docs/ARCHITECTURE.md §3.4）。
# 標準ライブラリ以外の何にも依存しない（core にも）ので DEPS は空のまま。
shashoku_add_module(linebreak
  SOURCES
    break_class.cpp
    punctuation.cpp
    line_breaker.cpp)
