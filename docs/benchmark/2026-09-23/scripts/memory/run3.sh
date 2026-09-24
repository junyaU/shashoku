#!/bin/bash
set -u
EXP="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$EXP/node/node-v20.18.1-linux-x64/bin:$PATH"
cd "$EXP/memory" || exit 1
r() { local tag="$1"; shift; echo "### $tag"; node --expose-gc trend.mjs "$@" "out_${tag}.json"; echo "exit=$?"; }
r yield_resvg         resvg_yield          07 400 20
r yield_full          full_yield           07 400 20
r min_resvg           resvg_minimal        07 400 20
r min_resvg_yield     resvg_minimal_yield  07 400 20
echo "ALL DONE 3"
# --- 追加: 修正案の確認 ---
( export MIMALLOC_PURGE_DELAY=0; r fix_full_purge0  full 07 400 20 )
( export MIMALLOC_PURGE_DELAY=0; r fix_full_long    full 07 1000 100 )
r base_full_long      full 07 1000 100
# フォントを毎回コピー（落ちる回数を見る。sample を細かく）
r font_freshbuf_small satori_freshbuf 07 60 5
echo "ALL DONE 3b"
