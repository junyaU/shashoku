#!/usr/bin/env bash
# 全 C++ ソースを clang-format で整形する。
#   scripts/format.sh          その場で整形
#   scripts/format.sh --check  差分があれば非ゼロで終了（CI 用）
set -euo pipefail
cd "$(dirname "$0")/.."

# メジャーバージョンで整形結果が変わるので 18 を優先する（CI と揃える）
CLANG_FORMAT="${CLANG_FORMAT:-$(command -v clang-format-18 || command -v clang-format || true)}"
if [[ -z "$CLANG_FORMAT" ]]; then
  echo "clang-format が見つかりません: sudo apt install clang-format-18" >&2
  exit 1
fi

# 未コミットの新規ファイルも対象にする（.gitignore は尊重）
mapfile -t files < <(git ls-files -co --exclude-standard -- '*.cpp' '*.hpp' '*.h')
if [[ ${#files[@]} -eq 0 ]]; then
  exit 0
fi

if [[ "${1:-}" == "--check" ]]; then
  "$CLANG_FORMAT" --dry-run --Werror "${files[@]}"
else
  "$CLANG_FORMAT" -i "${files[@]}"
fi
