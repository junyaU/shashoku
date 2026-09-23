#!/usr/bin/env bash
# check_dist_binary.sh 自身のテスト（#31）。
#
#   scripts/check_dist_binary_test.sh <合格するはずの配布バイナリ> <落ちるはずのバイナリ>
#     例: scripts/check_dist_binary_test.sh build/dist/tools/shashoku/shashoku \
#           build/dist/tests/core/core_test
#
# **なぜ検査を検査するか**: #31 で、ELF ですらないテキストファイルが合格していた
# （`readelf` の失敗を `|| true` で消していたため、解析できなかった結果が
# 「動的依存も glibc のシンボル版も無い正常なバイナリ」に化けていた）。
# 検査が壊れていると「OK」は何の保証にもならないので、落ちるべきものが落ちることを
# ここで見る。見るのは終了コードとメッセージだけ。
#
# 第 2 引数には **dist ではないビルドの実行ファイル** を渡す。配布物の静的リンク
# （-static-libstdc++）は CLI にだけ効くので、同じ dist ビルドのテスト実行ファイル
# （例 build/dist/tests/core/core_test）は release プリセットの CLI と同じく libc++ を
# 動的に引く。つまり「検査に落ちてほしいバイナリ」として使える。
#
# 終了コード: 0 = 全件期待どおり / 1 = 期待と違うものがある / 2 = 使い方の誤り。
# docker は要らない（readelf があれば動く）。CI では dist ジョブで走る。
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "使い方: $0 <合格するはずの配布バイナリ> <落ちるはずのバイナリ（libc++ に動的依存）>" >&2
  exit 2
fi
pass_bin="$1"
fail_bin="$2"

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
check="$here/check_dist_binary.sh"
if [ ! -x "$check" ]; then
  echo "error: $check がありません" >&2
  exit 2
fi
for required in "$pass_bin" "$fail_bin"; do
  if [ ! -f "$required" ]; then
    echo "error: テストに使うバイナリがありません: $required" >&2
    echo "  先に dist プリセットをビルドしてください（cmake --build --preset dist）" >&2
    exit 2
  fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# ---- 検査に掛ける材料 --------------------------------------------------------
# #31 の再現そのもの（実行権の無いテキストファイル）
printf 'this is not an executable\n' > "$tmp/text"
# 実行権があっても ELF でなければ落ちること（スクリプトを渡した場合など）
printf '#!/bin/sh\necho hello\n' > "$tmp/script"
chmod 755 "$tmp/script"
: > "$tmp/empty"
chmod 755 "$tmp/empty"
# ELF ヘッダだけで切れたファイル。readelf は **終了コード 0 のまま** stderr に
# Error を書くので、終了コードだけを見ていると壊れた ELF を素通りさせる。
head -c 64 "$pass_bin" > "$tmp/truncated"
chmod 755 "$tmp/truncated"
# ELF ではあるが実行できないもの（.o や .so を渡した場合に相当する）
cp "$pass_bin" "$tmp/not_executable"
chmod 644 "$tmp/not_executable"

# ---- 判定 --------------------------------------------------------------------
cases=0
failures=0

# expect_fail <説明> <出力に含まれるべき語> <コマンド...>
expect_fail() {
  local what="$1" needle="$2"
  shift 2
  local out status=0
  out=$("$@" 2>&1) || status=$?
  cases=$((cases + 1))
  if [ "$status" -eq 0 ]; then
    echo "NG: $what: 落ちるはずが合格しました"
    printf '%s\n' "$out" | sed 's/^/    /'
    failures=$((failures + 1))
    return 0
  fi
  if ! printf '%s\n' "$out" | grep -q -- "$needle"; then
    echo "NG: $what: 終了コード $status で落ちましたが、メッセージに「$needle」がありません"
    printf '%s\n' "$out" | sed 's/^/    /'
    failures=$((failures + 1))
    return 0
  fi
  echo "ok: $what（終了コード $status）"
}

# expect_pass <説明> <出力に含まれるべき語> <コマンド...>
expect_pass() {
  local what="$1" needle="$2"
  shift 2
  local out status=0
  out=$("$@" 2>&1) || status=$?
  cases=$((cases + 1))
  if [ "$status" -ne 0 ]; then
    echo "NG: $what: 合格するはずが終了コード $status で落ちました"
    printf '%s\n' "$out" | sed 's/^/    /'
    failures=$((failures + 1))
    return 0
  fi
  if ! printf '%s\n' "$out" | grep -q -- "$needle"; then
    echo "NG: $what: 合格しましたが、出力に「$needle」がありません"
    printf '%s\n' "$out" | sed 's/^/    /'
    failures=$((failures + 1))
    return 0
  fi
  echo "ok: $what"
}

echo "== $check を検査する"
expect_fail "存在しないパス" "ありません" "$check" "$tmp/no_such_file"
expect_fail "テキストファイル（#31 の再現）" "ELF" "$check" "$tmp/text"
expect_fail "実行権のあるシェルスクリプト" "ELF" "$check" "$tmp/script"
expect_fail "空ファイル" "ELF" "$check" "$tmp/empty"
expect_fail "ELF ヘッダだけで切れたファイル" "readelf" "$check" "$tmp/truncated"
expect_fail "ELF だが実行権が無い" "実行" "$check" "$tmp/not_executable"
expect_fail "libc++ に動的依存するバイナリ" "動的依存" "$check" "$fail_bin"
expect_pass "配布バイナリ" "OK:" "$check" "$pass_bin"
# 上限判定が生きていること（#31 では変えていない）。2.0 まで下げれば配布バイナリでも落ちる。
expect_fail "SHASHOKU_MAX_GLIBC を下げる" "GLIBC_" \
  env SHASHOKU_MAX_GLIBC=2.0 "$check" "$pass_bin"

if [ "$failures" -ne 0 ]; then
  echo "error: $cases 件中 $failures 件が期待どおりではありません" >&2
  exit 1
fi
echo "OK: $cases 件すべて期待どおりでした"
