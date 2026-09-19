# 最終成果物のライブラリ。利用者がリンクするのはこの `shashoku`（shashoku::shashoku）だけ。
add_library(shashoku STATIC
  version.cpp)
add_library(shashoku::shashoku ALIAS shashoku)

target_include_directories(shashoku
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
target_compile_features(shashoku PUBLIC cxx_std_23)
target_compile_definitions(shashoku PRIVATE SHASHOKU_VERSION="${PROJECT_VERSION}")
shashoku_target_defaults(shashoku)
