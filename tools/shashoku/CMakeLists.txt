# CLI（ARCHITECTURE.md §3.10）。公開 API だけを使うので、リンクするのは shashoku::shashoku
# 1 つだけ（各段のモジュールは静的ライブラリの推移的リンクで解決される）。
#
# 実行ファイル名はライブラリと同じ `shashoku` にしたいが、CMake のターゲット名は
# 衝突するので shashoku_cli にして OUTPUT_NAME で付け替える。
add_executable(shashoku_cli main.cpp)
set_target_properties(shashoku_cli PROPERTIES OUTPUT_NAME shashoku)
target_link_libraries(shashoku_cli PRIVATE shashoku::shashoku)
target_compile_features(shashoku_cli PRIVATE cxx_std_23)
shashoku_target_defaults(shashoku_cli)

if(SHASHOKU_BUILD_TESTS)
  include(TestAssets)
  # スモークテスト（ARCHITECTURE.md §4）。引数の組み立てと終了コードの検査を
  # シェルに頼らず CMake スクリプトで行う。
  foreach(case png_ok image_ok unsupported_css dump_box)
    add_test(NAME Cli.${case}
      COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:shashoku_cli>
        -DCASE=${case}
        -DFONT_DIR=${SHASHOKU_TEST_FONT_DIR}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DWORK_DIR=${CMAKE_BINARY_DIR}/test_output/cli
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_test.cmake)
  endforeach()
endif()
