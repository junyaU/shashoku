#!/usr/bin/env python3
"""UCD から C++ の表（*.inc）を再生成する。

生成するもの（いずれも生成物はコミットする。ビルドに Python もネットワークも要らない）:

  src/linebreak/break_class_table.inc        UAX #14 Line_Break      <- LineBreak.txt
  src/text/vertical_orientation_table.inc    UAX #50 Vertical_Orientation
                                                                     <- VerticalOrientation.txt
  src/layout/east_asian_width_table.inc      East_Asian_Width W / F  <- EastAsianWidth.txt

使い方:

    scripts/gen_unicode_tables.py            再生成してファイルを書く
    scripts/gen_unicode_tables.py --check    コミット済みの表と一致するか検査（不一致なら差分 + 終了 1）
    scripts/gen_unicode_tables.py --no-legacy
                                             全角表の「据え置き」（LEGACY_WIDE_DEVIATIONS）を外し、
                                             素の UCD どおりの表を出す。振る舞いが変わるので、
                                             出力を採用するかは人間が判断する
    scripts/gen_unicode_tables.py --print-sets
                                             break_class.cpp が手で持っている小さな集合
                                             （is_east_asian_bracket / is_wide_numeric_affix）の
                                             UCD 由来の中身を表示する（目視照合用）

UCD のファイルは版と SHA256 を固定して取得し、build/ucd/<版>/ にキャッシュする（コミットしない）。
ハッシュが合わなければ失敗する。ネットワークに出るのはこのスクリプトだけで、製品コードと
テストは UCD を実行時に読まない（DESIGN.md §3-5）。

更新手順は docs/UNICODE_TABLES.md。Python 3 の標準ライブラリだけで動く。
"""

from __future__ import annotations

import argparse
import bisect
import difflib
import hashlib
import re
import sys
import urllib.request
from pathlib import Path

# ---------------------------------------------------------------------------
# 固定する UCD の版とハッシュ。版を上げるときはここだけを変える（docs/UNICODE_TABLES.md）。
# ---------------------------------------------------------------------------

UCD_VERSION = "18.0.0"
UCD_BASE_URL = "https://www.unicode.org/Public"

UCD_FILES = {
    "LineBreak.txt": "ae8cf1970c73f3f1a12d77852df96c4b2b1723ca8b388e36bb5739250c6849de",
    "EastAsianWidth.txt": "a0cf29eacd00cfcaec4381c6b7c281685f18dbb4e7ff82b4076ccb342ca839aa",
    "VerticalOrientation.txt": "0803e09669d7aa7137de678e55b848bd418c6632694fa1b05f0b40941103c748",
}

MAX_CP = 0x10FFFF
SCRIPT_NAME = "scripts/gen_unicode_tables.py"

# ---------------------------------------------------------------------------
# 規則 1: Line_Break -> BreakClass（ARCHITECTURE.md §3.4 (1) と break_class.hpp）
# ---------------------------------------------------------------------------

# そのまま持つクラス。enum の名前は CamelCase（.clang-tidy の命名規則）。
LINE_BREAK_KEPT = [
    "BK", "CR", "LF", "NL", "SP", "ZW", "WJ", "GL", "CM", "ZWJ", "OP", "CL", "CP", "QU",
    "EX", "IS", "SY", "NS", "CJ", "IN", "B2", "BA", "BB", "HY", "PR", "PO", "NU", "AL",
    "ID", "EB", "EM", "RI",
]

# 持たないクラスの寄せ先。break_class.cpp の冒頭コメントと同じ表。
LINE_BREAK_FOLDED = {
    # LB1「未知は AL」。SA（複雑スクリプト）は Mn / Mc も含めて AL に寄せる
    "AI": "AL", "SG": "AL", "XX": "AL", "SA": "AL", "CB": "AL", "HL": "AL",
    # UAX #14 18.0 で HY から分かれた明示ハイフン。LB12a / LB20a / LB21 の振る舞いが HY と同じ
    "HH": "HY",
    # LB27「ハングル音節は ID」。ハングルの音節構成規則（LB26）は実装しない
    "JL": "ID", "JV": "ID", "JT": "ID", "H2": "ID", "H3": "ID",
    # ブラーフミー系。LB28a は実装しないので「割らない側」= AL に倒す
    "AK": "AL", "AP": "AL", "AS": "AL", "VF": "AL", "VI": "AL",
}

