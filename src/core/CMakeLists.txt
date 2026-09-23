# 基本型・エラー・警告・診断・UTF-8・JSON ダンプ。どのモジュールにも依存しない
# （ARCHITECTURE.md §2）。warning.cpp は to_string(RenderFailure) が
# to_string(WarningKind) を使うので api ではなくここに置く（A46）。
shashoku_add_module(core
  SOURCES
    error.cpp
    warning.cpp
    diagnostics.cpp
    utf8.cpp
    json_writer.cpp)
