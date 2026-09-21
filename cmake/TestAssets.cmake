# フォントはリポジトリに置かず、configure 時にコミット SHA と SHA256 を固定して
# ダウンロードする（ARCHITECTURE.md §3.5）。フォントが変わるとゴールデンが崩れるので、
# 依存ライブラリと同じく「版を固定して取りに行く」形にそろえる。
#
# ここで取るフォントは 2 つの役目を兼ねる（A38）:
#   1. テスト用アセット。tests/<module>/CMakeLists.txt で include(TestAssets) したあと
#        shashoku_use_test_fonts(<target>)
#      を呼ぶと、テストに SHASHOKU_TEST_FONT_DIR がコンパイル定義で渡る
#   2. CLI に埋め込む既定フォント（#20）。Noto Sans JP の Regular / Bold を
#      tools/shashoku が .incbin でバイナリに焼き込む。**CLI 層だけの機能**で、
#      ライブラリ（render() / dump()）は今までどおりバイト列しか受け取らない
#
# 和文 2 本は SHASHOKU_BUILD_TESTS=OFF でも取得する（既定フォントに要るため）。
# 欧文 1 本はフォールバックのテスト専用なので、テストを作るときだけ取る。
#
# ライセンス: いずれも SIL Open Font License 1.1（THIRD_PARTY_LICENSES に全文）。
include_guard(GLOBAL)

# 保存先。既定はビルドディレクトリの中。CI では全プリセット共通のディレクトリを指して
# actions/cache に載せる（-DSHASHOKU_TEST_ASSETS_DIR=...）。
set(SHASHOKU_TEST_ASSETS_DIR "${CMAKE_BINARY_DIR}/test_assets"
    CACHE PATH "テスト用アセット（フォント）の保存先")

# include_guard(GLOBAL) の下では 2 回目以降の include が何もしないので、
# ディレクトリスコープではなくキャッシュに置いて、どこから include しても見えるようにする。
set(SHASHOKU_TEST_FONT_DIR "${SHASHOKU_TEST_ASSETS_DIR}/fonts"
    CACHE INTERNAL "テスト用フォントの保存先")

# 既に同じ SHA256 のファイルがあればダウンロードしない（file(DOWNLOAD) の仕様）。
function(shashoku_fetch_test_asset file_name url sha256)
  set(destination "${SHASHOKU_TEST_FONT_DIR}/${file_name}")
  file(DOWNLOAD "${url}" "${destination}"
    EXPECTED_HASH SHA256=${sha256}
    TLS_VERIFY ON
    STATUS download_status)
  list(GET download_status 0 status_code)
  if(NOT status_code EQUAL 0)
    list(GET download_status 1 status_message)
    file(REMOVE "${destination}")
    message(FATAL_ERROR
      "テスト用フォントを取得できませんでした: ${url}\n  ${status_message}")
  endif()
endfunction()

# 和文（notofonts/noto-cjk の f8d1575 時点のサブセット OTF）。CLI の既定フォントでもある。
set(SHASHOKU_NOTO_CJK_COMMIT f8d157532fbfaeda587e826d4cd5b21a49186f7c)
set(_noto_cjk_raw
    "https://raw.githubusercontent.com/notofonts/noto-cjk/${SHASHOKU_NOTO_CJK_COMMIT}")
shashoku_fetch_test_asset(NotoSansJP-Regular.otf
  "${_noto_cjk_raw}/Sans/SubsetOTF/JP/NotoSansJP-Regular.otf"
  dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073)
shashoku_fetch_test_asset(NotoSansJP-Bold.otf
  "${_noto_cjk_raw}/Sans/SubsetOTF/JP/NotoSansJP-Bold.otf"
  1b0edfb500b73a4fa8a4fcaae1bbbd403994e08e73e3e0da37e70d3853f42c5f)

# 欧文（フォールバックのテスト用。和文グリフを持たないこと自体が条件）
if(SHASHOKU_BUILD_TESTS)
  shashoku_fetch_test_asset(NotoSans-Regular.ttf
    "https://raw.githubusercontent.com/notofonts/notofonts.github.io/17b05225674b0c2923d1c756e35f3476772a57dd/fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf"
    f3961a9cde016d41a4879aecda1474d3a36d6bf54fa0e4643de029cc2248b0e8)
endif()

# ---- CLI の既定フォント（#20）------------------------------------------------
# 埋め込む実体のパスと、`shashoku --version` が出す版の文字列。版はコミット SHA の
# 短縮形で示す（「同じ HTML でもフォントの版が変われば PNG は変わる」ため。A38）。
string(SUBSTRING "${SHASHOKU_NOTO_CJK_COMMIT}" 0 7 _noto_cjk_short)
set(SHASHOKU_DEFAULT_FONT_REGULAR "${SHASHOKU_TEST_FONT_DIR}/NotoSansJP-Regular.otf"
    CACHE INTERNAL "CLI に埋め込む既定フォント（Regular）")
set(SHASHOKU_DEFAULT_FONT_BOLD "${SHASHOKU_TEST_FONT_DIR}/NotoSansJP-Bold.otf"
    CACHE INTERNAL "CLI に埋め込む既定フォント（Bold）")
set(SHASHOKU_DEFAULT_FONT_VERSION
    "Noto Sans JP Regular / Bold (notofonts/noto-cjk ${_noto_cjk_short})"
    CACHE INTERNAL "既定フォントの版（--version が出す）")

function(shashoku_use_test_fonts target)
  target_compile_definitions(${target} PRIVATE
    SHASHOKU_TEST_FONT_DIR="${SHASHOKU_TEST_FONT_DIR}")
endfunction()
