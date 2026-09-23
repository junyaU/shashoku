#!/bin/bash
SRC=/home/junya/src/github.com/junyaU/shashoku/.claude/worktrees/agent-a0777c179f4789b6c/docs/benchmark/2026-09-23/inputs/a_plain
EXP2=/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/897195fe-9079-401f-bd6e-233ddc55e24c/scratchpad/a46/verify2/fix
for i in 01 02 03 04 05 06 07 08 09 10; do
  cp "$SRC/case$i.html" "$EXP2/case${i}_r0.html"
done
wc -l "$EXP2"/case*_r0.html