# LineBreak.txt の見出しが定める既定値（明示列挙されていないコードポイント）。
# 「@missing: 0000..10FFFF; XX」に加えて、次のブロックの未割り当ては ID / PR になる。
LINE_BREAK_DEFAULT_RANGES = [
    (0x3400, 0x4DBF, "ID"),    # CJK Unified Ideographs Extension A
    (0x4E00, 0x9FFF, "ID"),    # CJK Unified Ideographs
    (0xF900, 0xFAFF, "ID"),    # CJK Compatibility Ideographs
    (0x20000, 0x2FFFD, "ID"),  # Plane 2
    (0x30000, 0x3FFFD, "ID"),  # Plane 3
    (0x1F000, 0x1FAFF, "ID"),  # Plane 1（絵文字）
    (0x1FC00, 0x1FFFD, "ID"),
    (0x20A0, 0x20CF, "PR"),    # Currency Symbols
]

# 既定値。表に載せない（break_class_of() が表外で返す値と一致していること）。
LINE_BREAK_TABLE_DEFAULT = "AL"

# ---------------------------------------------------------------------------
# 規則 2: Vertical_Orientation -> VerticalOrientation（UAX #50 / ARCHITECTURE.md §3.5）
# ---------------------------------------------------------------------------

VERTICAL_ORIENTATION_VALUES = {
    "U": "Upright",
    "Tu": "TransformedUpright",
    "Tr": "TransformedRotated",
    "R": "Rotated",
}

# 「@missing: 0000..10FFFF; R」。未割り当てで U に倒れるブロックは
# VerticalOrientation.txt 本体が明示列挙しているので、ここで足す既定値はない。
VERTICAL_ORIENTATION_TABLE_DEFAULT = "Rotated"

# 読みやすさのための注記（範囲の先頭コードポイントで引く）。振る舞いには影響しない。
VERTICAL_ORIENTATION_NOTES = {
    0x3001: "、。",
    0x3008: "〈〉《》「」『』【】",
    0x30FC: "ー",
    0xFF08: "（）",
}

# ---------------------------------------------------------------------------
# 規則 3: East_Asian_Width の W / F（ARCHITECTURE.md A14 の全角判定）
# ---------------------------------------------------------------------------

EAST_ASIAN_WIDE_VALUES = ("W", "F")

# EastAsianWidth.txt の見出しが定める既定値（未割り当てでも W になるブロック）。
EAST_ASIAN_DEFAULT_WIDE_RANGES = [
    (0x3400, 0x4DBF),    # CJK Unified Ideographs Extension A
    (0x4E00, 0x9FFF),    # CJK Unified Ideographs
    (0xF900, 0xFAFF),    # CJK Compatibility Ideographs
    (0x20000, 0x2FFFD),  # Plane 2
    (0x30000, 0x3FFFD),  # Plane 3
]

EAST_ASIAN_WIDTH_NOTES = {
    0x1100: "ハングル字母",
    0x231A: "⌚⌛",
    0x2329: "〈〉",
    0x2E80: "CJK 部首補助",
    0x2F00: "康熙部首",
    0x2FF0: "漢字構成記述文字",
    0x3000: "　（F）、CJK の約物 、。「」（W）",
    0x3041: "ひらがな",
    0x3099: "濁点・カタカナ",
    0x3250: "CJK 統合漢字拡張 A まで",
    0x4E00: "CJK 統合漢字・彝文字",
    0xAC00: "ハングル音節",
    0xF900: "CJK 互換漢字",
    0xFF01: "全角英数・全角約物（F）",
    0xFFE0: "￠￡￥ など（F）",
    0x20000: "CJK 統合漢字拡張 B 以降",
}

