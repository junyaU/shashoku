# encode の出力を pngcheck（外部の独立した実装）に通す。
# 圧縮レベル（A32）は 0〜9 すべてを通す: レベルは zlib のブロックの作り方を変えるので、
# 無圧縮（0）から最高圧縮（9）まで PNG として妥当であることを外部の実装に確かめさせる。
# 使い方: cmake -DWRITER=... -DPNGCHECK=... -DOUT_DIR=... -P pngcheck.cmake

file(MAKE_DIRECTORY "${OUT_DIR}")

set(samples tiny solid odd alpha noise gradient)
set(levels 0 1 2 3 4 5 6 7 8 9)
foreach(sample IN LISTS samples)
  foreach(level IN LISTS levels)
    set(png "${OUT_DIR}/${sample}-${level}.png")

    execute_process(COMMAND "${WRITER}" "${sample}" "${png}" "${level}"
      RESULT_VARIABLE write_result OUTPUT_VARIABLE write_out ERROR_VARIABLE write_err)
    if(NOT write_result EQUAL 0)
      message(FATAL_ERROR
        "png_write_sample ${sample} level ${level} が失敗しました (${write_result}): ${write_out}${write_err}")
    endif()

    execute_process(COMMAND "${PNGCHECK}" -q "${png}"
      RESULT_VARIABLE check_result OUTPUT_VARIABLE check_out ERROR_VARIABLE check_err)
    if(NOT check_result EQUAL 0)
      message(FATAL_ERROR "pngcheck が ${png} を拒否しました (${check_result}): ${check_out}${check_err}")
    endif()
  endforeach()
endforeach()

message(STATUS "pngcheck: ${samples} x レベル ${levels} すべて OK")
