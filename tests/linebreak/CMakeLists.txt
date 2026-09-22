# 禁則処理の実質的な仕様書（DESIGN.md §10-2 / ARCHITECTURE.md §3.4）。
shashoku_add_test(linebreak_test
  SOURCES
    test_support.cpp
    break_class_test.cpp
    opportunity_test.cpp
    break_lines_test.cpp
    complexity_test.cpp
    item_policy_test.cpp
    min_content_test.cpp
    spacing_test.cpp
    overhang_test.cpp
    overflow_test.cpp
    property_test.cpp
  LIBS shashoku::linebreak)