# 全角表の「据え置き」。
#
# いまコミットされている kWideRanges は、機械生成ではなく Unicode 15.1 相当の
# EastAsianWidth.txt から手で起こしたもので、18.0.0 の W / F とは 32 か所ずれている。
# 18.0.0 どおりに直すと is_fullwidth() の答えが変わり、
#   - 空白の畳み込み（ARCHITECTURE.md A14: 改行の前後がどちらも全角なら改行を消す）
#   - tests/layout/test_support.cpp の偽 TextMeasurer の字幅（全角 1em / 半角 0.5em）
# が変わる。行分割と layout のゴールデンに波及するので、issue #11 の範囲では振る舞いを変えず、
# ずれを「どこが・なぜ」の形でここに残す。採否はオーケストレーターとユーザーが判断する
# （--no-legacy で 18.0.0 どおりの表を出せる）。
#
# (lo, hi, wide, 理由)
LEGACY_WIDE_DEVIATIONS = [
    # (a) 18.0.0 では W だが、手起こしの表に入っていないもの。
    #     「W になった版」は EastAsianWidth.txt を遡って確かめた値。
    (0x2630, 0x2637, False, "八卦。16.0 で N -> W"),
    (0x268A, 0x268F, False, "太玄経の単/重記号。16.0 で N -> W"),
    (0x2FFC, 0x2FFF, False, "漢字構成記述文字。15.1 の時点で W（手起こしの漏れ）"),
    (0x31E4, 0x31E5, False, "CJK の筆画。16.0 で N -> W"),
    (0x31EF, 0x31EF, False, "漢字構成記述文字。15.1 の時点で W（手起こしの漏れ）"),
    (0x4DC0, 0x4DFF, False, "易経の六十四卦。16.0 で N -> W"),
    (0x16FF2, 0x16FF6, False, "表意文字記号。17.0 で追加"),
    (0x187F8, 0x187FF, False, "西夏文字。17.0 で追加"),
    (0x18CD6, 0x18CDA, False, "契丹小字。18.0 で追加"),
    (0x18CFF, 0x18CFF, False, "契丹小字。16.0 で追加"),
    (0x18D09, 0x18D20, False, "西夏文字補助。17.0 で追加"),
    (0x18D80, 0x18DF2, False, "西夏文字部品補助。17.0 で追加"),
    (0x18E00, 0x19191, False, "女真文字。18.0 で追加"),
    (0x191A0, 0x191D2, False, "女真文字部首。18.0 で追加"),
    (0x1B155, 0x1B155, False, "小書きカタカナ「コ」。15.1 の時点で W（手起こしの漏れ。和文の文字）"),
    (0x1B168, 0x1B168, False, "小書きカタカナ。18.0 で追加"),
    (0x1D300, 0x1D356, False, "太玄経。16.0 で N -> W"),
    (0x1D360, 0x1D376, False, "算木。16.0 で N -> W"),
    (0x1F1AE, 0x1F1AE, False, "六曜記号。18.0 で追加"),
    (0x1F6D8, 0x1F6D9, False, "絵文字。17.0 で追加"),
    (0x1F7DA, 0x1F7DA, False, "幾何学記号。18.0 で追加"),
    (0x1FA8A, 0x1FA8E, False, "絵文字。17.0 で追加"),
    (0x1FAC8, 0x1FAC8, False, "絵文字。17.0 で追加"),
    (0x1FACC, 0x1FACD, False, "絵文字。18.0 で追加"),
    (0x1FADD, 0x1FADD, False, "絵文字。18.0 で追加"),
    (0x1FAEA, 0x1FAEB, False, "絵文字。17.0 で追加"),
    (0x1FAEF, 0x1FAEF, False, "絵文字。17.0 で追加"),
    (0x1FAF9, 0x1FAFA, False, "絵文字。18.0 で追加"),
    # (b) 手起こしの表が、未割り当ての穴をまたいで範囲をつないでいるもの。
    #     いずれも将来の追加に備えた予約領域で、実在する文字ではない。
    (0x1AFF4, 0x1AFF4, True, "かな拡張 B の未割り当て。手起こしが 1AFF0..1AFFE でまとめた"),
    (0x1AFFC, 0x1AFFC, True, "かな拡張 B の未割り当て。手起こしが 1AFF0..1AFFE でまとめた"),
    (0x1B129, 0x1B131, True, "かな補助の未割り当て。手起こしが 1B000..1B152 でまとめた"),
    (0x1B133, 0x1B14F, True, "かな補助の未割り当て。手起こしが 1B000..1B152 でまとめた"),
]


