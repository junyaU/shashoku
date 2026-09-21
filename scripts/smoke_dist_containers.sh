#!/usr/bin/env bash
# ビルドツールを何も入れていない素の Ubuntu に配布物を置いて、その場で PNG が書けることを見る
# （#20 の受け入れ条件）。22.04 は前提にしている glibc の下限（2.35）、24.04 は新しい側。
#
#   scripts/smoke_dist_containers.sh build/package/shashoku-linux-x86_64 [出力先]
#
# **docker が要る**ので CI 専用（このリポジトリの開発マシンには docker が無い）。
set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "使い方: $0 <展開済みの配布ディレクトリ> [出力先]" >&2
  exit 2
fi
dir=$(realpath "$1")
out=$(realpath -m "${2:-${TMPDIR:-/tmp}/shashoku-smoke}")

if ! command -v docker > /dev/null; then
  echo "error: docker がありません（このスクリプトは CI 専用です）" >&2
  exit 2
fi
if [ ! -x "$dir/shashoku" ]; then
  echo "error: $dir/shashoku がありません" >&2
  exit 2
fi
mkdir -p "$out"

for image in ubuntu:22.04 ubuntu:24.04; do
  tag="${image/:/-}"
  echo "== $image"
  # 何も apt-get せずに、置いたその場で動かす。
  docker run --rm \
    -v "$dir:/opt/shashoku:ro" -v "$out:/out" -w /opt/shashoku "$image" \
    sh -c './shashoku --version && ldd ./shashoku && ./shashoku examples/og_card.html \
             --image icon=examples/icon.png -o "/out/'"$tag"'.png" --width 1200 --height 630'
  # PNG のシグネチャ（89 50 4E 47 0D 0A 1A 0A）で始まること
  signature=$(head -c 8 "$out/$tag.png" | od -An -tx1 | tr -d ' \n')
  if [ "$signature" != "89504e470d0a1a0a" ]; then
    echo "error: $image の出力が PNG ではありません（$signature）" >&2
    exit 1
  fi
  echo "   OK: $(stat -c%s "$out/$tag.png") B"
done

echo "OK: 素の ubuntu:22.04 / 24.04 のどちらでも PNG を書けました"
