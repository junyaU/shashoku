# ソースは明示的に列挙する（GLOB は使わない: ファイル追加時に再 configure が走らないため）。
# モジュールを足すときは src/<module>/xxx.cpp をここに 1 行追加する。
add_library(shashoku STATIC
  version.cpp)
add_library(shashoku::shashoku ALIAS shashoku)

target_include_directories(shashoku
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>  # 公開 API: #include "shashoku/..."
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})                       # 内部ヘッダ: #include "png/..." 等

target_compile_features(shashoku PUBLIC cxx_std_23)
target_compile_definitions(shashoku PRIVATE SHASHOKU_VERSION="${PROJECT_VERSION}")
target_link_libraries(shashoku PRIVATE ZLIB::ZLIBSTATIC)
shashoku_target_defaults(shashoku)
