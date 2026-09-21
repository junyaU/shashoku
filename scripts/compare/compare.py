#!/usr/bin/env python3
"""shashoku と Chrome（headless）の組版を、4 つの判定粒度で比べるローカル専用スクリプト。

#15 のとおり、ピクセル差は見ない。見るのは **欠落 / 重なり / 改行位置 / はみ出し** の 4 つ。
Chrome は正解ではなく、差を見つけるための第 2 の意見として使う。

依存はゼロ（python3 の標準ライブラリと chrome.exe だけ）。CDP（WebSocket）は使わず、
ページ内の JavaScript（measure.js）で測って結果を DOM に書き出し、
`chrome.exe --headless --dump-dom` の標準出力から取り出す。

使い方:
    scripts/compare/compare.py \\
        --shashoku build/release/tools/shashoku/shashoku \\
        --chrome '/mnt/c/Program Files/Google/Chrome/Application/chrome.exe' \\
        --fonts build/_assets/fonts

    環境変数 SHASHOKU_BIN / SHASHOKU_CHROME / SHASHOKU_FONTS_DIR でも渡せる。
    --only 1,2,8 でケースを絞る。--no-chrome で shashoku 側だけ出す。

出力（--out、既定は build/compare/）:
    report.txt   ケース × 4 判定の表（まずこれを見る）
    report.html  左右に並べた画像 + 判定の詳細
    <case>/      shashoku.png / chrome.png / side-by-side.png / box.json / chrome.json
"""

import argparse
import base64
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import unicodedata
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# 比較の許容差。グリフ原点の丸め（A8）と Chrome の LayoutUnit（1/64 px）で必ず小さな差が出る。
TOL_INLINE = 1.0   # inline 方向の px
TOL_BLOCK = 2.5    # block 方向の px（line-height: normal の丸め方が違うぶん大きめ）

JUDGMENTS = ["欠落", "重なり", "改行位置", "はみ出し"]

FONT_FILES = {
    "jp": "NotoSansJP-Regular.otf",
    "jp-bold": "NotoSansJP-Bold.otf",
    "latin": "NotoSans-Regular.ttf",
}


# ---------------------------------------------------------------- フォントの情報
def read_font_info(path):
    """OpenType の name / head / hhea / OS/2 から、Chrome 側の @font-face に要る値を読む。

    family 名で Chrome 側の指定を組み立て、weight で font-weight を合わせる。
    ascent / descent は shashoku 側の行の ink の広がりを出すのに使う。
    """
    with open(path, "rb") as f:
        data = f.read()
    (_, num_tables) = struct.unpack(">IH", data[0:6])
    tables = {}
    for i in range(num_tables):
        off = 12 + i * 16
        tag, _, offset, length = struct.unpack(">4sIII", data[off:off + 16])
        tables[tag.decode("latin1")] = (offset, length)

    name_off = tables["name"][0]
    _, count, string_off = struct.unpack(">HHH", data[name_off:name_off + 6])
    family = None
    for i in range(count):
        e = name_off + 6 + i * 12
        pid, eid, lid, nid, ln, off = struct.unpack(">HHHHHH", data[e:e + 12])
        if nid != 1 or (pid, eid, lid) != (3, 1, 0x409):
            continue
        raw = data[name_off + string_off + off:name_off + string_off + off + ln]
        family = raw.decode("utf-16-be")
    if family is None:
        family = os.path.basename(path)

    head_off = tables["head"][0]
    upem = struct.unpack(">H", data[head_off + 18:head_off + 20])[0]
    hhea_off = tables["hhea"][0]
    ascender, descender, line_gap = struct.unpack(">hhh", data[hhea_off + 4:hhea_off + 10])
    weight = 400
    if "OS/2" in tables:
        weight = struct.unpack(">H", data[tables["OS/2"][0] + 4:tables["OS/2"][0] + 6])[0]

    return {
        "path": path,
        "family": family,
        "weight": weight,
        "upem": upem,
        "ascent": ascender / upem,
        "descent": -descender / upem,
        "line_gap": line_gap / upem,
    }


def non_finite_paths(node, path="root", found=None):
    """box ダンプの中の `null`（= 非有限値）を探す。

    shashoku の JSON ダンプは inf / nan を null にする。そうなった木は座標として
    使えないので、比較の前に見つけて「有限性」の判定として別立てにする（ケース 20）。
    """
    if found is None:
        found = []
    if len(found) >= 20:
        return found
    if node is None:
        found.append(path)
    elif isinstance(node, dict):
        for key, value in node.items():
            non_finite_paths(value, "%s.%s" % (path, key), found)
    elif isinstance(node, list):
        for i, value in enumerate(node):
            non_finite_paths(value, "%s[%d]" % (path, i), found)
    return found


def round64(v):
    """shashoku（FreeType の 26.6 固定小数）と同じ 1/64 px 丸め。"""
    return round(v * 64.0) / 64.0


# ---------------------------------------------------------------- 書記素の近似
_COMBINING = ("Mn", "Mc", "Me")


def graphemes(text):
    """結合文字・異体字セレクタ・サロゲートペアをまとめるだけの近似（比較ケースにはこれで足りる）。

    Intl.Segmenter（Chrome 側）と完全には一致しないので、数が合わないときは
    そのことを判定に出して、クラスタ単位の比較を諦める。
    """
    out = []
    for ch in text:
        cp = ord(ch)
        joinable = (
            unicodedata.category(ch) in _COMBINING
            or 0xFE00 <= cp <= 0xFE0F
            or 0xE0100 <= cp <= 0xE01EF
            or cp == 0x200D
        )
        if out and (joinable or (len(out[-1]) > 0 and out[-1][-1] == "‍")):
            out[-1] += ch
        else:
            out.append(ch)
    return out


