# shashoku 自身のターゲットに適用する共通設定。
# FetchContent で取り込む依存ライブラリには適用しない（他人のコードの警告は直せない）。
function(shashoku_target_defaults target)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(WARNING "未検証のコンパイラです: ${CMAKE_CXX_COMPILER_ID}")
    return()
  endif()

  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wold-style-cast
    -Wcast-align
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wimplicit-fallthrough
    -Wformat=2
    # 純粋関数の原則（DESIGN.md §3-5）: 同じ入力からバイト単位で同じ PNG を出す。
    # FMA 縮約は CPU / コンパイラによって浮動小数点の丸めを変えるので禁止する。
    -ffp-contract=off)

  if(SHASHOKU_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE -Werror)
  endif()

  # Debug ビルドでは標準ライブラリの境界チェックを有効化する。
  # ピクセルバッファやグリフ列の添字ミスを、未定義動作ではなく即 abort にする。
  # （使っていない側の標準ライブラリのマクロは単に無視される）
  target_compile_definitions(${target} PRIVATE
    $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>
    $<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG>)
endfunction()

# 依存ライブラリのヘッダを SYSTEM 扱いにして、こちらの警告フラグの対象外にする。
# （例: zlib の deflateInit マクロは C キャストを含み、-Wold-style-cast に引っかかる）
# CMake 3.25+ なら FetchContent_Declare(... SYSTEM) で済むが、3.22 対応のため手動で行う。
function(shashoku_mark_system target)
  get_target_property(_real ${target} ALIASED_TARGET)
  if(_real)
    set(target ${_real})
  endif()
  get_target_property(_includes ${target} INTERFACE_INCLUDE_DIRECTORIES)
  if(_includes)
    set_target_properties(${target} PROPERTIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_includes}")
  endif()
endfunction()
