# モジュール一覧。依存の向き（上から下）に並べる。依存表は docs/ARCHITECTURE.md を参照。
set(SHASHOKU_MODULES
  core       # 基本型・エラー・UTF-8・JSON ダンプ
  png        # ⑥ PNG エンコード / デコード
  raster     # ⑤b ディスプレイリスト → Bitmap
  linebreak  # ③ の中核: UAX #14 + 禁則（どのモジュールにも依存しない）
  text       # ④ FontStore・シェーピング・グリフのラスタライズ
  html       # ① HTML → DOM
  style      # ② DOM → スタイル付きツリー
  layout     # ③ スタイル付きツリー → ボックスツリー
  paint      # ⑤a ボックスツリー → ディスプレイリスト
  api)       # 公開 API: render()。最終成果物のライブラリ `shashoku` はここで定義する

# パイプラインの段ごとに 1 つの静的ライブラリ（shashoku_<name> / shashoku::<name>）を作る。
#
#   shashoku_add_module(<name>
#     SOURCES <files>...
#     [DEPS <targets>...]          # 公開依存（ヘッダに型が現れるもの）
#     [PRIVATE_DEPS <targets>...]) # 実装だけが使う依存（zlib, freetype など）
#
# DEPS はモジュール間の依存方向（docs/ARCHITECTURE.md の依存表）の宣言でもある。
# 表にない依存を足したくなったら、まず設計を疑うこと。
function(shashoku_add_module name)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;PRIVATE_DEPS" ${ARGN})
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "shashoku_add_module(${name}): SOURCES が空です")
  endif()

  add_library(shashoku_${name} STATIC ${ARG_SOURCES})
  add_library(shashoku::${name} ALIAS shashoku_${name})
  target_include_directories(shashoku_${name} PUBLIC
    ${PROJECT_SOURCE_DIR}/src       # 内部ヘッダ: #include "png/encoder.hpp"
    ${PROJECT_SOURCE_DIR}/include)  # 公開ヘッダ: #include "shashoku/error.hpp"
  target_compile_features(shashoku_${name} PUBLIC cxx_std_23)
  target_link_libraries(shashoku_${name} PUBLIC ${ARG_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
  shashoku_target_defaults(shashoku_${name})
endfunction()

# テスト実行ファイルを 1 つ追加する。
#
#   shashoku_add_test(<name> SOURCES <files>... LIBS <targets>...)
#
# テスト名は <module>_test にする（例: linebreak_test）。モジュール単位で分けておくと
# コアのテストだけを高速に回せる: ctest --preset dev -R LineBreaker
function(shashoku_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest_main GTest::gmock)
  target_include_directories(${name} PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests)
  target_compile_definitions(${name} PRIVATE
    SHASHOKU_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    SHASHOKU_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test_output")
  shashoku_target_defaults(${name})
  gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60)
endfunction()
