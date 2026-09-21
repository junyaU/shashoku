#!/usr/bin/env bash
# 配布する実行ファイルが「素の Linux に置いたその場で動く」形になっているかを検査する（#20。A-new-2）。
#
#   scripts/check_dist_binary.sh build/dist/tools/shashoku/shashoku
#
# 見るのは 2 つ:
#   1. 動的依存（NEEDED）が glibc（libc / libm）と ld-linux だけであること。
#      libc++ / libc++abi / libunwind / libgcc_s が残っていたら、素の Ubuntu では起動できない
#   2. 要求する glibc のシンボル版の最大が SHASHOKU_MAX_GLIBC（既定 2.35 = Ubuntu 22.04）以下
#      であること。新しい glibc のマシンでビルドすると、ここだけが静かに上がって
#      「古いディストリで GLIBC_2.39 not found」になる
#
# 終了コード: 0 = 合格 / 1 = 不合格。ローカルでもそのまま回せる（docker は要らない）。
set -euo pipefail

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

echo "== $bin"
if command -v file > /dev/null; then
  file "$bin"
fi

# ---- 1. 動的依存 -------------------------------------------------------------
needed=$("$readelf" -d -W "$bin" | sed -n 's/.*NEEDED.*\[\(.*\)\].*/\1/p' || true)
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
versions=$("$readelf" --dyn-syms -W "$bin" \
  | sed -n 's/.*@@*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -u || true)
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
