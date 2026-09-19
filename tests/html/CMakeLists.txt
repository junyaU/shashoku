shashoku_add_test(html_test
  SOURCES
    parser_test.cpp
    dump_json_test.cpp
    fuzz_test.cpp
  LIBS shashoku::html)
