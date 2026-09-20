# ⑥ png のテスト。zlib は「別実装の CRC32 / 圧縮」としてテスト側でも使う
# （自前の crc32 と突き合わせ、エンコーダが作れない PNG を組み立てるため）。
shashoku_add_test(png_test
  SOURCES
    png_test_util.cpp
    encoder_test.cpp
    decoder_test.cpp
    round_trip_test.cpp
    determinism_test.cpp
  LIBS shashoku::png ZLIB::ZLIBSTATIC)

# encode の出力を外部の実装に検査させる。pngcheck が無い環境ではテストごと省く。
add_executable(png_write_sample write_sample.cpp)
target_link_libraries(png_write_sample PRIVATE shashoku::png)
shashoku_target_defaults(png_write_sample)

find_program(SHASHOKU_PNGCHECK pngcheck)
if(SHASHOKU_PNGCHECK)
  add_test(NAME Png.Pngcheck
    COMMAND ${CMAKE_COMMAND}
      -DWRITER=$<TARGET_FILE:png_write_sample>
      -DPNGCHECK=${SHASHOKU_PNGCHECK}
      -DOUT_DIR=${CMAKE_BINARY_DIR}/test_output/pngcheck
      -P ${CMAKE_CURRENT_SOURCE_DIR}/pngcheck.cmake)
else()
  message(STATUS "pngcheck が見つかりません: Png.Pngcheck を省きます")
endif()