# ---------------------------------------------------------------------------
# UCD の取得とパース
# ---------------------------------------------------------------------------


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def sha256_of(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_ucd_file(name: str, cache_dir: Path, ucd_dir: Path | None, offline: bool) -> bytes:
    """版と SHA256 を固定して UCD のファイルを得る。ハッシュが合わなければ失敗する。"""
    want = UCD_FILES[name]

    if ucd_dir is not None:
        path = ucd_dir / name
        data = path.read_bytes()
        got = sha256_of(data)
        if got != want:
            raise SystemExit(
                f"{path} の SHA256 が合わない:\n  期待 {want}\n  実際 {got}\n"
                f"  （{UCD_VERSION} のファイルか確認する）")
        return data

    cached = cache_dir / name
    if cached.exists():
        data = cached.read_bytes()
        if sha256_of(data) == want:
            return data
        print(f"キャッシュ {cached} のハッシュが合わないので取り直す", file=sys.stderr)

    if offline:
        raise SystemExit(f"--offline だがキャッシュ {cached} が無い（または壊れている）")

    url = f"{UCD_BASE_URL}/{UCD_VERSION}/ucd/{name}"
    print(f"取得: {url}", file=sys.stderr)
    request = urllib.request.Request(url, headers={"User-Agent": "shashoku-gen-unicode-tables"})
    with urllib.request.urlopen(request, timeout=120) as response:  # noqa: S310（固定の https URL）
        data = response.read()
    got = sha256_of(data)
    if got != want:
        raise SystemExit(
            f"{url} の SHA256 が合わない:\n  期待 {want}\n  実際 {got}\n"
            f"  （UCD が差し替わったか、版の指定が誤っている。中身を確かめてから固定値を更新する）")
    cache_dir.mkdir(parents=True, exist_ok=True)
    cached.write_bytes(data)
    return data


class UcdFile:
    """UCD の «範囲; 値 # 注記» 形式のファイル。"""

    def __init__(self, name: str, data: bytes):
        self.name = name
        self.sha256 = sha256_of(data)
        self.entries: list[tuple[int, int, str, str]] = []  # (lo, hi, 値, 注記)
        for raw in data.decode("utf-8").splitlines():
            body, _, comment = raw.partition("#")
            body = body.strip()
            if not body:
                continue
            fields = [f.strip() for f in body.split(";")]
            lo_text, _, hi_text = fields[0].partition("..")
            lo = int(lo_text, 16)
            hi = int(hi_text, 16) if hi_text else lo
            self.entries.append((lo, hi, fields[1], comment.strip()))
        self._starts = [e[0] for e in self.entries]

    def describe(self, cp: int) -> str:
        """コードポイントを含むデータ行の注記（文字名）。差分を人が読むためだけに使う。"""
        index = bisect.bisect_right(self._starts, cp) - 1
        if index < 0:
            return "<未列挙>"
        lo, hi, _, comment = self.entries[index]
        if cp > hi:
            return "<未列挙>"
        # 「So     [8] TRIGRAM FOR HEAVEN..TRIGRAM FOR EARTH」-> 名前の部分だけ
        match = re.match(r"^\S+\s+(?:\[\d+\]\s+)?(.*)$", comment)
        return match.group(1) if match else comment


# ---------------------------------------------------------------------------
# 値の割り当てと範囲の併合
# ---------------------------------------------------------------------------


def assign(ucd: UcdFile, default: str, default_ranges: list[tuple[int, int, str]]) -> list[str]:
    """コードポイント -> プロパティ値。既定値を敷いてからデータ行で上書きする。"""
    values = [default] * (MAX_CP + 1)
    for lo, hi, value in default_ranges:
        for cp in range(lo, hi + 1):
            values[cp] = value
    for lo, hi, value, _ in ucd.entries:
        for cp in range(lo, hi + 1):
            values[cp] = value
    return values


def merge_ranges(values: list[str], skip: str) -> list[tuple[int, int, str]]:
    """同じ値の隣接コードポイントをまとめる。skip の値は落とす。"""
    merged: list[list] = []
    for cp, value in enumerate(values):
        if value == skip:
            continue
        if merged and merged[-1][1] + 1 == cp and merged[-1][2] == value:
            merged[-1][1] = cp
        else:
            merged.append([cp, cp, value])
    return [(lo, hi, value) for lo, hi, value in merged]


# ---------------------------------------------------------------------------
# 出力
# ---------------------------------------------------------------------------


def header_lines(sources: list[UcdFile], rule: list[str], target: str) -> list[str]:
    lines = [
        f"// このファイルは {SCRIPT_NAME} が生成する。手で編集しない。",
        f"// 生成元: Unicode {UCD_VERSION} の UCD",
    ]
    for source in sources:
        lines.append(f"//   {source.name}  sha256 {source.sha256}")
    lines.extend(f"// {line}" for line in rule)
    lines.append(f"// このファイルは {target} の初期化子としてのみ include する。")
    return lines


def emit(lines: list[str]) -> str:
    return "".join(line + "\n" for line in lines)


def format_entry(lo: int, hi: int, digits: int, value: str | None, note: str | None) -> str:
    cells = [f"0x{lo:0{digits}X}", f"0x{hi:0{digits}X}"]
    if value is not None:
        cells.append(value)
    line = "    {" + ", ".join(cells) + "},"
    if note:
        line += f"  // {note}"
    return line


def apply_notes(ranges: list[tuple[int, int, str]], notes: dict[int, str],
                label: str) -> dict[int, str]:
    """注記を範囲の先頭で引く。引けなかった注記は版上げで範囲が変わった印なので警告する。"""
    starts = {lo for lo, _, _ in ranges}
    unused = sorted(set(notes) - starts)
    for cp in unused:
        print(f"警告: {label} の注記 U+{cp:04X}「{notes[cp]}」に対応する範囲が無い"
              f"（版上げで範囲の切れ目が変わった）", file=sys.stderr)
    return notes


# ---------------------------------------------------------------------------
# 3 つの表
# ---------------------------------------------------------------------------


def build_break_class(line_break: UcdFile) -> str:
    fold = dict(LINE_BREAK_FOLDED)
    for name in LINE_BREAK_KEPT:
        fold[name] = name
    values = assign(line_break, "XX", LINE_BREAK_DEFAULT_RANGES)
    unknown = sorted({v for v in set(values) if v not in fold})
    if unknown:
        raise SystemExit(
            f"LineBreak.txt に未知のクラスがある: {', '.join(unknown)}\n"
            f"  BreakClass への寄せ先を LINE_BREAK_FOLDED / LINE_BREAK_KEPT に足すこと")
    folded = [fold[v] for v in values]
    merged = merge_ranges(folded, skip=LINE_BREAK_TABLE_DEFAULT)

    lines = header_lines(
        [line_break],
        ["規則: Line_Break プロパティを break_class.cpp のクラス寄せ表",
         "  （ARCHITECTURE.md §3.4 (1)）で変換し、隣り合う同クラスの範囲を併合したもの。",
         f"  既定値 {LINE_BREAK_TABLE_DEFAULT.capitalize()} の範囲は載せない"
         "（break_class_of() が表外で返す）。"],
        "break_class.cpp の kClassTable")
    for lo, hi, value in merged:
        lines.append(format_entry(lo, hi, 4, f"BreakClass::{value.capitalize()}", None))
    return emit(lines)


def build_vertical_orientation(vertical: UcdFile) -> str:
    values = assign(vertical, "R", [])
    unknown = sorted({v for v in set(values) if v not in VERTICAL_ORIENTATION_VALUES})
    if unknown:
        raise SystemExit(f"VerticalOrientation.txt に未知の値がある: {', '.join(unknown)}")
    named = [VERTICAL_ORIENTATION_VALUES[v] for v in values]
    merged = merge_ranges(named, skip=VERTICAL_ORIENTATION_TABLE_DEFAULT)
    notes = apply_notes(merged, VERTICAL_ORIENTATION_NOTES, "縦書きの向き")

    lines = header_lines(
        [vertical],
        ["規則: UAX #50 Vertical_Orientation から R 以外の範囲を抜き出し、",
         "  同じ値の隣接範囲を併合したもの（lo の昇順・重なりなし）。",
         "  載っていない文字は R（@missing 行が 0000..10FFFF; R）。"],
        "char_properties.cpp の kVerticalOrientationTable")
    for lo, hi, value in merged:
        lines.append(format_entry(lo, hi, 5, f"Vo::{value}", notes.get(lo)))
    return emit(lines)


def build_east_asian_width(east_asian: UcdFile, legacy: bool) -> str:
    values = assign(east_asian, "N",
                    [(lo, hi, "W") for lo, hi in EAST_ASIAN_DEFAULT_WIDE_RANGES])
    wide = ["W" if v in EAST_ASIAN_WIDE_VALUES else "-" for v in values]
    if legacy:
        for lo, hi, is_wide, _ in LEGACY_WIDE_DEVIATIONS:
            for cp in range(lo, hi + 1):
                wide[cp] = "W" if is_wide else "-"
    merged = merge_ranges(wide, skip="-")
    notes = apply_notes(merged, EAST_ASIAN_WIDTH_NOTES, "全角")

    rule = ["規則: East_Asian_Width が W / F のコードポイント（見出しが定める",
            "  「未割り当てでも W」のブロックを含む）を、隣接する範囲を併合して並べたもの。"]
    if legacy:
        rule.append(f"  さらに {SCRIPT_NAME} の LEGACY_WIDE_DEVIATIONS "
                    f"{len(LEGACY_WIDE_DEVIATIONS)} 件を当ててある")
        rule.append("  （現行の表が Unicode 15.1 相当で凍結されているため。docs/UNICODE_TABLES.md）。")
        rule.append("  --no-legacy を付けると 18.0.0 そのままの表になる（振る舞いが変わる）。")
    else:
        rule.append("  LEGACY_WIDE_DEVIATIONS は当てていない（--no-legacy）。")

    lines = header_lines([east_asian], rule, "east_asian_width.cpp の kWideRanges")
    for lo, hi, _ in merged:
        lines.append(format_entry(lo, hi, 4, None, notes.get(lo)))
    return emit(lines)


# ---------------------------------------------------------------------------
# 差分の表示
# ---------------------------------------------------------------------------

ENTRY_RE = re.compile(r"^\s*\{0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)(?:,\s*(?:\w+::)?(\w+))?\}")


def parse_generated(text: str) -> list[tuple[int, int, str]]:
    out = []
    for line in text.splitlines():
        match = ENTRY_RE.match(line)
        if match:
            out.append((int(match.group(1), 16), int(match.group(2), 16), match.group(3) or "W"))
    return out


def semantic_diff(old: str, new: str, default: str, ucd: UcdFile) -> list[str]:
    """表の意味の差（どのコードポイントの値が変わるか）を、文字名つきで並べる。"""
    def spread(ranges):
        values = [default] * (MAX_CP + 1)
        for lo, hi, value in ranges:
            for cp in range(lo, hi + 1):
                values[cp] = value
        return values

    before = spread(parse_generated(old))
    after = spread(parse_generated(new))
    changes: list[list] = []
    for cp in range(MAX_CP + 1):
        if before[cp] == after[cp]:
            continue
        if (changes and changes[-1][1] + 1 == cp and changes[-1][2] == before[cp]
                and changes[-1][3] == after[cp]):
            changes[-1][1] = cp
        else:
            changes.append([cp, cp, before[cp], after[cp]])
    lines = []
    for lo, hi, was, now in changes:
        span = f"U+{lo:04X}" if lo == hi else f"U+{lo:04X}..U+{hi:04X}"
        lines.append(f"    {span:<20} {was} -> {now}  {ucd.describe(lo)}")
    return lines


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true",
                        help="書かずに、コミット済みの表と一致するか検査する")
    parser.add_argument("--no-legacy", action="store_true",
                        help="全角表の据え置き（LEGACY_WIDE_DEVIATIONS）を外す")
    parser.add_argument("--print-sets", action="store_true",
                        help="break_class.cpp が手で持つ小さな集合の UCD 由来の中身を表示する")
    parser.add_argument("--ucd-dir", type=Path, default=None,
                        help="UCD のファイルを置いたディレクトリ（取得しない。ハッシュは検査する）")
    parser.add_argument("--cache-dir", type=Path, default=None,
                        help=f"キャッシュ先（既定: build/ucd/{UCD_VERSION}）")
    parser.add_argument("--offline", action="store_true",
                        help="キャッシュが無ければ取得せずに失敗する")
    args = parser.parse_args()

    root = repo_root()
    cache_dir = args.cache_dir or (root / "build" / "ucd" / UCD_VERSION)
    sources = {name: UcdFile(name, load_ucd_file(name, cache_dir, args.ucd_dir, args.offline))
               for name in sorted(UCD_FILES)}

    if args.print_sets:
        print_derived_sets(sources)
        return 0

    line_break = sources["LineBreak.txt"]
    east_asian = sources["EastAsianWidth.txt"]
    vertical = sources["VerticalOrientation.txt"]

    targets = [
        (root / "src/linebreak/break_class_table.inc", build_break_class(line_break),
         "Al", line_break),
        (root / "src/text/vertical_orientation_table.inc",
         build_vertical_orientation(vertical), "Rotated", vertical),
        (root / "src/layout/east_asian_width_table.inc",
         build_east_asian_width(east_asian, legacy=not args.no_legacy), "-", east_asian),
    ]

    failed = False
    for path, content, default, ucd in targets:
        relative = path.relative_to(root)
        if not args.check:
            path.write_text(content, encoding="utf-8", newline="\n")
            print(f"書いた: {relative}")
            continue
        current = path.read_text(encoding="utf-8") if path.exists() else ""
        if current == content:
            print(f"一致: {relative}")
            continue
        failed = True
        print(f"不一致: {relative}")
        diff = difflib.unified_diff(current.splitlines(), content.splitlines(),
                                    fromfile=f"{relative}（コミット済み）",
                                    tofile=f"{relative}（再生成）", lineterm="", n=1)
        for i, line in enumerate(diff):
            if i >= 200:
                print("  …（以下略）")
                break
            print(f"  {line}")
        changes = semantic_diff(current, content, default, ucd)
        if changes:
            print(f"  値が変わるコードポイント（{len(changes)} 区間）:")
            for line in changes[:100]:
                print(line)
            if len(changes) > 100:
                print("    …（以下略）")

    if failed:
        print(f"\n表がコミット済みのものと一致しない。{SCRIPT_NAME} で再生成し、"
              f"差分を読んでから取り込むこと（docs/UNICODE_TABLES.md）。")
        return 1
    return 0


