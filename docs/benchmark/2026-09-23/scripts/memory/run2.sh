#!/bin/bash
set -u
EXP="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$EXP/node/node-v20.18.1-linux-x64/bin:$PATH"
cd "$EXP/memory" || exit 1
r() { local tag="$1"; shift; echo "### $tag"; node --expose-gc trend.mjs "$@" "out_${tag}.json"; echo "exit=$?"; }
# resvg のどの呼び出しが漏らすか
r iso_resvg_ctor      resvg_ctor      07 400 20
r iso_resvg_render    resvg_render    07 400 20
r iso_resvg_async     resvg_async     07 200 20
# 画素数との比例（case03 720x430 / case02 1000x345 / case07 1080x1080）
r size_resvg_c03      resvg           03 400 40
r size_resvg_c02      resvg           02 400 40
# mimalloc のつまみ
( export MIMALLOC_PURGE_DELAY=0;  r mi_purge0     resvg 07 400 40 )
( export MIMALLOC_PURGE_DECOMMITS=1 MIMALLOC_PURGE_DELAY=0; r mi_decommit resvg 07 400 40 )
( export MIMALLOC_ARENA_EAGER_COMMIT=0 MIMALLOC_PURGE_DELAY=0; r mi_noeager resvg 07 400 40 )
( export MIMALLOC_VERBOSE=1; node --expose-gc trend.mjs resvg 07 60 30 out_mi_verbose.json 2>&1 | tail -40 > mi_verbose.txt )
# フォントを毎回コピーして渡す（仮説 4）
r font_freshbuf       satori_freshbuf 07 400 5
# satori だけを長く回す（頭打ちの確認）
r long_satori         satori          07 2000 100
echo "ALL DONE 2"
