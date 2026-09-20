# CLI のスモークテスト（ctest から cmake -P で呼ばれる）。
#   -DCLI=<実行ファイル> -DCASE=<ケース名> -DFONT_DIR=... -DSOURCE_DIR=... -DWORK_DIR=...
#
# 「正常に PNG ができる」「未対応 CSS で終了コード 1 とエラーメッセージ」
# 「--dump-stage=box が JSON を出す」の 3 つだけを見る。組版の中身は integration_test の担当。

function(expect_equal actual expected what)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${what}: expected [${expected}] but got [${actual}]")
  endif()
endfunction()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(font "${FONT_DIR}/NotoSansJP-Regular.otf")

if(CASE STREQUAL "png_ok")
  set(output "${WORK_DIR}/hello.png")
  file(REMOVE "${output}")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/hello.html" --font "${font}" -o "${output}"
            --width 600
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code (stderr: ${stderr_text})")
  if(NOT EXISTS "${output}")
    message(FATAL_ERROR "PNG が作られていません: ${output}")
  endif()
  # PNG のシグネチャ（89 50 4E 47 0D 0A 1A 0A）で始まること
  file(READ "${output}" signature LIMIT 8 HEX)
  expect_equal("${signature}" "89504e470d0a1a0a" "PNG signature")

elseif(CASE STREQUAL "unsupported_css")
  set(html "${WORK_DIR}/unsupported.html")
  file(WRITE "${html}" "<div style=\"float: left\">あ</div>\n")
  execute_process(
    COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/never.png"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "1" "exit code")
  if(NOT stderr_text MATCHES "unsupported-property")
    message(FATAL_ERROR "stderr に unsupported-property がありません: ${stderr_text}")
  endif()
  if(NOT stderr_text MATCHES "float")
    message(FATAL_ERROR "stderr に原因のプロパティ名がありません: ${stderr_text}")
  endif()

elseif(CASE STREQUAL "dump_box")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/hello.html" --font "${font}" --dump-stage box
            --width 600
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code (stderr: ${stderr_text})")
  string(SUBSTRING "${stdout_text}" 0 1 first_char)
  expect_equal("${first_char}" "{" "dump の 1 文字目")
  if(NOT stdout_text MATCHES "\"root\"")
    message(FATAL_ERROR "ボックスツリーの JSON に見えません: ${stdout_text}")
  endif()

else()
  message(FATAL_ERROR "知らないケースです: ${CASE}")
endif()