# ---------------------------------------------------------------- 最小限の PNG
def png_read(path):
    """8 bit / 非インターレース の PNG を (width, height, rgba_bytes) にする。"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s は PNG ではない" % path)
    pos, idat, header = 8, bytearray(), None
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or interlace != 0 or color not in (0, 2, 4, 6):
                raise ValueError("%s: 対応していない PNG（depth=%d color=%d interlace=%d）"
                                 % (path, depth, color, interlace))
            header = (w, h, color)
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length

    w, h, color = header
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color]
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    out = bytearray(h * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise ValueError("%s: 未知のフィルタ %d" % (path, ft))
        out[y * stride:(y + 1) * stride] = line
        prev = line

    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        px = out[i * channels:(i + 1) * channels]
        if channels == 1:
            rgba[i * 4:i * 4 + 4] = bytes((px[0], px[0], px[0], 255))
        elif channels == 2:
            rgba[i * 4:i * 4 + 4] = bytes((px[0], px[0], px[0], px[1]))
        elif channels == 3:
            rgba[i * 4:i * 4 + 4] = bytes((px[0], px[1], px[2], 255))
        else:
            rgba[i * 4:i * 4 + 4] = px
    return w, h, rgba


def png_write(path, w, h, rgba):
    """RGBA 8 bit、フィルタなしで書く（比較用なのでサイズは気にしない）。"""
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


def png_crop(path, width, height):
    """PNG の左上 width×height を切り出して上書きする。

    Chrome のスクリーンショットは `--window-size` そのままの大きさで、1:1 の CSS px で撮れる。
    ただし WSL から呼ぶと幅 500 px 未満のウィンドウを作れず、小さすぎるウィンドウでは
    内容が描かれないことがある（実測: 300x117 では親文字が出ず、600x300 では出る）。
    そこで大きめに撮ってから、shashoku の PNG と同じ範囲に切り出す。
    """
    w, h, rgba = png_read(path)
    width, height = min(width, w), min(height, h)
    out = bytearray(width * height * 4)
    for y in range(height):
        out[y * width * 4:(y + 1) * width * 4] = rgba[y * w * 4:y * w * 4 + width * 4]
    png_write(path, width, height, out)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(26)
    return struct.unpack(">II", head[16:24])


def png_side_by_side(left_path, right_path, out_path, gap=12):
    """2 枚の PNG を左右に並べて 1 枚にする（間に灰色の仕切りを入れる）。"""
    lw, lh, lp = png_read(left_path)
    rw, rh, rp = png_read(right_path)
    w, h = lw + gap + rw, max(lh, rh)
    canvas = bytearray(b"\xff\xff\xff\xff" * (w * h))
    for y in range(h):
        base = y * w * 4
        canvas[base + lw * 4:base + (lw + gap) * 4] = b"\xc8\xc8\xc8\xff" * gap
    for y in range(lh):
        canvas[y * w * 4:y * w * 4 + lw * 4] = lp[y * lw * 4:(y + 1) * lw * 4]
    for y in range(rh):
        off = y * w * 4 + (lw + gap) * 4
        canvas[off:off + rw * 4] = rp[y * rw * 4:(y + 1) * rw * 4]
    png_write(out_path, w, h, canvas)
    return w, h


def make_box_png(path, size=64):
    """比較ケースが使う <img> を作る（リポジトリにバイナリを置かないため、実行時に生成する）。"""
    rgba = bytearray()
    for y in range(size):
        for x in range(size):
            edge = x < 2 or y < 2 or x >= size - 2 or y >= size - 2
            if edge:
                rgba += bytes((0x20, 0x30, 0x50, 0xFF))
            else:
                rgba += bytes((0x60 + (x * 2) % 0x60, 0x90, 0xC0, 0xFF))
    png_write(path, size, size, rgba)


# ---------------------------------------------------------------- shashoku 側
def parse_box_dump(dump, font_infos):
    """--dump-stage box の JSON を、Chrome 側と同じ形（blocks → lines → items）に直す。"""
    vertical = dump.get("writing_mode", "horizontal-tb").startswith("vertical")
    blocks = []
    notes = []

    def font_metrics(index):
        info = font_infos[index] if index < len(font_infos) else font_infos[0]
        return info["ascent"], info["descent"]

    ruby_groups = []

    def collect_ruby_groups(line):
        """断片の並びからルビ組を切り出す。

        box ダンプには組の境目が出ないので、**「注記の直前の text 断片から後ろ向きに伸ばし、
        親文字の送りの中心が注記の中心と一致したところで止める」**で切る。shashoku は
        短い方を中央に置く（A-new-2）ので、中心が一致するのは組をちょうど覆ったときだけ。
        """
        line_baseline = line["baseline"]
        texts = [f for f in line["fragments"] if f.get("type") == "text"]
        for i, frag in enumerate(texts):
            if frag["baseline"] >= line_baseline - 0.01:
                continue  # 注記ではない
            rt_start = frag["inline_start"]
            rt_end = rt_start + frag["inline_size"]
            rt_center = (rt_start + rt_end) / 2.0
            base = []
            for prev in reversed(texts[:i]):
                if prev["baseline"] < line_baseline - 0.01:
                    break  # 別の組の注記に当たった
                base.insert(0, prev)
                start = base[0]["inline_start"]
                end = max(f["inline_start"] + f["inline_size"] for f in base)
                if abs((start + end) / 2.0 - rt_center) < 0.05:
                    break
            if not base:
                continue
            start = base[0]["inline_start"]
            end = max(f["inline_start"] + f["inline_size"] for f in base)
            last = base[-1]
            last_glyphs = last.get("glyphs") or [[0, last["inline_start"], 0, 0]]
            ruby_groups.append({
                "text": "".join(f["text"] for f in base) + frag["text"],
                "base_advance": [start, end],
                "base_last_cluster_start": last_glyphs[-1][1],
                "rt_box": [rt_start, rt_end],
            })

    def convert_line(line):
        collect_ruby_groups(line)
        line_baseline = line["baseline"]
        items, ruby_items = [], []
        for frag in line["fragments"]:
            kind = frag.get("type")
            if kind == "image":
                rect = frag["rect"]
                items.append({
                    "kind": "image", "text": "", "ruby": False,
                    "inline_start": rect[0], "inline_end": rect[0] + rect[2],
                    "block_start": rect[1], "block_end": rect[1] + rect[3],
                })
                continue
            if kind != "text":
                # background（インライン背景の箱）など、文字を持たない断片。
                # 中身は他の断片から出るので、4 つの判定では見ない。
                continue
            is_ruby = frag["baseline"] < line_baseline - 0.01
            size = frag["font_size"]
            if vertical:
                # 縦書きのベースラインは字面の中心軸。ink は前後に font-size の半分ずつ
                # （#15 本文の候補 8「インライン背景は font-size の半分ずつ」）。
                top = frag["baseline"] - size / 2.0
                bottom = frag["baseline"] + size / 2.0
            else:
                ascent, descent = font_metrics(frag.get("font", 0))
                top = frag["baseline"] - round64(size * ascent)
                bottom = frag["baseline"] + round64(size * descent)
            clusters = graphemes(frag["text"])
            glyphs = frag.get("glyphs", [])
            target = ruby_items if is_ruby else items
            if len(clusters) == len(glyphs) and glyphs:
                for i, cluster in enumerate(clusters):
                    start = glyphs[i][1]
                    end = (glyphs[i + 1][1] if i + 1 < len(glyphs)
                           else frag["inline_start"] + frag["inline_size"])
                    target.append({
                        "kind": "text", "text": cluster, "ruby": is_ruby,
                        "inline_start": start, "inline_end": end,
                        "block_start": top, "block_end": bottom,
                    })
            else:
                # 合字（Noto Sans JP の fl など）でグリフ数とクラスタ数が合わない。
                # クラスタ単位の字送りは比べられないので、断片ごとの箱だけ使う。
                notes.append("合字などでグリフ数 %d ≠ クラスタ数 %d の断片がある: %r"
                             % (len(glyphs), len(clusters), frag["text"]))
                target.append({
                    "kind": "text", "text": frag["text"], "ruby": is_ruby,
                    "inline_start": frag["inline_start"],
                    "inline_end": frag["inline_start"] + frag["inline_size"],
                    "block_start": top, "block_end": bottom,
                })
        items.sort(key=lambda it: it["inline_start"])
        rect = line["rect"]
        return {
            "inline_start": rect[0], "inline_end": rect[0] + rect[2],
            "block_start": rect[1], "block_end": rect[1] + rect[3],
            "items": items, "ruby": ruby_items, "baseline": line_baseline,
        }

    def walk(node, clip):
        rect = node["rect"]
        # 祖先の箱との共通部分。flex アイテムのように、自分の箱が親より広くなることがある
        # （ケース 8）ので、はみ出しは祖先まで見ないと測れない。
        clip = (max(clip[0], rect[0]), min(clip[1], rect[0] + rect[2]))
        if "lines" in node:
            blocks.append({
                "tag": node.get("tag", "?"),
                "rect": {"inline_start": rect[0], "inline_end": rect[0] + rect[2],
                         "block_start": rect[1], "block_end": rect[1] + rect[3]},
                "clip": {"inline_start": clip[0], "inline_end": clip[1]},
                "lines": [convert_line(line) for line in node["lines"]],
            })
        for child in node.get("blocks", []):
            walk(child, clip)

    walk(dump["root"], (float("-inf"), float("inf")))
    root_rect = dump["root"]["rect"]
    vw = float(dump["viewport_width"])
    vh = dump.get("viewport_height")
    # box は論理座標。横書きは inline = 紙面の幅 / block = 紙面の高さ、
    # 縦書きは inline = 紙面の高さ / block = 紙面の幅（A1）。
    # --height を省くと画像の高さは内容に追従するので、ルートの広がりを使う。
    if vertical:
        inline_end = float(vh) if vh else root_rect[0] + root_rect[2]
        block_end = vw
    else:
        inline_end = vw
        block_end = float(vh) if vh else root_rect[1] + root_rect[3]
    viewport = {"inline_start": 0.0, "inline_end": inline_end,
                "block_start": 0.0, "block_end": block_end}
    return {"vertical": vertical, "viewport": viewport, "blocks": blocks, "notes": notes,
            "ruby_groups": ruby_groups}


# ---------------------------------------------------------------- 4 つの判定
def line_text(line):
    text = "".join(it["text"] for it in line["items"] if it["kind"] == "text")
    return re.sub(r"\s+", " ", text).strip()


def all_lines(side):
    out = []
    for block in side["blocks"]:
        for line in block["lines"]:
            out.append((block, line))
    return out


def judge_linebreak(shk, chrome):
    a = [line_text(ln) for _, ln in all_lines(shk)]
    b = [line_text(ln) for _, ln in all_lines(chrome)]
    a = [t for t in a if t]
    b = [t for t in b if t]
    if a == b:
        return "same", "%d 行" % len(a), {"shashoku": a, "chrome": b}
    detail = {"shashoku": a, "chrome": b}
    if len(a) != len(b):
        return "diff", "行数 %d / %d" % (len(a), len(b)), detail
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return "diff", "%d 行目が違う" % (i + 1), detail
    return "diff", "?", detail


def self_overlap(side):
    """同じ側の中で、隣り合うクラスタの箱が inline 方向に重なっている量の最大。"""
    worst, where = 0.0, None
    for _, line in all_lines(side):
        items = sorted(line["items"], key=lambda it: it["inline_start"])
        for prev, cur in zip(items, items[1:]):
            over = prev["inline_end"] - cur["inline_start"]
            if over > worst and over > 0.1:
                worst, where = over, "%r と %r" % (prev["text"], cur["text"])
    return worst, where


def cluster_steps(line, key="items", internal_only=False):
    """クラスタごとの送り（次のクラスタとの inline 距離）。最後は箱の幅。

    key を "ruby" にすると `<rt>` の側を見る。親文字（items）と注記（ruby）は
    字間の扱いが違う（shashoku は `<rt>` に letter-spacing を適用しない。A37）ので、
    判定でも分けて数える。

    `internal_only` を立てると最後のクラスタ（= 箱の幅）を落とす。注記の側はこれが要る:
    **Chrome の `<rt>` の Range が返すのは「割り当てられた枡」で、グリフの送りではない**
    （実測: 1 文字の注記 `x` の箱が親文字の送り 108.91 px いっぱいに広がる）。
    クラスタ間の送りだけなら、両者とも「字間が入っているか」を同じ意味で表す。
    """
    items = sorted(line[key], key=lambda it: it["inline_start"])
    steps = []
    for i, it in enumerate(items):
        if i + 1 == len(items):
            if internal_only:
                break
            nxt = it["inline_end"]
        else:
            nxt = items[i + 1]["inline_start"]
        steps.append((it["text"] if it["kind"] == "text" else "<img>", nxt - it["inline_start"]))
    return steps


def compare_steps(a_lines, b_lines, key, notes, internal_only=False):
    """2 つの側のクラスタの送りを行ごとに突き合わせ、(最大の差, どこで, 比べられたか) を返す。"""
    worst, worst_at = 0.0, None
    comparable = len(a_lines) == len(b_lines)
    if not comparable:
        return worst, worst_at, False
    for i, ((_, la), (_, lb)) in enumerate(zip(a_lines, b_lines)):
        sa = cluster_steps(la, key, internal_only)
        sb = cluster_steps(lb, key, internal_only)
        if [t for t, _ in sa] != [t for t, _ in sb]:
            comparable = False
            notes.append("%d 行目（%s）: クラスタの並びが違う（%d / %d 個）"
                         % (i + 1, key, len(sa), len(sb)))
            continue
        for (text, x), (_, y) in zip(sa, sb):
            if abs(x - y) > worst:
                worst, worst_at = abs(x - y), "%d 行目 %r: %.2f / %.2f" % (i + 1, text, x, y)
    return worst, worst_at, comparable


def ruby_centering(side):
    """ルビ組ごとに「親文字の送りの箱」と「注記の箱」の中心のずれを測る。

    shashoku は注記を**親文字の送り**（末尾の字間を含む）の中央に置く（A37）。
    Chrome がインクの中央に置くのか送りの中央に置くのかを見るための値で、
    `rt_center − base_center` を px で返す。組の切り出しは座標から推測せず、
    shashoku は box ダンプの断片の並びから、Chrome は DOM の `<ruby>` から取る。
    """
    out = []
    for group in side.get("ruby_groups", []):
        base_start, base_end = group["base_advance"]
        rt_start, rt_end = group["rt_box"]
        out.append({
            "text": group.get("text", ""),
            # 親文字の「送り」の箱。末尾の字間を含む（A37）
            "base_advance": [round(base_start, 3), round(base_end, 3)],
            # 末尾クラスタの開始。インクの終わりはここ + そのグリフの送りなので、
            # 「送りの中央」と「インクの中央」のどちらに合わせているかが読み取れる
            "base_last_cluster_start": round(group.get("base_last_cluster_start", 0), 3),
            "rt_box": [round(rt_start, 3), round(rt_end, 3)],
            "rt_center_minus_base_center": round(
                (rt_start + rt_end) / 2.0 - (base_start + base_end) / 2.0, 3),
        })
    return out


def judge_overlap(shk, chrome):
    shk_over, shk_where = self_overlap(shk)
    ch_over, ch_where = self_overlap(chrome)
    notes = []
    a, b = all_lines(shk), all_lines(chrome)
    # 親文字（items）と注記（ruby）は分けて数える。shashoku は `<rt>` に letter-spacing を
    # 適用しない（A37）ので、両者を混ぜると「#16 が直ったこと」と「A37 の意図した差」が
    # 同じ 1 つの数字に潰れてしまう。
    base_worst, base_at, base_ok = compare_steps(a, b, "items", notes)
    rt_worst, rt_at, rt_ok = compare_steps(a, b, "ruby", notes, internal_only=True)
    detail = {
        "shashoku_self_overlap": round(shk_over, 3),
        "shashoku_where": shk_where,
        "chrome_self_overlap": round(ch_over, 3),
        "chrome_where": ch_where,
        "step_diffs": notes,
        "base_max_step_diff": round(base_worst, 3),
        "base_max_step_diff_at": base_at,
        "rt_max_step_diff": round(rt_worst, 3),
        "rt_max_step_diff_at": rt_at,
        "ruby_centering_shashoku": ruby_centering(shk),
        "ruby_centering_chrome": ruby_centering(chrome),
    }

    # 0.1 px までは丸めの差（Chrome の LayoutUnit は 1/64 px、shashoku は A8 の丸め）。
    # Chrome 側の「重なり」は判定に使わない: ルビ組では親文字の Range の箱が注記の幅まで
    # 広がり、続きの文字と重なって見える（実測: ケース 2 で 8 px）。判定は
    #   (1) shashoku が本当に字を重ねていないか
    #   (2) 親文字の字送りが合っているか
    #   (3) 注記の字送りが合っているか
    # の 3 つで行い、Chrome 側の重なりは詳細に残すだけにする。
    suffix = "（Chrome 側 %.2f px は ruby の箱）" % ch_over if ch_over > 0.1 else ""
    rt_note = ""
    if rt_ok and rt_worst > TOL_INLINE:
        rt_note = " / rt %.2f px" % rt_worst
    if shk_over > 0.1:
        return "diff", "shashoku で %.2f px 重なる%s" % (shk_over, suffix), detail
    if not base_ok:
        return "unknown", "クラスタの並びが違い比較不能", detail
    if base_worst > TOL_INLINE:
        return "diff", "親文字 %.2f px%s%s" % (base_worst, rt_note, suffix), detail
    if rt_note:
        return "diff", "親文字 %.2f px%s%s" % (base_worst, rt_note, suffix), detail
    return "same", "親文字 %.2f px%s" % (base_worst, suffix), detail


def overflow_amounts(side):
    """行（または flex アイテム）の inline 方向のはみ出し量。

    shashoku 側は `max(inline_start + inline_size) − rect[2]`（#15 のコメント §2-3 のとおり、
    box ダンプに `overflows` フィールドは無いので自分で計算する）。
    Chrome 側は断片の inline の終端が包含ブロックを超えた量。
    """
    worst, where = 0.0, None
    for block, line in all_lines(side):
        items = line["items"] + line["ruby"]
        if not items:
            continue
        end = max(it["inline_end"] for it in items)
        limit = min(line["inline_end"], block["clip"]["inline_end"])
        over = end - limit
        if over > worst:
            worst, where = over, "%s: %r" % (block["tag"], line_text(line)[:16])
    return worst, where


def judge_overflow(shk, chrome):
    a, a_at = overflow_amounts(shk)
    b, b_at = overflow_amounts(chrome)
    detail = {"shashoku": round(a, 3), "shashoku_where": a_at,
              "chrome": round(b, 3), "chrome_where": b_at}
    if abs(a - b) > TOL_INLINE:
        return "diff", "%.1f / %.1f px" % (a, b), detail
    if max(a, b) > TOL_INLINE:
        return "same", "両側とも %.1f px" % a, detail
    return "same", "なし", detail


def missing_amounts(side):
    """block 方向に、ブロックの箱・紙面の外へ ink が出た量。

    ルビの注記は数えない。ルビは行の上（縦なら右）の帯に置かれ、その帯と注記の
    インラインボックスの関係は shashoku と Chrome で作りが違うので、
    ここに混ぜると全部のルビのケースが差になってしまう（実測: Chrome は 4 px 出る）。
    ルビの広がりは ruby_out_of_block として別に出す。
    """
    out_of_block, out_of_view, ruby_out = 0.0, 0.0, 0.0
    where_block, where_view = None, None
    view = side["viewport"]
    for block, line in all_lines(side):
        for it in line["ruby"]:
            ruby_out = max(ruby_out,
                           block["rect"]["block_start"] - it["block_start"],
                           it["block_end"] - block["rect"]["block_end"])
        for it in line["items"]:
            over = max(block["rect"]["block_start"] - it["block_start"],
                       it["block_end"] - block["rect"]["block_end"])
            if over > out_of_block:
                out_of_block, where_block = over, "%s: %r" % (block["tag"], it["text"])
            over_v = max(view["block_start"] - it["block_start"],
                         it["block_end"] - view["block_end"])
            if over_v > out_of_view:
                out_of_view, where_view = over_v, "%r" % it["text"]
    return out_of_block, where_block, out_of_view, where_view, ruby_out


def judge_missing(shk, chrome):
    ab, aw, av, avw, ar = missing_amounts(shk)
    bb, bw, bv, bvw, br = missing_amounts(chrome)
    detail = {"shashoku_out_of_block": round(ab, 3), "shashoku_where": aw,
              "shashoku_out_of_paper": round(av, 3), "shashoku_paper_where": avw,
              "shashoku_ruby_out_of_block": round(ar, 3),
              "chrome_out_of_block": round(bb, 3), "chrome_where": bw,
              "chrome_out_of_paper": round(bv, 3), "chrome_paper_where": bvw,
              "chrome_ruby_out_of_block": round(br, 3)}
    if abs(ab - bb) > TOL_BLOCK:
        return "diff", "行外 %.1f / %.1f px" % (ab, bb), detail
    if av > TOL_BLOCK and bv <= TOL_BLOCK:
        return "diff", "shashoku だけ紙面の外へ %.1f px" % av, detail
    if max(ab, bb) > TOL_BLOCK:
        return "same", "両側とも行外 %.1f px" % ab, detail
    return "same", "なし", detail


# ---------------------------------------------------------------- Chrome の起動
def to_windows_path(path):
    # **必ず絶対パスにしてから渡す。** 相対パスのままだと `wslpath -w` も相対のまま返し、
    # chrome.exe は自分のカレントディレクトリを基準に解釈して
    # 「Failed to write file build\compare\01\chrome.png: 指定されたパスが見つかりません」
    # で静かに失敗する（スクリーンショットだけが書かれない形で出る）。
    return subprocess.run(["wslpath", "-w", os.path.abspath(path)], check=True,
                          capture_output=True, text=True).stdout.strip()


def to_file_url(path):
    win = to_windows_path(path)
    if win.startswith("\\\\"):
        return "file:" + win.replace("\\", "/")          # UNC: file://wsl.localhost/...
    return "file:///" + win.replace("\\", "/")


def build_chrome_page(fragment, font_infos, width, height, vertical, measure_js, image_urls):
    """比較ケースの断片を、shashoku と同じ条件になるように包んだページを組む。

    - フォントは shashoku に渡したのと同じファイルを @font-face で読ませる。
      family 名は SHK0, SHK1, … に付け替える（同名のシステムフォントを拾わないため）。
      shashoku は「フォントファイルの family 名でまとめ、その中で weight が近いものを選ぶ」ので、
      同じ family 名のファイルは同じ SHK<n> にまとめ、weight だけ変える。
    - shashoku の UA スタイルシート（src/style/ua_stylesheet.cpp）と同じ既定値を入れる。
    - CLI の既定が --line-break strict なので line-break: strict にする。
    - font-synthesis: none（shashoku は合成ボールドを作らない）。
    """
    groups, order = {}, []
    for info in font_infos:
        if info["family"] not in groups:
            groups[info["family"]] = []
            order.append(info["family"])
        groups[info["family"]].append(info)

    faces, families = [], []
    for i, family in enumerate(order):
        alias = "SHK%d" % i
        families.append('"%s"' % alias)
        for info in groups[family]:
            fmt = "opentype" if info["path"].lower().endswith(".otf") else "truetype"
            faces.append(
                '@font-face { font-family: "%s"; font-weight: %d; font-style: normal;\n'
                '             src: url("%s") format("%s"); }'
                % (alias, info["weight"], to_file_url(info["path"]), fmt))

    for name, path in image_urls.items():
        fragment = fragment.replace('src="%s"' % name, 'src="%s"' % to_file_url(path))

    # 縦書きは writing-mode をルート（html）に置く。shashoku は writing-mode を文書全体で
    # 1 つしか持たない（A1）ので、断片の div だけを縦にすると紙面の向きが食い違う。
    root_wm = "vertical-rl" if vertical else "horizontal-tb"
    return """<!DOCTYPE html>
