# end-to-end（ARCHITECTURE.md §4）。render() を通した PNG をゴールデンと比べる。
include(TestAssets)

# concurrency_test は std::thread を使う（共有資源の同時利用。A34）。
find_package(Threads REQUIRED)

shashoku_add_test(integration_test
  SOURCES
    golden_test.cpp
    color_font_test.cpp
    error_test.cpp
    limits_test.cpp
    nonfinite_test.cpp
    pipeline_test.cpp
    shaping_test.cpp
    line_policy_test.cpp
    empty_inline_test.cpp
    overflow_wrap_test.cpp
    scale_property_test.cpp
    determinism_test.cpp
    reuse_test.cpp
    concurrency_test.cpp
    fuzz_test.cpp
  LIBS shashoku::shashoku shashoku_test_support Threads::Threads)

shashoku_use_test_fonts(integration_test)
target_compile_definitions(integration_test PRIVATE
  SHASHOKU_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
