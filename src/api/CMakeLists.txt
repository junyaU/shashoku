# 最終成果物のライブラリ。利用者がリンクするのはこの `shashoku`（shashoku::shashoku）だけ。
# 各段のモジュールは PRIVATE にリンクする: 静的ライブラリなのでリンク時には推移的に
# 解決されるが、include パスは伝播しない（公開ヘッダだけで完結させるため。§3.10）。
add_library(shashoku STATIC
  font_set.cpp
  image_set.cpp
  render.cpp
  version.cpp
  warning.cpp)
add_library(shashoku::shashoku ALIAS shashoku)

target_include_directories(shashoku
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
target_compile_features(shashoku PUBLIC cxx_std_23)
target_compile_definitions(shashoku PRIVATE SHASHOKU_VERSION="${PROJECT_VERSION}")
target_link_libraries(shashoku PRIVATE
  shashoku::html
  shashoku::style
  shashoku::layout
  shashoku::paint
  shashoku::text
  shashoku::raster
  shashoku::png
  shashoku::core)
shashoku_target_defaults(shashoku)
