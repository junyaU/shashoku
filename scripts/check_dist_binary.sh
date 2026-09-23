#!/usr/bin/env bash
# 配布する実行ファイルが「素の Linux に置いたその場で動く」形になっているかを検査する（#20。A39）。
#
#   scripts/check_dist_binary.sh build/dist/tools/shashoku/shashoku
#
# 見るのは 3 つ:
#   0. 渡されたものが ELF の実行ファイルであること（#31。A-new）。readelf が解析できない
#      ファイル（テキスト・空ファイル・壊れた ELF）を「動的依存も glibc のシンボル版も
#      無い正常なバイナリ」と取り違えないための前提
#   1. 動的依存（NEEDED）が glibc（libc / libm）と ld-linux だけであること。
#      libc++ / libc++abi / libunwind / libgcc_s が残っていたら、素の Ubuntu では起動できない
#   2. 要求する glibc のシンボル版の最大が SHASHOKU_MAX_GLIBC（既定 2.35 = Ubuntu 22.04）以下
#      であること。新しい glibc のマシンでビルドすると、ここだけが静かに上がって
#      「古いディストリで GLIBC_2.39 not found」になる
#
# この検査そのもののテストは scripts/check_dist_binary_test.sh（CI の dist ジョブで走る）。
# 終了コード: 0 = 合格 / 1 = 不合格 / 2 = 使い方の誤り。ローカルでもそのまま回せる（docker は要らない）。
set -euo pipefail

# readelf の見出しもエラーも言語で変わる（ja_JP のマシンでは日本語になる）。
# 解析する以上、出力は言語に依存させない。
export LC_ALL=C

if [ $# -ne 1 ]; then
  echo "使い方: $0 <実行ファイル>" >&2
  exit 2
fi
bin="$1"
max_glibc="${SHASHOKU_MAX_GLIBC:-2.35}"

if [ ! -f "$bin" ]; then
  echo "error: 実行ファイルがありません: $bin" >&2
  exit 2
fi

readelf=$(command -v readelf || true)
if [ -z "$readelf" ]; then
  echo "error: readelf が要ります（binutils）" >&2
  exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# readelf を走らせて標準出力を readelf_out に入れる。失敗したら**その場で**落とす。
#
# #31: 以前はここを `|| true` で終えていたので、解析の失敗が「NEEDED も GLIBC も無い」に
# 化けて、ELF ですらないテキストファイルが合格していた。終了コードだけでは足りない:
# ELF ヘッダだけで切れたファイルでは readelf は**終了コード 0 のまま** stderr に
# Error を書く（実測）。両方を見る。
#
# `| sed` を挟まないのは #22（SIGPIPE）と同じ理由ではなく、stderr と終了コードを
# 受け取るため。読み手（sed / sort）は入力を最後まで読むので SIGPIPE は起きない。
readelf_out=""
run_readelf() {
  local what="$1"
  shift
  local status=0
  readelf_out=$("$readelf" "$@" 2> "$tmp/readelf.err") || status=$?
  if [ "$status" -eq 0 ] && [ ! -s "$tmp/readelf.err" ]; then
    return 0
  fi
  {
    echo "error: $what: $bin"
    echo "  readelf $* が失敗しました（終了コード $status）:"
    sed 's/^/    /' "$tmp/readelf.err"
  } >&2
  exit 1
}

echo "== $bin"
if command -v file > /dev/null; then
  file "$bin"
fi

# ---- 0. ELF の実行ファイルであること（#31） ---------------------------------
run_readelf "ELF ではありません（readelf がヘッダを読めませんでした）" -h -W "$bin"
# ELF ではあっても、実行できない形（.o / .a / .so や、実行権を落としたファイル）は
# 配布物になりえない。NEEDED が空であることを「完全静的リンク」と読む前に弾く。
if [ ! -x "$bin" ]; then
  echo "error: 実行権がありません（配布する実行ファイルを渡してください）: $bin" >&2
  exit 1
fi

# ---- 1. 動的依存 -------------------------------------------------------------
run_readelf "動的依存（NEEDED）を読めませんでした" -d -W "$bin"
needed=$(printf '%s\n' "$readelf_out" | sed -n 's/.*NEEDED.*\[\(.*\)\].*/\1/p')
echo "-- NEEDED"
if [ -z "$needed" ]; then
  echo "   (なし: 完全静的リンク)"
else
  echo "$needed" | sed 's/^/   /'
fi
unexpected=$(printf '%s\n' "$needed" \
  | grep -v -E '^(libc\.so\.6|libm\.so\.6|ld-linux-x86-64\.so\.2|)$' || true)
if [ -n "$unexpected" ]; then
  echo "error: glibc 以外の動的依存が残っています:" >&2
  printf '%s\n' "$unexpected" | sed 's/^/  /' >&2
  exit 1
fi

# ---- 2. 要求する glibc の版 --------------------------------------------------
# 動的シンボルの版（例 "foo@GLIBC_2.34"）を集めて最大を取る。GLIBC_PRIVATE は版ではない。
run_readelf "動的シンボルを読めませんでした" --dyn-syms -W "$bin"
versions=$(printf '%s\n' "$readelf_out" \
  | sed -n 's/.*@@*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -u)
echo "-- 要求する glibc のシンボル版"
if [ -z "$versions" ]; then
  echo "   (なし)"
  echo "OK: glibc のシンボル版を要求していません（上限 $max_glibc）"
  exit 0
fi
printf '%s\n' "$versions" | sed 's/^/   GLIBC_/'

# 2.4 < 2.35 を正しく比べるため、major / minor を整数で見る（文字列比較にしない）。
to_number() {
  local major minor
  major=${1%%.*}
  minor=${1#*.}
  [ "$minor" = "$1" ] && minor=0
  minor=${minor%%.*}
  echo $((major * 1000 + minor))
}
max_allowed=$(to_number "$max_glibc")
worst=""
worst_number=0
for v in $versions; do
  n=$(to_number "$v")
  if [ "$n" -gt "$worst_number" ]; then
    worst_number=$n
    worst=$v
  fi
done

echo "-- 最大: GLIBC_$worst（上限 GLIBC_$max_glibc）"
if [ "$worst_number" -gt "$max_allowed" ]; then
  echo "error: GLIBC_$worst を要求しています（上限 GLIBC_$max_glibc）。" >&2
  echo "  もっと古い glibc の環境でビルドしてください（配布物は ubuntu:22.04 の中で作ります）。" >&2
  exit 1
fi

echo "OK: NEEDED は glibc だけ、要求する glibc は GLIBC_$worst（上限 GLIBC_$max_glibc）以下"
