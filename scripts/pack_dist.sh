#!/usr/bin/env bash
# 配布物の tar.gz を組み立てる（#20）。リポジトリのルートで実行する。
#
#   scripts/pack_dist.sh build/dist/tools/shashoku/shashoku build/package
#
# 入れるもの: 実行ファイル（strip 済み）/ examples/ / 試用版 README / LICENSE /
# THIRD_PARTY_LICENSES。同じコミットからは**同じバイト列**の tar.gz が出るように、
# 所有者・並び順・時刻・gzip のタイムスタンプを固定する（shashoku 自身の決定性と同じ考え方）。
#
# 終了コード: 0 = 成功。ローカルでもそのまま回せる（docker は要らない）。
set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
  echo "使い方: $0 <実行ファイル> <出力ディレクトリ> [<アーカイブ名>]" >&2
  exit 2
fi
bin="$1"
out_dir="$2"
name="${3:-shashoku-linux-x86_64}"

for required in "$bin" examples LICENSE THIRD_PARTY_LICENSES docs/dist/README.md; do
  if [ ! -e "$required" ]; then
    echo "error: $required がありません（リポジトリのルートで実行してください）" >&2
    exit 2
  fi
done

rm -rf "${out_dir:?}/$name"
mkdir -p "$out_dir/$name"
cp "$bin" "$out_dir/$name/shashoku"
chmod 755 "$out_dir/$name/shashoku"
strip "$out_dir/$name/shashoku"
cp -R examples "$out_dir/$name/examples"
cp LICENSE THIRD_PARTY_LICENSES "$out_dir/$name/"
cp docs/dist/README.md "$out_dir/$name/README.md"

# 時刻の正はコミット。git が無い環境では SOURCE_DATE_EPOCH、それも無ければ 0。
stamp="${SOURCE_DATE_EPOCH:-}"
if [ -z "$stamp" ] && command -v git > /dev/null; then
  # コンテナの中では所有者が違って git が拒むことがある（dubious ownership）。
  # 失敗しても止めない（下の既定値に落ちる）。
  stamp=$(git log -1 --format=%ct HEAD 2> /dev/null || true)
fi
stamp="${stamp:-0}"

tar -C "$out_dir" --owner=0 --group=0 --numeric-owner --sort=name \
    --mtime="@$stamp" -cf - "$name" | gzip -9n > "$out_dir/$name.tar.gz"
(cd "$out_dir" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")

echo "-- $out_dir/$name.tar.gz ($(stat -c%s "$out_dir/$name.tar.gz") B)"
cat "$out_dir/$name.tar.gz.sha256"
tar tzf "$out_dir/$name.tar.gz" | sed 's/^/   /'
