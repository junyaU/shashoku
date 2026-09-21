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

elseif(CASE STREQUAL "image_ok")
  # --image name=path の経路（A12）。OG カードはアイコンを引けないと ImageNotFound になる。
  set(output "${WORK_DIR}/og_card.png")
  file(REMOVE "${output}")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/og_card.html"
            --font "${FONT_DIR}/NotoSansJP-Bold.otf" --font "${font}"
            --image "icon=${SOURCE_DIR}/tests/data/icon.png"
            -o "${output}" --width 1200 --height 630 --scale 0.5
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code (stderr: ${stderr_text})")
  file(READ "${output}" signature LIMIT 8 HEX)
  expect_equal("${signature}" "89504e470d0a1a0a" "PNG signature")

  # 画像を渡さなければ ImageNotFound で落ちる（黙って描き飛ばさない）
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/og_card.html" --font "${font}"
            -o "${WORK_DIR}/never.png" --width 1200 --height 630
    RESULT_VARIABLE missing_status ERROR_VARIABLE missing_stderr)
  expect_equal("${missing_status}" "1" "exit code without --image")
  if(NOT missing_stderr MATCHES "image-not-found")
    message(FATAL_ERROR "stderr に image-not-found がありません: ${missing_stderr}")
  endif()

elseif(CASE STREQUAL "trim_flags")
  # 約物の空きの旗（値を取らないオプション）。天付きにすると絵が変わる。
  set(html "${WORK_DIR}/trim.html")
  file(WRITE "${html}"
       "<div style=\"font-size: 20px; width: 260px\">「天付きにすると行頭の括弧が詰まる。」</div>\n")
  execute_process(
    COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/plain.png" --width 300
    RESULT_VARIABLE plain_status ERROR_VARIABLE stderr_text)
  expect_equal("${plain_status}" "0" "exit code (stderr: ${stderr_text})")
  execute_process(
    COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/trimmed.png" --width 300
            --trim-line-start --no-trim-line-end --no-collapse-punctuation
    RESULT_VARIABLE trim_status ERROR_VARIABLE stderr_text)
  expect_equal("${trim_status}" "0" "exit code (stderr: ${stderr_text})")
  file(READ "${WORK_DIR}/plain.png" plain HEX)
  file(READ "${WORK_DIR}/trimmed.png" trimmed HEX)
  if(plain STREQUAL trimmed)
    message(FATAL_ERROR "--trim-line-start が絵に反映されていません")
  endif()

  # 値を取らないので `=` を付けたら引数エラー（終了コード 2）
  execute_process(
    COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/never.png" --trim-line-start=1
    RESULT_VARIABLE bad_status ERROR_VARIABLE bad_stderr)
  expect_equal("${bad_status}" "2" "exit code for --trim-line-start=1")

elseif(CASE STREQUAL "compression")
  # --compression（A33）。レベルはファイルの大きさだけを変え、絵は変えない。
  set(html "${WORK_DIR}/compression.html")
  file(WRITE "${html}" "<div style=\"font-size: 20px; width: 400px\">圧縮レベルの検査。</div>\n")
  foreach(level 0 9)
    execute_process(
      COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/level${level}.png" --width 420
              --compression ${level}
      RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
    expect_equal("${status}" "0" "exit code for --compression ${level} (stderr: ${stderr_text})")
  endforeach()
  file(SIZE "${WORK_DIR}/level0.png" size0)
  file(SIZE "${WORK_DIR}/level9.png" size9)
  if(NOT size0 GREATER size9)
    message(FATAL_ERROR "--compression 0 (${size0} B) が 9 (${size9} B) より大きくありません")
  endif()

  # 範囲外は render() が InvalidOption で落とす（終了コード 1）
  execute_process(
    COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/never.png" --compression 10
    RESULT_VARIABLE bad_status ERROR_VARIABLE bad_stderr)
  expect_equal("${bad_status}" "1" "exit code for --compression 10")
  if(NOT bad_stderr MATCHES "invalid-option")
    message(FATAL_ERROR "stderr に invalid-option がありません: ${bad_stderr}")
  endif()

  # 数値でなければ引数エラー（終了コード 2）
  execute_process(
    COMMAND "${CLI}" "${html}" --font "${font}" -o "${WORK_DIR}/never.png" --compression fast
    RESULT_VARIABLE junk_status ERROR_VARIABLE junk_stderr)
  expect_equal("${junk_status}" "2" "exit code for --compression fast")

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
