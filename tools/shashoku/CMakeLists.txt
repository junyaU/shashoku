# CLI（ARCHITECTURE.md §3.10）。公開 API だけを使うので、リンクするのは shashoku::shashoku
# 1 つだけ（各段のモジュールは静的ライブラリの推移的リンクで解決される）。
#
# 実行ファイル名はライブラリと同じ `shashoku` にしたいが、CMake のターゲット名は
# 衝突するので shashoku_cli にして OUTPUT_NAME で付け替える。
add_executable(shashoku_cli main.cpp embedded.cpp)
set_target_properties(shashoku_cli PROPERTIES OUTPUT_NAME shashoku)
target_link_libraries(shashoku_cli PRIVATE shashoku::shashoku)
target_compile_features(shashoku_cli PRIVATE cxx_std_23)
shashoku_target_defaults(shashoku_cli)

# `--version` が出す版。shashoku 自身は公開 API の shashoku::version() から取り、
# 依存ライブラリの版は cmake/Dependencies.cmake の変数から渡す（CLI に zlib /
# FreeType / HarfBuzz のヘッダを見せないため。§3.10）。
# `--license` の文面はリポジトリ直下の 2 ファイルをそのまま焼き込む（#20）。
target_compile_definitions(shashoku_cli PRIVATE
  SHASHOKU_ZLIB_VERSION="${SHASHOKU_ZLIB_VERSION}"
  SHASHOKU_FREETYPE_VERSION="${SHASHOKU_FREETYPE_VERSION}"
  SHASHOKU_HARFBUZZ_VERSION="${SHASHOKU_HARFBUZZ_VERSION}"
  SHASHOKU_LICENSE_FILE="${PROJECT_SOURCE_DIR}/LICENSE"
  SHASHOKU_THIRD_PARTY_LICENSE_FILE="${PROJECT_SOURCE_DIR}/THIRD_PARTY_LICENSES")

set(shashoku_cli_embedded
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/THIRD_PARTY_LICENSES")

# 既定フォント（#20 / A-new-1）。CLI 層だけの機能で、ライブラリの公開 API は変わらない。
if(SHASHOKU_EMBED_DEFAULT_FONT)
  include(TestAssets)  # SHASHOKU_BUILD_TESTS=OFF でも和文 2 本は取得する
  target_compile_definitions(shashoku_cli PRIVATE
    SHASHOKU_EMBED_DEFAULT_FONT
    SHASHOKU_DEFAULT_FONT_REGULAR="${SHASHOKU_DEFAULT_FONT_REGULAR}"
    SHASHOKU_DEFAULT_FONT_BOLD="${SHASHOKU_DEFAULT_FONT_BOLD}"
    SHASHOKU_DEFAULT_FONT_VERSION="${SHASHOKU_DEFAULT_FONT_VERSION}")
  list(APPEND shashoku_cli_embedded
       "${SHASHOKU_DEFAULT_FONT_REGULAR}" "${SHASHOKU_DEFAULT_FONT_BOLD}")
endif()

# .incbin が読むファイルはコンパイラの依存解析に出てこないので、明示的に依存を張る。
set_source_files_properties(embedded.cpp PROPERTIES
  OBJECT_DEPENDS "${shashoku_cli_embedded}")

# 配布用のリンク（A-new-2）。C++ ランタイム（libc++ / libc++abi / libunwind / libgcc）だけを
# 静的に取り込み、**glibc は動的のまま**にする。素の Ubuntu に無いのは前者なのでこれで足りる。
# 完全静的（-static）にすると glibc（LGPL-2.1）を実行ファイルに取り込むことになり、
# 再リンク手段の提供という条件に触れうるので採らない。
# 変えるのはリンク方法だけで、コンパイルフラグ（-ffp-contract=off 等）は触らない = 絵は変わらない。
if(SHASHOKU_STATIC_RUNTIME)
  target_link_options(shashoku_cli PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(SHASHOKU_BUILD_TESTS)
  include(TestAssets)
  # スモークテスト（ARCHITECTURE.md §4）。引数の組み立てと終了コードの検査を
  # シェルに頼らず CMake スクリプトで行う。
  set(cli_cases png_ok image_ok trim_flags compression unsupported_css dump_box version license)
  if(SHASHOKU_EMBED_DEFAULT_FONT)
    # 既定フォント（#20）。埋め込みが無いビルドでは成り立たないので足さない。
    list(APPEND cli_cases default_font examples)
  endif()
  foreach(case ${cli_cases})
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