<html lang="ja"><head><meta charset="utf-8"><title>shashoku compare</title>
<style>
%(faces)s
html { writing-mode: %(wm)s; }
html, body { margin: 0; padding: 0; }
body {
  font-family: %(families)s;
  font-size: 16px;
  color: #000;
  background: #fff;
  line-break: strict;
  font-synthesis: none;
  font-kerning: auto;
}
/* shashoku の UA スタイルシートに合わせる（src/style/ua_stylesheet.cpp） */
h1 { font-size: 2em;    font-weight: bold; margin: 0.67em 0 }
h2 { font-size: 1.5em;  font-weight: bold; margin: 0.83em 0 }
h3 { font-size: 1.17em; font-weight: bold; margin: 1em 0 }
h4 { font-size: 1em;    font-weight: bold; margin: 1.33em 0 }
h5 { font-size: 0.83em; font-weight: bold; margin: 1.67em 0 }
h6 { font-size: 0.67em; font-weight: bold; margin: 2.33em 0 }
p { margin: 1em 0 }
rt { font-size: 0.5em }
/* shashoku はルビ組の中で親文字を中央に寄せる（#15 のケース 5 の実測） */
ruby { ruby-align: center; }
/* 紙面をウィンドウではなく CSS で決める。WSL から呼ぶ chrome.exe は --window-size より
   小さいウィンドウを作れず（幅は 500px 未満にならない）、--dump-dom のときと
   --screenshot のときでレイアウトの viewport も食い違う。position:absolute で
   物理的な左上に固定すると、どちらの経路でも同じ組版になり、スクリーンショットの
   左上が shashoku の PNG と同じ範囲になる。 */
