# CLI のスモークテスト（ctest から cmake -P で呼ばれる）。
#   -DCLI=<実行ファイル> -DCASE=<ケース名> -DFONT_DIR=... -DSOURCE_DIR=... -DWORK_DIR=...
#
# 「正常に PNG ができる」「未対応 CSS で終了コード 1 とエラーメッセージ」
# 「--dump-stage=box が JSON を出す」の 3 つだけを見る。組版の中身は integration_test の担当。
#
# 配布（#20）で足したケース:
#   version / license   --version と --license の中身
#   default_font        --font 無しで PNG が出て、`--font <同じ OTF>` とバイト単位で一致する
#   examples            examples/*.html の先頭コメントの「そのまま貼れる 1 行」が実際に動く

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
  # 未対応と分かっているプロパティには代替案が一言つく（#20）。手がかりがゼロだと
  # 試用の最初の 1 枚で詰まる。box-sizing は「既知の制限」の筆頭。
  set(box_sizing_html "${WORK_DIR}/box_sizing.html")
  file(WRITE "${box_sizing_html}"
       "<div style=\"box-sizing: border-box; width: 200px\">あ</div>\n")
  execute_process(
    COMMAND "${CLI}" "${box_sizing_html}" --font "${font}" -o "${WORK_DIR}/never.png"
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "1" "exit code for box-sizing")
  if(NOT stderr_text MATCHES "`box-sizing` is not a supported property \\(content-box only")
    message(FATAL_ERROR "box-sizing のエラーに代替案がありません: ${stderr_text}")
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

