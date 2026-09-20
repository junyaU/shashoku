cmake_minimum_required(VERSION 3.22...3.31)

project(shashoku
  VERSION 0.1.0
  DESCRIPTION "HTML-to-PNG rendering engine specialized in Japanese typesetting"
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(SHASHOKU_BUILD_TESTS "テストをビルドする" ${PROJECT_IS_TOP_LEVEL})
option(SHASHOKU_BUILD_TOOLS "CLI（tools/shashoku）をビルドする" ${PROJECT_IS_TOP_LEVEL})
option(SHASHOKU_WARNINGS_AS_ERRORS "警告をエラーとして扱う" OFF)
option(SHASHOKU_SANITIZE "ASan + UBSan を有効にする（依存ライブラリにも適用）" OFF)

# ---- 言語規格 ---------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++ モジュールは使わない。CMake 3.28+ は C++20 以上で依存スキャンを自動で有効にし、
# compile_commands.json にビルド時生成の @*.modmap を書き込むため、ビルド前に走る
# clang-tidy が「ファイルがない」で落ちる。ビルドも無駄に遅くなるので明示的に切る。
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# std::expected はエラー型の中核（DESIGN.md §7）。無いツールチェーンでは
# 謎のコンパイルエラーになる前に、configure 時点で理由つきで落とす。
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
  #include <expected>
  int main() {
    std::expected<int, int> e{1};
    return e.has_value() ? 0 : 1;
  }" SHASHOKU_HAS_STD_EXPECTED)
if(NOT SHASHOKU_HAS_STD_EXPECTED)
  # （行頭を空白にすると CMake が勝手に折り返さない）
  message(FATAL_ERROR
    "このツールチェーンでは <expected> (C++23) が使えません: "
    "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
    "  対応環境: Clang 17+ と libc++ 17+（-stdlib=libc++）、または GCC 13+\n"
    "  Ubuntu 22.04 の場合:\n"
    "    sudo apt install libc++-18-dev libc++abi-18-dev\n"
    "    cmake --preset dev")
endif()

# ---- サニタイザ -------------------------------------------------------------
# 依存ライブラリより前に置く（zlib / GoogleTest も同じフラグでビルドする）
if(SHASHOKU_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(CompilerOptions)
include(Modules)
include(Dependencies)

add_subdirectory(src)

if(SHASHOKU_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# CLI はテストのあと（CLI のスモークテストが add_test を使うので enable_testing() より後に置く）
if(SHASHOKU_BUILD_TOOLS)
  add_subdirectory(tools/shashoku)
endif()
