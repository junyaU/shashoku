#!/usr/bin/env python3
"""clang-tidy をかけるべき翻訳単位（.cpp）を、base からの変更に基づいて選ぶ。

    scripts/tidy_select.py <base-ref> <build-dir>

標準出力に、対象の .cpp をリポジトリ相対パスで 1 行ずつ出す。全ファイルにかけるべきときは
"ALL" の 1 行だけを出す。対象がなければ何も出さない。

選び方:
  * base との merge-base から見て変更・追加されたファイル（未コミット・未追跡を含む）を集める
  * 変更された .cpp はそのまま対象
  * 変更されたヘッダ（.hpp / .h / .inc）は、それを直接または間接に include している .cpp を
    対象にする。ヘッダだけを変えた PR が素通りしないようにするため。include はプロジェクト内では
    ルート相対（src/ include/ tests/ 起点）か同じディレクトリ相対で統一されているので、
    コンパイラを起動せずテキストの #include "..." を辿れば足りる（全体で数十ミリ秒）
  * lint の結果を変えうる設定（.clang-tidy、CMake、このスクリプト自身、CI の定義）が
    変わっていたら "ALL"（変更ファイルだけでは安全と言えないため）
"""

import json
import os
import re
import subprocess
import sys

SOURCE_ROOTS = ("src", "include", "tests", "tools")
INCLUDE_ROOTS = ("src", "include", "tests")
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".inc")
# これが変わったら全ファイルにかけ直す
GLOBAL_PATTERNS = (
    r"(^|/)\.clang-tidy$",
    r"(^|/)CMakeLists\.txt$",
    r"^CMakePresets\.json$",
    r"^cmake/",
    r"^scripts/tidy",
    r"^\.github/workflows/",
)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


def git(*args):
    return subprocess.run(("git",) + args, check=True, capture_output=True, text=True).stdout


def changed_files(base):
    merge_base = git("merge-base", base, "HEAD").strip()
    names = set(git("diff", "--name-only", "--diff-filter=d", merge_base).split("\n"))
    names |= set(git("ls-files", "--others", "--exclude-standard").split("\n"))
    return {name for name in names if name}


def project_sources():
    files = set()
    for root in SOURCE_ROOTS:
        for directory, _, names in os.walk(root):
            for name in names:
                if name.endswith(SOURCE_SUFFIXES):
                    files.add(os.path.join(directory, name))
    return files


def resolve_include(including_file, target, sources):
    candidates = [os.path.normpath(os.path.join(os.path.dirname(including_file), target))]
    candidates += [os.path.join(root, target) for root in INCLUDE_ROOTS]
    for candidate in candidates:
        if candidate in sources:
            return candidate
    return None  # 標準ライブラリや依存ライブラリのヘッダ


def reverse_include_graph(sources):
    included_by = {}
    for path in sources:
        with open(path, encoding="utf-8", errors="replace") as file:
            text = file.read()
        for target in INCLUDE_RE.findall(text):
            resolved = resolve_include(path, target, sources)
            if resolved is not None:
                included_by.setdefault(resolved, set()).add(path)
    return included_by


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    base, build_dir = sys.argv[1], sys.argv[2]

    changed = changed_files(base)
    if any(re.search(pattern, name) for name in changed for pattern in GLOBAL_PATTERNS):
        print("ALL")
        return

    sources = project_sources()
    included_by = reverse_include_graph(sources)

    affected = set()
    pending = [name for name in changed if name in sources]
    while pending:
        current = pending.pop()
        if current in affected:
            continue
        affected.add(current)
        pending.extend(included_by.get(current, ()))

    root = os.getcwd()
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as file:
        units = {os.path.relpath(entry["file"], root) for entry in json.load(file)}
    for name in sorted(affected & units):
        print(name)


if __name__ == "__main__":
    main()
