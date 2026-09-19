# ③ レイアウト（ARCHITECTURE.md §3.8）。偽の TextMeasurer を注入してフォントなしで回す。
shashoku_add_test(layout_test
  SOURCES
    test_support.cpp
    block_test.cpp
    inline_test.cpp
    line_break_test.cpp
    dump_json_test.cpp
    error_test.cpp
    property_test.cpp
  LIBS shashoku::layout)
