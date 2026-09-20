# 基本型・エラー・UTF-8・JSON ダンプ。どのモジュールにも依存しない（ARCHITECTURE.md §2）。
shashoku_add_module(core
  SOURCES
    error.cpp
    utf8.cpp
    json_writer.cpp)
