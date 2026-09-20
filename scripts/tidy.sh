#!/usr/bin/env bash
# プロジェクト自身のソース（依存ライブラリを除く）に clang-tidy をかける。
#   scripts/tidy.sh [build-dir]                    全ファイル。既定は build/dev
#   scripts/tidy.sh --changed <base> [build-dir]   <base>（例: origin/main）から変更された .cpp と、
#                                                  変更されたヘッダを include している .cpp だけ
# 先に cmake --preset dev が必要（compile_commands.json と依存ライブラリのヘッダを使う）。
set -euo pipefail
cd "$(dirname "$0")/.."

BASE=""
if [[ "${1:-}" == "--changed" ]]; then
  BASE="${2:?--changed には比較元（例: origin/main）が必要です}"
  shift 2
fi

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
patterns=("^$PWD/(src|tests|tools)/")

if [[ -n "$BASE" ]]; then
  mapfile -t selected < <(python3 scripts/tidy_select.py "$BASE" "$BUILD_DIR")
  if [[ ${#selected[@]} -eq 0 ]]; then
    echo "clang-tidy: $BASE から変更された対象ファイルはありません"
    exit 0
  fi
  if [[ "${selected[0]}" == "ALL" ]]; then
    echo "clang-tidy: lint に影響する設定が変わっているので全ファイルにかけます"
  else
    echo "clang-tidy: $BASE から影響を受ける ${#selected[@]} ファイルにかけます"
    printf '  %s\n' "${selected[@]}"
    patterns=()
    for file in "${selected[@]}"; do
      patterns+=("^$PWD/${file//./\\.}\$")
    done
  fi
fi

"$RUN_CLANG_TIDY" -quiet -p "$BUILD_DIR" "${patterns[@]}"
