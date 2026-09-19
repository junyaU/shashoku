# encode の出力を pngcheck（外部の独立した実装）に通す。
# 使い方: cmake -DWRITER=... -DPNGCHECK=... -DOUT_DIR=... -P pngcheck.cmake

file(MAKE_DIRECTORY "${OUT_DIR}")

set(samples tiny solid odd alpha noise gradient)
foreach(sample IN LISTS samples)
  set(png "${OUT_DIR}/${sample}.png")

  execute_process(COMMAND "${WRITER}" "${sample}" "${png}"
    RESULT_VARIABLE write_result OUTPUT_VARIABLE write_out ERROR_VARIABLE write_err)
  if(NOT write_result EQUAL 0)
    message(FATAL_ERROR "png_write_sample ${sample} が失敗しました (${write_result}): ${write_out}${write_err}")
  endif()

  execute_process(COMMAND "${PNGCHECK}" -q "${png}"
    RESULT_VARIABLE check_result OUTPUT_VARIABLE check_out ERROR_VARIABLE check_err)
  if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "pngcheck が ${png} を拒否しました (${check_result}): ${check_out}${check_err}")
  endif()
endforeach()

message(STATUS "pngcheck: ${samples} すべて OK")
