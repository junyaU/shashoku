include(GoogleTest)

# テスト実行ファイルを 1 つ追加する。
#   shashoku_add_test(<name> <sources>... [LIBS <extra-libs>...])
# モジュール単位で分けておくと、コア（行分割器など）のテストだけを高速に回せる:
#   ctest --preset dev -R LineBreaker
function(shashoku_add_test name)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  add_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(${name} PRIVATE shashoku GTest::gtest_main GTest::gmock ${ARG_LIBS})
  # 内部モジュールを直接テストするため src/ を見せる: #include "linebreak/..." 等
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  shashoku_target_defaults(${name})
  gtest_discover_tests(${name} DISCOVERY_TIMEOUT 30)
endfunction()

shashoku_add_test(toolchain_test toolchain_test.cpp LIBS ZLIB::ZLIBSTATIC)
