# テスト共通のヘルパー（ゴールデン比較）。製品コードではないので shashoku_add_module は使わず、
# 素の add_library + shashoku_target_defaults() で作る。
add_library(shashoku_test_support STATIC golden.cpp)
target_include_directories(shashoku_test_support PUBLIC
  ${PROJECT_SOURCE_DIR}/tests    # #include "support/golden.hpp"
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/include)
target_compile_features(shashoku_test_support PUBLIC cxx_std_23)
target_link_libraries(shashoku_test_support PUBLIC shashoku::png GTest::gtest)
target_compile_definitions(shashoku_test_support PUBLIC
  SHASHOKU_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")
target_compile_definitions(shashoku_test_support PRIVATE
  SHASHOKU_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test_output")
shashoku_target_defaults(shashoku_test_support)

# ヘルパー自身のテスト。期待画像は一時ディレクトリに作り、tests/golden/ は触らない。
shashoku_add_test(support_test
  SOURCES golden_test.cpp
  LIBS shashoku_test_support shashoku::png)