#shk-root { position: absolute; left: 0; top: 0; width: %(width)dpx;%(root_height)s }
</style></head>
<body><div id="shk-root">%(fragment)s</div>
<script>
%(measure)s
</script>
<script>
document.fonts.ready.then(function () {
  window.__shkMeasure({ vertical: %(vertical)s, width: %(width)d, height: %(height)s });
});
</script>
</body></html>
""" % {
        "faces": "\n".join(faces),
        "families": ", ".join(families),
        "wm": root_wm,
        "width": width,
        # height を指定しないケースでは、紙面の高さは Chrome 自身のルートの広がりにする
        # （shashoku の PNG の高さを当てると、行高の丸め方の違いがそのまま「紙面の外」に出る）。
        "height": "null" if height is None else str(int(height)),
        # 縦書きは height が inline 方向（行長）なので、shashoku の --height と合わせる。
        "root_height": (" height: %dpx;" % height) if (vertical and height) else "",
        "fragment": fragment,
        "measure": measure_js,
        "vertical": "true" if vertical else "false",
    }


# ウィンドウの上限。ケース 20（padding: 1e38em）では Chrome が 2^25 px で飽和させるので、
# その高さを素直に渡すと chrome.exe が起動に失敗する（exit 8）。
MAX_WINDOW = 4000


def window_size(width, height):
    """chrome.exe に渡すウィンドウの大きさ。

    WSL から呼ぶと幅 500 px 未満のウィンドウを作れず、小さすぎるウィンドウでは内容が
    描かれないことがある。紙面は CSS 側（`#shk-root`）で決めているので、ウィンドウは
    常に大きめにして、スクリーンショットは後から切り出す。
    """
    return (min(max(int(width) + 60, 560), MAX_WINDOW),
            min(max(int(height) + 200, 400), MAX_WINDOW))


def chrome_args(chrome, profile_dir, width, height):
    width, height = window_size(width, height)
    return [
        chrome,
        "--headless",
        "--disable-gpu",
        "--no-sandbox",
        "--hide-scrollbars",
        "--force-device-scale-factor=1",
        "--force-color-profile=srgb",
        "--window-size=%d,%d" % (width, height),
        "--user-data-dir=%s" % to_windows_path(profile_dir),
        "--virtual-time-budget=10000",
    ]


def launch_chrome(args, timeout):
    if os.environ.get("SHASHOKU_COMPARE_DEBUG"):
        print("  chrome: %s" % " ".join(args), file=sys.stderr)
    return subprocess.run(args, capture_output=True, timeout=timeout)


def run_chrome(chrome, page_path, profile_dir, width, height, timeout=180):
    """ページ内の measure.js が書き出した測定値を取り出す（--dump-dom）。

    スクリーンショットは別の起動にする（`--screenshot` と `--dump-dom` を同時に渡すと
    PNG が書かれないことがあり、切り出しのためにウィンドウの大きさも変えたいので）。
    """
    args = chrome_args(chrome, profile_dir, width, height)
    args.append("--dump-dom")
    args.append(to_file_url(page_path))
    proc = launch_chrome(args, timeout)
    dom = proc.stdout.decode("utf-8", "replace")
    # base64 の文字だけを拾う（ページの中には measure.js の原文も入っているため）。
    pattern = r'id="shk-result"[^<>]*?data-json="([A-Za-z0-9+/=]+)"'
    found = re.findall(pattern, dom) or re.findall(
        r'data-json="([A-Za-z0-9+/=]+)"[^<>]*?id="shk-result"', dom)
    match = found[-1] if found else None
    if match is None:
        raise RuntimeError("Chrome の測定結果が取れなかった（フォント読み込みで止まった？）\n"
                           + proc.stderr.decode("utf-8", "replace")[-2000:])
    return json.loads(base64.b64decode(match).decode("utf-8"))


def screenshot(chrome, page_path, profile_dir, width, height, screenshot_path, timeout=180):
    """大きめに撮ってから width×height に切り出す。撮れたかどうかを返す。"""
    args = chrome_args(chrome, profile_dir, width, height)
    args.append("--screenshot=%s" % to_windows_path(screenshot_path))
    args.append(to_file_url(page_path))
    proc = launch_chrome(args, timeout)
    if os.path.exists(screenshot_path):
        png_crop(screenshot_path, int(width), int(height))
        return True, ""
    tail = proc.stderr.decode("utf-8", "replace").strip().splitlines()[-3:]
    return False, "exit %d: %s" % (proc.returncode, " / ".join(tail))


# ---------------------------------------------------------------- フォントの確認
PROBE_FRAGMENT = ('<div id="probe-latin" style="font-size:20px">Hamburgefonstiv</div>'
                  '<div id="probe-cjk" style="font-size:20px">春はあけぼの</div>')


def verify_font(ctx):
    """Chrome 側で本当に同じフォントが使われたかを確かめる。

    document.fonts の status だけだと、ある文字だけシステムフォントに落ちた場合を見逃す。
    そこで同じ文字列の字送りを shashoku と突き合わせる（欧文は字幅がフォントごとに大きく違う）。
    """
    probe_dir = os.path.join(ctx.out, "_fontprobe")
    os.makedirs(probe_dir, exist_ok=True)
    src = os.path.join(probe_dir, "probe.html")
    with open(src, "w", encoding="utf-8") as f:
        f.write(PROBE_FRAGMENT + "\n")

    dump = run_shashoku_dump(ctx, src, ctx.fonts, 400, None)
    shk = parse_box_dump(dump, ctx.fonts)

    page = os.path.join(probe_dir, "chrome.html")
    with open(page, "w", encoding="utf-8") as f:
        f.write(build_chrome_page(PROBE_FRAGMENT, ctx.fonts, 400, None, False, ctx.measure_js, {}))
    chrome = run_chrome(ctx.chrome, page, ctx.profile, 400, 200)

    shk_lines = [(line_text(ln), ln) for _, ln in all_lines(shk)]
    ch_lines = [(line_text(ln), ln) for _, ln in all_lines(chrome)]
    report = {
        "fonts": chrome.get("fonts", []),
        "fonts_status": chrome.get("fonts_status"),
        "measurements": [],
        "ok": chrome.get("fonts_status") == "loaded"
        and all(f["status"] == "loaded" for f in chrome.get("fonts", [])),
    }
    for (ta, la), (tb, lb) in zip(shk_lines, ch_lines):
        wa = max(it["inline_end"] for it in la["items"]) - min(
            it["inline_start"] for it in la["items"])
        wb = max(it["inline_end"] for it in lb["items"]) - min(
            it["inline_start"] for it in lb["items"])
        ok = ta == tb and abs(wa - wb) <= 0.2
        report["measurements"].append(
            {"text": ta, "shashoku_width": round(wa, 3), "chrome_width": round(wb, 3), "ok": ok})
        report["ok"] = report["ok"] and ok
    return report


# ---------------------------------------------------------------- shashoku の起動
def shashoku_args(ctx, html, font_infos, width, height, images):
    args = [ctx.shashoku, html]
    for info in font_infos:
        args += ["--font", info["path"]]
    for name, path in (images or {}).items():
        args += ["--image", "%s=%s" % (name, path)]
    args += ["--width", str(width)]
    if height:
        args += ["--height", str(height)]
    return args


def try_shashoku_dump(ctx, html, font_infos, width, height, images=None, stage="box"):
    """(returncode, stdout, stderr) をそのまま返す。入力を拒否させたいケース用。"""
    args = shashoku_args(ctx, html, font_infos, width, height, images)
    args += ["--dump-stage", stage]
    proc = subprocess.run(args, capture_output=True, text=True)
    return proc.returncode, proc.stdout, proc.stderr.strip()


def run_shashoku_dump(ctx, html, font_infos, width, height, images=None, stage="box"):
    code, out, err = try_shashoku_dump(ctx, html, font_infos, width, height, images, stage)
    if code != 0:
        raise RuntimeError("shashoku が失敗した (exit %d):\n%s" % (code, err))
    return json.loads(out)


def run_shashoku_png(ctx, html, font_infos, width, height, out_png, images=None):
    args = shashoku_args(ctx, html, font_infos, width, height, images)
    args += ["-o", out_png]
    proc = subprocess.run(args, capture_output=True, text=True)
    return proc.returncode, proc.stderr.strip()


# ---------------------------------------------------------------- 報告
def format_table(rows):
    header = ["#", "ケース"] + JUDGMENTS
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]

    def line(cells, fill="-", joint="-+-"):
        return joint.join(fill * w for w in widths) if cells is None else \
            " | ".join(str(c).ljust(w) for c, w in zip(cells, widths))

    out = [line(header), line(None)]
    out += [line(r) for r in rows]
    return "\n".join(out)


MARK = {"same": "OK", "diff": "差", "unknown": "??", "error": "失敗", "expected": "期待"}


def handle_expect_error(ctx, case, entry, rows, results, case_dir, src,
                        font_infos, width, height, images, expect_error, args):
    """shashoku が入力を拒否するのが正しいケースを扱う（cases.json の expect_error）。

    ここは「組版の比較」ではなく「入力を拒否できたか」の検査。Chrome 側は参考として
    測り、飽和させて描いた結果を記録する（#19 のケース 20）。
    """
    code, _, err = try_shashoku_dump(ctx, src, font_infos, width, height, images)
    ok = code != 0 and expect_error in err
    entry["shashoku_exit"] = code
    entry["shashoku_stderr"] = err
    if ok:
        cell = "入力を拒否（期待どおり）"
        summary = "shashoku は入力を拒否した（期待どおり）: %s" % err.splitlines()[0]
        verdict = "expected"
    elif code != 0:
        cell = "別のエラーで止まった"
        summary = "エラーにはなったが %s ではない: %s" % (expect_error, err.splitlines()[0])
        verdict = "diff"
    else:
        cell = "拒否されず exit 0"
        summary = "%s で止まるはずが exit 0 で通った" % expect_error
        verdict = "diff"
        entry["errors"].append(summary)

    chrome_note = None
    if not args.no_chrome:
        try:
            page = os.path.join(case_dir, "chrome.html")
            with open(src, encoding="utf-8") as f:
                fragment = f.read()
            with open(page, "w", encoding="utf-8") as f:
                f.write(build_chrome_page(fragment, font_infos, width, height,
                                          False, ctx.measure_js, images or {}))
            shot = os.path.join(case_dir, "chrome.png")
            chrome_raw = run_chrome(ctx.chrome, page, ctx.profile, width, height or 400)
            with open(os.path.join(case_dir, "chrome.json"), "w", encoding="utf-8") as f:
                json.dump(chrome_raw, f, ensure_ascii=False, indent=1)
            screenshot(ctx.chrome, page, ctx.profile, width, height or 400, shot)
            chrome = {"vertical": chrome_raw["vertical"], "viewport": chrome_raw["viewport"],
                      "blocks": chrome_raw["blocks"], "notes": []}
            chrome_note = {
                "root_rect": chrome_raw.get("root_rect"),
                "lines": [line_text(ln) for _, ln in all_lines(chrome)],
            }
        except Exception as exc:  # noqa: BLE001
            entry["errors"].append("chrome: %s" % exc)

    for name in JUDGMENTS:
        entry["judgments"][name] = verdict
        entry["details"][name] = {"summary": summary,
                                  "detail": {"chrome": chrome_note} if chrome_note else {}}
    rows.append([case["id"], case["title"]] + ["%s %s" % (MARK[verdict], cell)] * 4)
    results.append(entry)


def main():
    parser = argparse.ArgumentParser(description="shashoku と Chrome（headless）を比べる")
    parser.add_argument("--shashoku", default=os.environ.get("SHASHOKU_BIN"),
                        help="shashoku の CLI（既定: $SHASHOKU_BIN）")
    parser.add_argument("--chrome", default=os.environ.get("SHASHOKU_CHROME"),
                        help="chrome.exe（既定: $SHASHOKU_CHROME）")
    parser.add_argument("--fonts", default=os.environ.get("SHASHOKU_FONTS_DIR"),
                        help="フォントのディレクトリ（既定: $SHASHOKU_FONTS_DIR）")
    parser.add_argument("--cases", default=os.path.join(HERE, "cases"))
    parser.add_argument("--out", default=os.path.join(REPO, "build", "compare"))
    parser.add_argument("--only", default=None, help="ケース番号をカンマ区切りで")
    parser.add_argument("--no-chrome", action="store_true", help="shashoku 側だけ出す")
    parser.add_argument("--no-images", action="store_true", help="左右に並べた PNG を作らない")
    args = parser.parse_args()

    if not args.shashoku:
        parser.error("--shashoku か $SHASHOKU_BIN が要る")
    if not args.fonts:
        parser.error("--fonts か $SHASHOKU_FONTS_DIR が要る")
    if not args.no_chrome and not args.chrome:
        parser.error("--chrome か $SHASHOKU_CHROME が要る（--no-chrome なら不要）")

    with open(os.path.join(args.cases, "cases.json"), encoding="utf-8") as f:
        catalog = json.load(f)
    wanted = None
    if args.only:
        wanted = {int(x) for x in args.only.split(",")}

    os.makedirs(args.out, exist_ok=True)
    image_path = os.path.join(args.out, "box.png")
    make_box_png(image_path)

    class Ctx:
        pass

    ctx = Ctx()
    ctx.shashoku = args.shashoku
    ctx.chrome = args.chrome
    ctx.out = args.out
    # examples/og_card.html は `icon` という名前で引くので同じ画像を両方の名前で渡す。
    ctx.images = {"box": image_path, "icon": image_path}
    # **ユーザーの普段の Chrome のプロファイルには触らない。** 実行ごとに使い捨ての
    # ディレクトリを作り、終わったら消す（前の実行が残したロックを引きずらないよう
    # プロセス ID を付ける）。Windows 側には何も置かない。
    ctx.profile = os.path.join(args.out, "chrome-profile-%d" % os.getpid())
    os.makedirs(ctx.profile, exist_ok=True)
    with open(os.path.join(HERE, "measure.js"), encoding="utf-8") as f:
        ctx.measure_js = f.read()

    font_cache = {}

    def fonts_for(keys):
        out = []
        for key in keys:
            if key not in font_cache:
                font_cache[key] = read_font_info(os.path.join(args.fonts, FONT_FILES[key]))
            out.append(font_cache[key])
        return out

    ctx.fonts = fonts_for(["jp"])

    font_report = None
    if not args.no_chrome:
        print("フォントの確認 …", file=sys.stderr)
        font_report = verify_font(ctx)
        print("  -> %s" % ("同じフォントで描かれている" if font_report["ok"]
                           else "**Chrome 側が別のフォントで描いた疑いがある**"), file=sys.stderr)

    rows, results = [], []
    for case in catalog["cases"]:
        if wanted is not None and case["id"] not in wanted:
            continue
        print("ケース %2d: %s" % (case["id"], case["title"]), file=sys.stderr)
        case_dir = os.path.join(args.out, "%02d" % case["id"])
        os.makedirs(case_dir, exist_ok=True)
        src = os.path.join(args.cases, case["file"])
        font_infos = fonts_for(case.get("fonts", ["jp"]))
        width, height = case["width"], case.get("height")

        images = ctx.images if case.get("images") else None

        entry = {"case": case, "judgments": {}, "details": {}, "errors": []}

        # 「shashoku が入力を拒否するのが正しい」ケース（cases.json の expect_error）。
        # 比較の対象にはせず、拒否できたかどうかと、Chrome が同じ入力をどう扱うかを記録する。
        expect_error = case.get("expect_error")
        if expect_error:
            handle_expect_error(ctx, case, entry, rows, results, case_dir, src,
                                font_infos, width, height, images, expect_error, args)
            continue

        try:
            dump = run_shashoku_dump(ctx, src, font_infos, width, height, images)
            with open(os.path.join(case_dir, "box.json"), "w", encoding="utf-8") as f:
                json.dump(dump, f, ensure_ascii=False, indent=1)
            # viewport_height は「内容に追従」を表す正当な null なので、木の中だけを見る。
            non_finite = non_finite_paths(dump["root"], "root")
            shk = None if non_finite else parse_box_dump(dump, font_infos)
        except Exception as exc:  # noqa: BLE001 - ローカル専用スクリプト
            entry["errors"].append("shashoku: %s" % exc)
            rows.append([case["id"], case["title"]] + ["失敗"] * 4)
            results.append(entry)
            continue

        if non_finite:
            # 座標に非有限値が出ているので数値の比較はできない。これ自体が判定結果。
            entry["non_finite"] = non_finite
            entry["errors"].append(
                "box ダンプの座標が非有限（JSON では null）: %s ほか %d 箇所"
                % (", ".join(non_finite[:4]), len(non_finite)))

        shk_png = os.path.join(case_dir, "shashoku.png")
        code, err = run_shashoku_png(ctx, src, font_infos, width, height, shk_png, images)
        entry["shashoku_png_exit"] = code
        if code != 0:
            entry["errors"].append("shashoku -o: exit %d: %s" % (code, err))
        png_w, png_h = width, (height or 400)
        if code == 0:
            png_w, png_h = png_size(shk_png)

        if args.no_chrome:
            rows.append([case["id"], case["title"]] + ["-"] * 4)
            results.append(entry)
            continue

        try:
            page = os.path.join(case_dir, "chrome.html")
            with open(src, encoding="utf-8") as f:
                fragment = f.read()
            vertical = dump.get("writing_mode", "horizontal-tb").startswith("vertical")
            with open(page, "w", encoding="utf-8") as f:
                f.write(build_chrome_page(fragment, font_infos, width, height,
                                          vertical, ctx.measure_js, images or {}))
            shot = os.path.join(case_dir, "chrome.png")
            chrome_raw = run_chrome(ctx.chrome, page, ctx.profile, png_w, png_h)
            with open(os.path.join(case_dir, "chrome.json"), "w", encoding="utf-8") as f:
                json.dump(chrome_raw, f, ensure_ascii=False, indent=1)
            chrome = {"vertical": chrome_raw["vertical"],
                      "viewport": chrome_raw["viewport"],
                      "blocks": chrome_raw["blocks"],
                      "notes": chrome_raw.get("warnings", []),
                      "ruby_groups": [g for blk in chrome_raw["blocks"]
                                      for g in blk.get("ruby_groups", [])]}
        except Exception as exc:  # noqa: BLE001
            entry["errors"].append("chrome: %s" % exc)
            rows.append([case["id"], case["title"]] + ["失敗"] * 4)
            results.append(entry)
            continue

        if shk is None:
            for name in JUDGMENTS:
                entry["judgments"][name] = "unknown"
                entry["details"][name] = {
                    "summary": "box の座標が非有限（null）で比較不能",
                    "detail": {"non_finite": entry.get("non_finite", [])[:8],
                               "chrome_lines": [line_text(ln) for _, ln in all_lines(chrome)]}}
            entry["notes"] = chrome["notes"]
        else:
            for name, fn in (("欠落", judge_missing), ("重なり", judge_overlap),
                             ("改行位置", judge_linebreak), ("はみ出し", judge_overflow)):
                verdict, summary, detail = fn(shk, chrome)
                entry["judgments"][name] = verdict
                entry["details"][name] = {"summary": summary, "detail": detail}
            entry["notes"] = shk["notes"] + chrome["notes"]
        rows.append([case["id"], case["title"]]
                    + ["%s %s" % (MARK[entry["judgments"][n]], entry["details"][n]["summary"])
                       for n in JUDGMENTS])
        results.append(entry)

        # Chrome の方が背が高くなるケース（行高の丸めの違いや、折り返しが増えたとき）では、
        # shashoku の PNG の高さで撮ると内容が切れて絵が読めない。Chrome 自身のルートが
        # 入る高さで撮り直す（並べたときの左上の範囲は shashoku の PNG と同じまま）。
        chrome_root = chrome_raw.get("root_rect", {})
        chrome_h = chrome_root.get("inline_end" if chrome_raw["vertical"] else "block_end", 0)
        shot_h = min(max(png_h, int(chrome_h) + 1), MAX_WINDOW)
        ok, why = screenshot(ctx.chrome, page, ctx.profile, png_w, shot_h, shot)
        if not ok:
            entry["errors"].append("chrome: スクリーンショットが書かれなかった（%s）" % why)
        if os.path.exists(shot) and not args.no_images and os.path.exists(shk_png):
            try:
                png_side_by_side(shk_png, shot, os.path.join(case_dir, "side-by-side.png"))
            except Exception as exc:  # noqa: BLE001
                entry["errors"].append("side-by-side: %s" % exc)

    write_reports(args.out, rows, results, font_report)
    shutil.rmtree(ctx.profile, ignore_errors=True)
    print("\n" + format_table(rows))
    print("\n報告: %s" % os.path.join(args.out, "report.txt"))
    return 0


def write_reports(out_dir, rows, results, font_report):
    lines = ["shashoku ↔ Chrome（headless）比較",
             "=" * 60, ""]
    if font_report:
        lines.append("フォントの確認: %s" % ("OK（同じフォントで描かれている）" if font_report["ok"]
                                            else "NG（Chrome 側が別のフォントで描いた疑い）"))
        for face in font_report["fonts"]:
            lines.append("  @font-face %s / weight %s / %s"
                         % (face["family"], face["weight"], face["status"]))
        for m in font_report["measurements"]:
            lines.append("  %r の幅: shashoku %.3f / Chrome %.3f -> %s"
                         % (m["text"], m["shashoku_width"], m["chrome_width"],
                            "一致" if m["ok"] else "不一致"))
        lines.append("")
    lines.append(format_table(rows))
    lines.append("")
    lines.append("凡例: OK = 差なし / 差 = 差あり / ?? = 比較不能 / 期待 = 拒否されるのが正しい入力 /"
                 " 失敗 = 実行できず")
    lines.append("")
    lines.append("-" * 60)
    for entry in results:
        case = entry["case"]
        lines.append("")
        lines.append("## ケース %d: %s（%s）" % (case["id"], case["title"], case["file"]))
        if case.get("known_bug"):
            lines.append("既知の不具合: %s" % case["known_bug"])
        for err in entry["errors"]:
            lines.append("  !! %s" % err)
        for note in entry.get("notes", []):
            lines.append("  注: %s" % note)
        for name in JUDGMENTS:
            if name not in entry["details"]:
                continue
            info = entry["details"][name]
            lines.append("  [%s] %s %s" % (name, MARK[entry["judgments"][name]], info["summary"]))
            for key, value in info["detail"].items():
                if value in (None, [], 0, 0.0):
                    continue
                # ルビの中央寄せは差が無くても記録する（#15 の「Chrome がどこに置くか」）。
                if entry["judgments"][name] == "same" and not key.startswith("ruby_centering"):
                    continue
                lines.append("      %s: %s" % (key, value))
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out_dir, "report.txt"), "w", encoding="utf-8") as f:
        f.write(text)
    write_html_report(out_dir, rows, results, font_report)


def write_html_report(out_dir, rows, results, font_report):
    def esc(s):
        return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))

    parts = ["<!DOCTYPE html><html lang='ja'><head><meta charset='utf-8'>",
             "<title>shashoku ↔ Chrome 比較</title><style>",
             "body{font-family:sans-serif;margin:24px;color:#222}",
             "table{border-collapse:collapse;margin-bottom:24px}",
             "th,td{border:1px solid #ccc;padding:4px 8px;font-size:13px;text-align:left}",
             ".same{background:#e8f5e9}.diff{background:#ffebee}",
             ".unknown{background:#fff8e1}.error{background:#eceff1}",
             ".expected{background:#e3f2fd}",
             "figure{margin:0 0 8px 0}img{border:1px solid #ddd;vertical-align:top}",
             ".pair{display:flex;gap:16px;align-items:flex-start;overflow-x:auto}",
             "pre{background:#f7f7f7;padding:8px;font-size:12px;overflow-x:auto}",
             "</style></head><body>",
             "<h1>shashoku ↔ Chrome（headless）比較</h1>"]
    if font_report:
        parts.append("<p><b>フォントの確認</b>: %s</p>"
                     % ("OK（同じフォントで描かれている）" if font_report["ok"]
                        else "<span style='color:#c00'>NG（別のフォントの疑い）</span>"))
    parts.append("<table><tr><th>#</th><th>ケース</th>"
                 + "".join("<th>%s</th>" % j for j in JUDGMENTS) + "</tr>")
    for entry in results:
        case = entry["case"]
        parts.append("<tr><td><a href='#c%d'>%d</a></td><td>%s</td>"
                     % (case["id"], case["id"], esc(case["title"])))
        for name in JUDGMENTS:
            verdict = entry["judgments"].get(name, "error")
            info = entry["details"].get(name, {"summary": "-"})
            parts.append("<td class='%s'>%s %s</td>"
                         % (verdict, MARK[verdict], esc(info["summary"])))
        parts.append("</tr>")
    parts.append("</table>")

    for entry in results:
        case = entry["case"]
        d = "%02d" % case["id"]
        parts.append("<h2 id='c%d'>ケース %d: %s</h2>" % (case["id"], case["id"], esc(case["title"])))
        if case.get("known_bug"):
            parts.append("<p>既知の不具合: %s</p>" % esc(case["known_bug"]))
        parts.append("<div class='pair'>"
                     "<figure><figcaption>shashoku</figcaption>"
                     "<img src='%s/shashoku.png'></figure>"
                     "<figure><figcaption>Chrome</figcaption>"
                     "<img src='%s/chrome.png'></figure></div>" % (d, d))
        parts.append("<pre>%s</pre>" % esc(json.dumps(
            {"judgments": entry["judgments"], "details": entry["details"],
             "errors": entry["errors"]}, ensure_ascii=False, indent=1)))
    parts.append("</body></html>")
    with open(os.path.join(out_dir, "report.html"), "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


if __name__ == "__main__":
    sys.exit(main())
