shashoku_add_test(style_test
  SOURCES
    value_test.cpp
    cascade_test.cpp
    inherit_test.cpp
    error_test.cpp
    tree_test.cpp
    dump_json_test.cpp
    fuzz_test.cpp
  LIBS shashoku::style)
