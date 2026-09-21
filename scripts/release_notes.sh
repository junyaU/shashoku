#!/usr/bin/env bash
# リリース（ドラフト）の本文を標準出力に書く（#20）。
#
#   scripts/release_notes.sh build/package/shashoku-linux-x86_64/shashoku \
#     build/package/shashoku-linux-x86_64.tar.gz junyaU/shashoku v0.1.0
#
# 版の欄は実際の実行ファイルの `--version` をそのまま貼る（試用報告で突き合わせるため）。
# ローカルでもそのまま回せる（docker は要らない）。
set -euo pipefail

if [ $# -ne 4 ]; then
  echo "使い方: $0 <実行ファイル> <アーカイブ> <owner/repo> <タグ>" >&2
  exit 2
fi
bin="$1"
archive="$2"
repo="$3"
tag="$4"
archive_name=$(basename "$archive")
dir_name="${archive_name%.tar.gz}"

if [ ! -x "$bin" ]; then
  echo "error: 実行ファイルがありません: $bin" >&2
  exit 2
fi
if [ ! -f "$archive.sha256" ]; then
  echo "error: $archive.sha256 がありません（scripts/pack_dist.sh が作ります）" >&2
  exit 2
fi
sha=$(cut -d' ' -f1 < "$archive.sha256")

cat <<MARKDOWN
### 試す（ビルドもフォントの用意も要りません）

\`\`\`bash
curl -LO https://github.com/$repo/releases/download/$tag/$archive_name
tar xf $archive_name && cd $dir_name
./shashoku examples/og_card.html --image icon=examples/icon.png -o og.png --width 1200 --height 630
\`\`\`

実行ファイルは 1 つだけで、依存ライブラリも既定フォント（Noto Sans JP Regular / Bold）も
中に入っています。\`--font\` を書けばそちらが優先されます。

**対応環境: linux-x86_64 / glibc 2.35 以降**（Ubuntu 22.04 以降、Debian 12 以降など）。
C++ ランタイムは静的リンク済みで、動的に要るのは libc と libm だけです。
musl の環境（Alpine など）では動きません。

### 版

\`\`\`
$("$bin" --version)
\`\`\`

### SHA256

\`\`\`
$sha  $archive_name
\`\`\`

組めなかった HTML があれば、[試用報告](https://github.com/$repo/issues/new/choose) から教えてください。
MARKDOWN
