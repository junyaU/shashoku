#!/bin/bash
# usage: run.sh <case-number-2digit> <round>
W=/home/junya/src/github.com/junyaU/shashoku/.claude/worktrees/agent-a0777c179f4789b6c
CLI=$W/build/release/tools/shashoku/shashoku
EXP2=/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/897195fe-9079-401f-bd6e-233ddc55e24c/scratchpad/a46/verify2/fix
N=$1
K=$2
cd "$EXP2" || exit 99

case "$N" in
  01) OPTS=(--width 940) ;;
  02) OPTS=(--width 1040) ;;
  03) OPTS=(--width 760) ;;
  04) OPTS=(--width 840) ;;
  05) OPTS=(--width 1080 --height 1080) ;;
  06) OPTS=(--width 1200 --height 630 --image "avatar=$W/examples/icon.png") ;;
  07) OPTS=(--width 1080 --height 1080) ;;
  08) OPTS=(--width 840) ;;
  09) OPTS=(--width 840) ;;
  10) OPTS=(--width 800 --height 1200) ;;
  *) echo "unknown case"; exit 99 ;;
esac

"$CLI" "case${N}_r${K}.html" -o "case${N}.png" "${OPTS[@]}" --diagnostics json \
  > "case${N}_r${K}.json" 2> "case${N}_r${K}.err"
RC=$?
echo "exit=$RC"
echo "--- stderr ---"
cat "case${N}_r${K}.err"
echo "--- json (pretty) ---"
python3 - "case${N}_r${K}.json" <<'PY'
import json,sys,collections
p=sys.argv[1]
raw=open(p,encoding='utf-8').read()
try:
    d=json.loads(raw)
except Exception as e:
    print("JSON parse failed:",e); print(raw[:2000]); sys.exit(0)
print("ok=%s width=%s height=%s truncated=%s errors=%d warnings=%d" % (
    d.get('ok'), d.get('width'), d.get('height'), d.get('truncated'),
    len(d.get('errors',[])), len(d.get('warnings',[]))))
kinds=collections.Counter(e['kind'] for e in d.get('errors',[]))
print("error kinds:", dict(kinds))
for e in d.get('errors',[]):
    print("E %s %s:%s %s" % (e['kind'], e.get('line'), e.get('column'), e.get('message')))
    if e.get('hint'):
        print("     hint: %s" % e['hint'])
wk=collections.Counter(w['kind'] for w in d.get('warnings',[]))
print("warning kinds:", dict(wk))
for w in d.get('warnings',[])[:40]:
    print("W %s %s:%s %s edge=%s overflow=%s" % (w['kind'], w.get('line'), w.get('column'), w.get('detail'), w.get('edge'), w.get('overflow_px')))
PY
