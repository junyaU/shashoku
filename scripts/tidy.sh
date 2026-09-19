#!/usr/bin/env bash
# プロジェクト自身のソース（依存ライブラリを除く）に clang-tidy をかける。
#   scripts/tidy.sh [build-dir]   既定は build/dev。先に cmake --preset dev が必要
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build/dev}"
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
  echo "$BUILD_DIR/compile_commands.json がありません。先に: cmake --preset dev" >&2
  exit 1
fi

RUN_CLANG_TIDY="${RUN_CLANG_TIDY:-$(command -v run-clang-tidy-18 || command -v run-clang-tidy || true)}"
if [[ -z "$RUN_CLANG_TIDY" ]]; then
  echo "run-clang-tidy が見つかりません: sudo apt install clang-tidy-18" >&2
  exit 1
fi

# build/ 配下の依存ライブラリ（_deps/*/src/...）を拾わないよう、ルートからの絶対パスで絞る
"$RUN_CLANG_TIDY" -quiet -p "$BUILD_DIR" "^$PWD/(src|tests|tools)/"
