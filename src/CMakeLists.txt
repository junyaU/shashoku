# モジュールは依存の向き（上から下）に並べる。依存表は docs/ARCHITECTURE.md を参照。
# モジュール一覧（SHASHOKU_MODULES）は cmake/Modules.cmake にある。
# 各モジュールのターゲット定義は src/<module>/CMakeLists.txt が持つ（このファイルは触らなくてよい）。
foreach(module IN LISTS SHASHOKU_MODULES)
  # 未着手のモジュールはディレクトリごと存在しない（空ディレクトリを先に作らない方針）
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt")
    add_subdirectory(${module})
  endif()
endforeach()
