# 公開 API の検査（ARCHITECTURE.md §3.10）。
#
# 肝は public_header_check: **include パスを include/ だけに絞った**ターゲットにする。
# shashoku::shashoku の PUBLIC な include ディレクトリは include/ だけで、各段のモジュールは
# PRIVATE にリンクされているので src/ は伝播しない。公開ヘッダが内部ヘッダや FreeType /
# HarfBuzz を引いた瞬間にこのターゲットがコンパイルエラーになる。
# （shashoku_add_test() は src/ と tests/ を include パスに足してしまうので使わない）
add_library(shashoku_public_header_check STATIC public_header_check.cpp)
target_link_libraries(shashoku_public_header_check PRIVATE shashoku::shashoku)
target_compile_features(shashoku_public_header_check PRIVATE cxx_std_23)
shashoku_target_defaults(shashoku_public_header_check)

shashoku_add_test(api_test
  SOURCES api_test.cpp
  LIBS shashoku::shashoku shashoku_public_header_check)
target_compile_definitions(api_test PRIVATE
  SHASHOKU_PUBLIC_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include/shashoku")
