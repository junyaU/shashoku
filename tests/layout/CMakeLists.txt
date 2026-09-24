# ③ レイアウト（ARCHITECTURE.md §3.8）。偽の TextMeasurer を注入してフォントなしで回す。
shashoku_add_test(layout_test
  SOURCES
    test_support.cpp
    block_test.cpp
    inline_test.cpp
    empty_inline_test.cpp
    flex_test.cpp
    box_sizing_test.cpp
    image_test.cpp
    vertical_test.cpp
    ruby_test.cpp
    line_break_test.cpp
    east_asian_width_test.cpp
    complexity_test.cpp
    geometry_limit_test.cpp
    overflow_test.cpp
    dump_json_test.cpp
    error_test.cpp
    property_test.cpp
    writing_mode_property_test.cpp
    invariance_test.cpp
  LIBS shashoku::layout)
