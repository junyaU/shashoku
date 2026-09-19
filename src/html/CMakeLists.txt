# ① HTML サブセット → DOM（ARCHITECTURE.md §3.6）。契約ヘッダは dom.hpp。
shashoku_add_module(html
  SOURCES
    parser.cpp
  DEPS shashoku::core)
