# 外部依存はすべて FetchContent でバージョンとハッシュを固定して取り込む。
# システムのライブラリ（apt の zlib 等）は使わない: 依存のバージョンが変わると
# 出力 PNG のバイト列が変わり、純粋関数の原則とゴールデンテストが崩れるため。
include(FetchContent)

# ---- zlib（Phase 0: PNG エンコーダの deflate）--------------------------------
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
  URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
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

# FreeType / HarfBuzz は Phase 2 で追加する。