elseif(CASE STREQUAL "version")
  # --version は試用報告にそのまま貼る欄（#20）。shashoku 自身・依存 3 つ・既定フォントを出す。
  execute_process(
    COMMAND "${CLI}" --version
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code (stderr: ${stderr_text})")
  foreach(needle "shashoku [0-9]+\\.[0-9]+\\.[0-9]+" "zlib [0-9]" "FreeType [0-9]"
                 "HarfBuzz [0-9]" "default font: Noto Sans JP")
    if(NOT stdout_text MATCHES "${needle}")
      message(FATAL_ERROR "--version に ${needle} がありません: ${stdout_text}")
    endif()
  endforeach()
  # 入力ファイルが無くても動く（引数エラーにしない）
  if(NOT stderr_text STREQUAL "")
    message(FATAL_ERROR "--version が stderr に何か書いています: ${stderr_text}")
  endif()

elseif(CASE STREQUAL "license")
  # SIL OFL 1.1 の「ライセンス文の同梱」を、バイナリ 1 つでも満たせることの検査（#20）。
  execute_process(
    COMMAND "${CLI}" --license
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code (stderr: ${stderr_text})")
  foreach(needle "MIT License" "zlib" "The FreeType Project LICENSE" "HarfBuzz"
                 "SIL OPEN FONT LICENSE Version 1.1")
    if(NOT stdout_text MATCHES "${needle}")
      message(FATAL_ERROR "--license に ${needle} がありません")
    endif()
  endforeach()

elseif(CASE STREQUAL "default_font")
  # 既定フォント（#20 / A38）。CLI 層だけの機能で、render() の署名は変わっていない。
  set(bold "${FONT_DIR}/NotoSansJP-Bold.otf")

  # (1) --font 無しで PNG が出る
  set(default_png "${WORK_DIR}/default_hello.png")
  file(REMOVE "${default_png}")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/hello.html" -o "${default_png}" --width 600
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code without --font (stderr: ${stderr_text})")
  file(READ "${default_png}" signature LIMIT 8 HEX)
  expect_equal("${signature}" "89504e470d0a1a0a" "PNG signature")

  # (2) 受け入れ条件の要: 埋め込みと `--font <同じ OTF>` がバイト単位で一致する。
  #     og_card は font-weight: 700 を含むので、Regular / Bold 2 本の選び方まで見る。
  set(explicit_png "${WORK_DIR}/explicit_hello.png")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/hello.html" --font "${font}" --font "${bold}"
            -o "${explicit_png}" --width 600
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code with --font (stderr: ${stderr_text})")
  file(READ "${default_png}" a HEX)
  file(READ "${explicit_png}" b HEX)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "既定フォントと --font <同じ OTF> の PNG が一致しません: hello.html")
  endif()

  set(default_og "${WORK_DIR}/default_og.png")
  set(explicit_og "${WORK_DIR}/explicit_og.png")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/og_card.html"
            --image "icon=${SOURCE_DIR}/examples/icon.png" -o "${default_og}"
            --width 1200 --height 630 --scale 0.5
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "og_card without --font (stderr: ${stderr_text})")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/og_card.html" --font "${font}" --font "${bold}"
            --image "icon=${SOURCE_DIR}/examples/icon.png" -o "${explicit_og}"
            --width 1200 --height 630 --scale 0.5
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "og_card with --font (stderr: ${stderr_text})")
  file(READ "${default_og}" a HEX)
  file(READ "${explicit_og}" b HEX)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "既定フォントと --font <同じ OTF> の PNG が一致しません: og_card.html")
  endif()

  # (3) --font を書いたらそちらが優先される。欧文フォントだけを渡すと和文が豆腐になる
  #     （= 既定フォントが黙って足されていない）。
  set(latin_png "${WORK_DIR}/latin_hello.png")
  execute_process(
    COMMAND "${CLI}" "${SOURCE_DIR}/examples/hello.html" --font "${FONT_DIR}/NotoSans-Regular.ttf"
            -o "${latin_png}" --width 600
    RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
  expect_equal("${status}" "0" "exit code with Latin-only font (stderr: ${stderr_text})")
  if(NOT stderr_text MATCHES "missing-glyph")
    message(FATAL_ERROR "欧文フォントだけなのに豆腐の警告がありません: ${stderr_text}")
  endif()
  file(READ "${latin_png}" c HEX)
  file(READ "${default_png}" a HEX)
  if(a STREQUAL c)
    message(FATAL_ERROR "--font が既定フォントより優先されていません")
  endif()

elseif(CASE STREQUAL "examples")
  # examples/*.html の先頭コメントに書いた「そのまま貼れる 1 行」を、**文字どおりそのまま**
  # シェルに渡して実行する（#20）。
  #
  # 以前はこの検査が引数だけを取り出し、実行ファイルは ${CLI} に差し替えていたので、
  # **コマンド名そのもの（`shashoku` か `./shashoku` か）を一度も実行していなかった**。
  # そのため「アーカイブを展開した場所で貼ると `command not found`」を素通りさせた。
  # いまは配布物と同じ形（`shashoku` と `examples/` が並ぶディレクトリ）を作り、
  # そこを作業ディレクトリにして行を丸ごと `sh -c` に渡す。
  set(stage "${WORK_DIR}/stage")
  file(REMOVE_RECURSE "${stage}")
  file(MAKE_DIRECTORY "${stage}")
  file(CREATE_LINK "${CLI}" "${stage}/shashoku" SYMBOLIC)
  file(CREATE_LINK "${SOURCE_DIR}/examples" "${stage}/examples" SYMBOLIC)

  foreach(name hello og_card ruby vertical kinsoku)
    set(path "${SOURCE_DIR}/examples/${name}.html")
    file(READ "${path}" content)
    # 配布物の README と同じ `./shashoku …` で始まること自体を検査する。
    if(NOT content MATCHES "[\r\n][ \t]*(\\./shashoku examples/[^\r\n]*)")
      message(FATAL_ERROR
        "examples/${name}.html に「そのまま貼れる 1 行」がありません"
        "（`./shashoku examples/…` で始まる行が要ります）")
    endif()
    set(line "${CMAKE_MATCH_1}")
    if(line MATCHES "--font")
      message(FATAL_ERROR "examples/${name}.html の 1 行が --font を要求しています: ${line}")
    endif()
    set(output "${WORK_DIR}/example_${name}.png")
    file(REMOVE "${output}")
    # 末尾に足した -o が勝つ（書かれているとおりの行を動かしつつ、出力先だけを移す）。
    execute_process(
      COMMAND sh -c "${line} -o '${output}'"
      WORKING_DIRECTORY "${stage}"
      RESULT_VARIABLE status ERROR_VARIABLE stderr_text)
    expect_equal("${status}" "0" "examples/${name}.html: exit code (stderr: ${stderr_text})")
    if(NOT EXISTS "${output}")
      message(FATAL_ERROR "examples/${name}.html: PNG が作られていません")
    endif()
    file(READ "${output}" signature LIMIT 8 HEX)
    expect_equal("${signature}" "89504e470d0a1a0a" "examples/${name}.html: PNG signature")
  endforeach()

else()
  message(FATAL_ERROR "知らないケースです: ${CASE}")
endif()
