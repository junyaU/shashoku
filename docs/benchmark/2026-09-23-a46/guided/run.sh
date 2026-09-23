#!/bin/bash
# 使い方: bash run.sh <round> [case ...]
# 各ケースを 1 回描いて、診断 JSON と stderr と終了コードを残す。
set -u
ROUND="$1"
shift || true
CLI=/home/junya/src/github.com/junyaU/shashoku/.claude/worktrees/agent-a218d36f6690e8324/build/release/tools/shashoku/shashoku
ICON=/home/junya/src/github.com/junyaU/shashoku/.claude/worktrees/agent-a218d36f6690e8324/examples/icon.png
DIR=/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/897195fe-9079-401f-bd6e-233ddc55e24c/scratchpad/a46/verify2/guided
cd "$DIR"

opts_for() {
  case "$1" in
    case01) echo "--width 900" ;;
    case02) echo "--width 1000" ;;
    case03) echo "--width 720" ;;
    case04) echo "--width 800" ;;
    case05) echo "--width 1080 --height 1080" ;;
    case06) echo "--width 1200 --height 630 --image avatar=$ICON" ;;
    case07) echo "--width 1080 --height 1080" ;;
    case08) echo "--width 800" ;;
    case09) echo "--width 800" ;;
    case10) echo "--width 800 --height 1200" ;;
    en01)   echo "--width 1200 --height 630 --image avatar=$ICON" ;;
    en02)   echo "--width 900" ;;
    en03)   echo "--width 1000" ;;
    en04)   echo "--width 1080 --height 1080" ;;
    en05)   echo "--width 800" ;;
  esac
}

CASES="$*"
if [ -z "$CASES" ]; then
  CASES="case01 case02 case03 case04 case05 case06 case07 case08 case09 case10 en01 en02 en03 en04 en05"
fi

for c in $CASES; do
  o=$(opts_for "$c")
  "$CLI" "$c.html" -o "$c.png" $o --strict --diagnostics json > "$c.r$ROUND.json" 2> "$c.r$ROUND.err"
  code=$?
  echo "$c exit=$code"
  echo "$code" > "$c.r$ROUND.exit"
done
