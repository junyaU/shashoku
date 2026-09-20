# end-to-end（ARCHITECTURE.md §4）。render() を通した PNG をゴールデンと比べる。
include(TestAssets)

shashoku_add_test(integration_test
  SOURCES
    golden_test.cpp
    error_test.cpp
    limits_test.cpp
    pipeline_test.cpp
    shaping_test.cpp
    line_policy_test.cpp
    scale_property_test.cpp
    determinism_test.cpp
    fuzz_test.cpp
  LIBS shashoku::shashoku shashoku_test_support)

shashoku_use_test_fonts(integration_test)
target_compile_definitions(integration_test PRIVATE
  SHASHOKU_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
