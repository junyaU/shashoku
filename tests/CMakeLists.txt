include(GoogleTest)

# 環境の配線確認（製品コードのテストではない）
shashoku_add_test(toolchain_test
  SOURCES toolchain_test.cpp
  LIBS shashoku ZLIB::ZLIBSTATIC)

# モジュールごとのテストは tests/<module>/CMakeLists.txt が持つ（このファイルは触らなくてよい）。
# support はテスト共通のヘルパー（ゴールデン比較など）。
foreach(module IN LISTS SHASHOKU_MODULES ITEMS support integration)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt")
    add_subdirectory(${module})
  endif()
endforeach()