def print_derived_sets(sources: dict[str, UcdFile]) -> None:
    """break_class.cpp が手で持っている集合を UCD から作って見せる（目視照合用）。"""
    line_break = assign(sources["LineBreak.txt"], "XX", LINE_BREAK_DEFAULT_RANGES)
    east_asian = assign(sources["EastAsianWidth.txt"], "N",
                        [(lo, hi, "W") for lo, hi in EAST_ASIAN_DEFAULT_WIDE_RANGES])

    def show(title: str, classes: tuple[str, ...], widths: tuple[str, ...]) -> None:
        members = [cp for cp in range(MAX_CP + 1)
                   if line_break[cp] in classes and east_asian[cp] in widths]
        print(f"{title}（Line_Break {'/'.join(classes)} かつ "
              f"East_Asian_Width {'/'.join(widths)}）: {len(members)} 個")
        print("  " + ", ".join(f"0x{cp:04X}" for cp in members))

    print(f"Unicode {UCD_VERSION} から導いた集合。break_class.cpp の手書きの集合と見比べること。")
    show("is_east_asian_bracket", ("OP", "CP"), ("F", "W", "H"))
    show("is_wide_numeric_affix", ("PO", "PR"), ("A", "F", "W"))


if __name__ == "__main__":
    sys.exit(main())
