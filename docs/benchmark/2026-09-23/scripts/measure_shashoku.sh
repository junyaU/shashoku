#!/usr/bin/env bash
# shashoku CLI の cold 実行コストを測る。
# 使い方: measure_shashoku.sh <results.json のあるディレクトリ> <出力 json>
# results.json の各要素の html / cli_args を使い、ケースごとに 3 回走らせて wall clock の中央値と最大 RSS を記録する。
set -uo pipefail
DIR="$1"; OUT="$2"
BIN=/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/94c9fae6-a9a0-40c9-ac43-94fa14abccb2/scratchpad/exp/bin/shashoku
python3 - "$DIR" "$OUT" "$BIN" <<'PY'
import json, subprocess, sys, os, re, statistics, shlex, tempfile
d, out, binp = sys.argv[1:4]
res = json.load(open(os.path.join(d, "results.json")))
rows = []
for r in res:
    if r.get("final") != "pass" or not r.get("html"):
        rows.append({"case": r["case"], "skipped": r.get("final")}); continue
    html = os.path.join(d, r["html"])
    args = shlex.split(r.get("cli_args", ""))
    walls, rss = [], []
    for i in range(3):
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tf: png = tf.name
        cmd = ["/usr/bin/time", "-v", binp, html, "-o", png] + args
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=d)
        m = re.search(r"Elapsed \(wall clock\).*: (?:(\d+):)?(\d+):([\d.]+)", p.stderr)
        h, mnt, sec = m.groups(); wall = (int(h or 0)*3600 + int(mnt)*60 + float(sec))*1000
        rs = int(re.search(r"Maximum resident set size \(kbytes\): (\d+)", p.stderr).group(1))
        walls.append(wall); rss.append(rs)
        size = os.path.getsize(png) if p.returncode == 0 else None
        os.unlink(png)
    rows.append({"case": r["case"], "exit": p.returncode, "wall_ms_median": round(statistics.median(walls), 1),
                 "wall_ms_all": [round(w,1) for w in walls], "max_rss_kb": max(rss), "png_bytes": size, "args": " ".join(args)})
json.dump(rows, open(out, "w"), ensure_ascii=False, indent=1)
for row in rows: print(row)
PY
