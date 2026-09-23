#!/bin/bash
set -u
EXP="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$EXP/node/node-v20.18.1-linux-x64/bin:$PATH"
cd "$EXP/memory" || exit 1
N=400; S=20
run() { # run <tag> <mode> ... ; env は呼ぶ側で
  local tag="$1" mode="$2"; shift 2
  echo "### $tag"
  node --expose-gc trend.mjs "$mode" 07 $N $S "out_${tag}.json"
  echo "exit=$?"
}
run base_noop            noop
run base_full            full
run base_satori          satori
run base_resvg           resvg
run base_resvg_nosysfont resvg_nosysfont
run base_satori_freshbuf satori_freshbuf
( export MALLOC_ARENA_MAX=1 MALLOC_TRIM_THRESHOLD_=0 MALLOC_MMAP_THRESHOLD_=131072
  run malloc_full   full
  run malloc_resvg  resvg
  run malloc_satori satori )
echo "ALL DONE"
