# 外部依存はすべて FetchContent でバージョンとハッシュを固定して取り込む。
# システムのライブラリ（apt の zlib 等）は使わない: 依存のバージョンが変わると
# 出力 PNG のバイト列が変わり、純粋関数の原則とゴールデンテストが崩れるため。
include(FetchContent)

# 版は変数に持たせて URL に差し込む。`shashoku --version` がこの値をそのまま出す
# （利用者が「どの依存でできた PNG か」を言えるようにするため。#20）。
set(SHASHOKU_ZLIB_VERSION 1.3.2)
set(SHASHOKU_FREETYPE_VERSION 2.14.3)
set(SHASHOKU_HARFBUZZ_VERSION 14.4.0)

# ---- zlib（Phase 0: PNG エンコーダの deflate）--------------------------------
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
  URL "https://github.com/madler/zlib/releases/download/v${SHASHOKU_ZLIB_VERSION}/zlib-${SHASHOKU_ZLIB_VERSION}.tar.gz"
  URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16)
FetchContent_MakeAvailable(zlib)
shashoku_mark_system(ZLIB::ZLIBSTATIC)

# ---- GoogleTest --------------------------------------------------------------
if(SHASHOKU_BUILD_TESTS)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
  FetchContent_MakeAvailable(googletest)
  foreach(_t gtest gtest_main gmock gmock_main)
    shashoku_mark_system(${_t})
  endforeach()
endif()

# ---- FreeType（Phase 2: グリフのラスタライズ）--------------------------------
# システムの zlib / bzip2 / libpng / brotli / HarfBuzz を find_package で拾わせない。
# 拾うと環境ごとにフォントの読み方（圧縮 WOFF2 や PNG 埋め込みカラー絵文字の可否）が
# 変わり、出力が非決定的になる。shashoku が使うのは輪郭のラスタライズだけなので全部不要。
# FT_REQUIRE_* も明示的に OFF にする（ユーザーのキャッシュに残っていた場合の保険）。
foreach(_dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
  set(FT_DISABLE_${_dep} ON CACHE BOOL "" FORCE)
  set(FT_REQUIRE_${_dep} OFF CACHE BOOL "" FORCE)
endforeach()
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)  # FontLoad エラーの message に使う
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
  URL "https://download.savannah.gnu.org/releases/freetype/freetype-${SHASHOKU_FREETYPE_VERSION}.tar.xz"
  URL_HASH SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f)
FetchContent_MakeAvailable(freetype)
# FreeType のビルドツリーには CMake の FindFreetype 互換の名前が無い（インストール時の
# EXPORT_NAME としてしか定義されない）ので、こちらで別名を作る。
if(NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
endif()
shashoku_mark_system(freetype)

# ---- HarfBuzz（Phase 2: シェーピング）----------------------------------------
# 公式 CMakeLists ではなくアマルガム（src/harfbuzz.cc）を自前の add_library でビルドする。
# 理由:
#   1. 公式 CMakeLists は `if (TARGET freetype)` を見て HB_HAVE_FREETYPE を勝手に ON にする。
#      shashoku は FreeType を先に取り込むので、放っておくと A7（HarfBuzz と FreeType は
#      互いを知らない）が黙って破られる
#   2. subset / raster / vector / gpu といった使わないライブラリまで既定で作る
#   3. find_package(Python3) や check_function_exists でシステムを覗く（決定性を下げる）
# 公式 CMakeLists 自身もビルド対象はアマルガム 1 本なので、ビルド時間は変わらない。
# config.h を渡さないので HarfBuzz は既定構成になる = glib / ICU / FreeType / Graphite2 の
# どれとも連携しない（hb-ft.cc などは #ifdef HAVE_FREETYPE で丸ごと消える）。
# SOURCE_SUBDIR に存在しないディレクトリを指すのは「取得はするが add_subdirectory しない」
# ための定石（CMake 3.18+）。
FetchContent_Declare(harfbuzz
  URL "https://github.com/harfbuzz/harfbuzz/releases/download/${SHASHOKU_HARFBUZZ_VERSION}/harfbuzz-${SHASHOKU_HARFBUZZ_VERSION}.tar.xz"
  URL_HASH SHA256=2357ed966c6ced7bfa720b0640c0231065af01158fbea215093ffa15aed44371
  SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(harfbuzz)

add_library(harfbuzz STATIC "${harfbuzz_SOURCE_DIR}/src/harfbuzz.cc")
add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
target_include_directories(harfbuzz SYSTEM PUBLIC "${harfbuzz_SOURCE_DIR}/src")
# 他人のコードなので警告は見ない（shashoku 側の -Werror とは無関係）。
target_compile_options(harfbuzz PRIVATE -w)
set_target_properties(harfbuzz PROPERTIES
  CXX_STANDARD 17          # HarfBuzz が想定している規格。C++23 の規則変更を踏まない
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
