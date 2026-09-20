# テスト用フォントは configure 時にハッシュ固定でダウンロードする（cmake/TestAssets.cmake）。
include(TestAssets)

shashoku_add_test(text_test
  SOURCES
    font_store_test.cpp
    shaper_test.cpp
    fallback_test.cpp
    cluster_test.cpp
    vertical_test.cpp
    glyph_source_test.cpp
    debug_output_test.cpp
  LIBS shashoku::text)

shashoku_use_test_fonts(text_test)
