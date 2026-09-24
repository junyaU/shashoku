#!/bin/bash
set -u
EXP="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$EXP/node/node-v20.18.1-linux-x64/bin:$PATH"
cd "$EXP/memory" || exit 1
r() { local tag="$1"; shift; echo "### $tag"; node --expose-gc trend.mjs "$@" "out_${tag}.json"; echo "exit=$?"; }
r iso_gc_each      resvg_gc_each 07 400 20     # 譲らず毎回 gc()
r server_long      full_yield    07 2000 100   # サーバー相当（毎回イベントループを回す）を長く
echo "ALL DONE 4"
r font_freshbuf_200  satori_freshbuf 07 200 10
echo DONE4b
