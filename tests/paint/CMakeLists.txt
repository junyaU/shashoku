# ⑤a ペイント（ARCHITECTURE.md §3.9）。ボックスツリーを手で組んで命令列を検証する。
shashoku_add_test(paint_test
  SOURCES
    block_test.cpp
    inline_test.cpp
    vertical_test.cpp
    dump_test.cpp
  LIBS shashoku::paint)
