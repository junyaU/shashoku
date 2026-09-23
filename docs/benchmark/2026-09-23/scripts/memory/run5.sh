#!/bin/bash
set -u
EXP="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$EXP/node/node-v20.18.1-linux-x64/bin:$PATH"
cd "$EXP/memory" || exit 1
r() { local tag="$1"; shift; echo "### $tag"; node --expose-gc trend.mjs "$@" "out_${tag}.json"; echo "exit=$?"; }
# 前回やり残し: サーバー相当（毎回イベントループに譲る）を長く
r server_long        full_yield 07 2000 100
( export MIMALLOC_PURGE_DELAY=0; r server_long_purge0 full_yield 07 2000 100 )
# K 回に 1 回だけ譲る（ハンドラが await をはさむ頻度に相当）
( export YIELD_EVERY=10;  r yieldk_10  resvg_yield_k 07 400 20 )
( export YIELD_EVERY=50;  r yieldk_50  resvg_yield_k 07 400 20 )
# フォントを毎回コピーして渡す（仮説 4）を 200 回まで
r font_freshbuf_200  satori_freshbuf 07 200 10
echo "ALL DONE 5"
