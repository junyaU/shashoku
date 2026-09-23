#!/usr/bin/env bash
# 完全な HTML 文書を Windows 側の headless Chrome で撮る（参照画像用。ローカル専用）。
# 使い方: chrome_shot.sh <in.html> <width> <height> <out.png>
# 注意: WSL からは幅 500px 未満のウィンドウを作れない。使い捨てプロファイルを使い、終わったら消す。
set -uo pipefail
IN="$(realpath "$1")"; W="$2"; H="$3"; OUT="$(realpath -m "$4")"
CHROME='/mnt/c/Program Files/Google/Chrome/Application/chrome.exe'
PROF="$(dirname "$OUT")/chrome-profile-$$"; mkdir -p "$PROF"
winp() { wslpath -w "$1"; }
url="file:$(winp "$IN" | sed 's#\\#/#g')"; case "$url" in file:////*) ;; file:\\\\*) ;; esac
[[ "$(winp "$IN")" == \\\\* ]] || url="file:///$(winp "$IN" | sed 's#\\#/#g')"
"$CHROME" --headless --disable-gpu --no-sandbox --hide-scrollbars --force-device-scale-factor=1 \
  --force-color-profile=srgb --window-size="$W,$H" --user-data-dir="$(winp "$PROF")" \
  --virtual-time-budget=10000 --screenshot="$(winp "$OUT")" "$url" > "$OUT.chrome.log" 2>&1
rc=$?
rm -rf "$PROF"
if [[ -s "$OUT" ]]; then echo "ok $OUT"; exit 0; else echo "failed rc=$rc (see $OUT.chrome.log)"; exit 1; fi
