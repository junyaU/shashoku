#!/usr/bin/env python3
"""shashoku の CLI を呼び、診断 JSON を読み、直して再試行する最小の実例。

呼び出し側のアプリがどう書くかだけを示します。HTTP サービスでも、汎用の
エージェント基盤でもありません。依存は python3 の標準ライブラリだけです。

    1. HTML を作る（``generate``。ここが LLM の差し替え口）
    2. CLI を子プロセスで ``--diagnostics json --strict`` で呼ぶ
    3. 失敗したら診断を ``generate`` に渡してもう一度作らせる
    4. 上限 N 回（既定 3）で諦め、診断ごと呼び出し側に返す

shashoku 自身は AI を呼びません。何回まで回すか、諦めたときに何を返すかは
呼び出し側のアプリの責任です（DESIGN.md §0）。

実行例（リポジトリの直下で）:

    python3 examples/server/render_with_retry.py \\
        --shashoku build/release/tools/shashoku/shashoku --out out.png
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from html import escape as html_escape
from typing import Any, Callable, Dict, List, Mapping, Optional, Sequence

# CLI の終了コード（README「使い方」/ ガイド §6.1）。
EXIT_OK = 0
EXIT_DIAGNOSTICS = 1  # レンダリングの失敗（診断 JSON が出る）か、入出力の失敗（出ない）
EXIT_USAGE = 2  # 引数の誤り。診断ではないので再試行しない

# 診断を読んで機械的に直せる種類。ここに無い種類は諦めて LLM に投げ直します。
FIXABLE_KINDS = ("unsupported-property", "unsupported-value")


class ShashokuError(RuntimeError):
    """診断ではない失敗。

    引数の誤り（終了コード 2）、入出力の失敗（入力を読めない・出力を書けない）、
    起動できない、時間切れ。どれも HTML を書き直しても直らないので、
    **再試行せずにそのまま失敗させます**。
    """

    def __init__(self, message: str, *, exit_code: Optional[int] = None, stderr: str = ""):
        super().__init__(message)
        self.exit_code = exit_code
        self.stderr = stderr


@dataclass
class RenderResult:
    """``render_with_retry`` の結果。"""

    ok: bool
    path: Optional[str]  # 成功したときだけ、目的の名前になった PNG のパス
    attempts: int  # 実際に CLI を呼んだ回数
    html: str  # 最後に生成した HTML（諦めたときの調査用）
    diagnostics: Dict[str, Any] = field(default_factory=dict)  # 最後の診断 JSON
    advice: str = ""  # 諦めたときに呼び出し側へ返す一言


def run_shashoku(
    shashoku: str,
    html: str,
    out_path: str,
    *,
    width: Optional[int] = None,
    height: Optional[int] = None,
    fonts: Sequence[str] = (),
    images: Optional[Mapping[str, str]] = None,
    strict: bool = True,
    extra_args: Sequence[str] = (),
    timeout: float = 30.0,
) -> Dict[str, Any]:
    """CLI を 1 回呼び、診断 JSON（辞書）を返す。

    返り値は ``{"ok": bool, "width": ..., "height": ..., "truncated": bool,
    "errors": [...], "warnings": [...]}``（ガイド §6.4）。``ok`` が真なら
    ``out_path`` に PNG が書かれています。

    終了コードの扱い:

    * 0 … 成功。JSON の ``ok`` は真
    * 1 … 診断あり（JSON を読む）。ただし **入出力の失敗のときは JSON が出ない**ので、
      その場合は ``ShashokuError``
    * それ以外（2 = 引数の誤りなど）… 診断ではないので ``ShashokuError``
    """
    html_dir = tempfile.mkdtemp(prefix="shashoku-html-")
    try:
        html_path = os.path.join(html_dir, "input.html")
        with open(html_path, "w", encoding="utf-8") as handle:
            handle.write(html)

        argv = [shashoku, html_path, "-o", out_path, "--diagnostics", "json"]
        if strict:
            argv.append("--strict")
        if width is not None:
            argv += ["--width", str(width)]
        if height is not None:
            argv += ["--height", str(height)]
        for font in fonts:
            argv += ["--font", font]
        for name, path in (images or {}).items():
            argv += ["--image", "{}={}".format(name, path)]
        argv += list(extra_args)

        try:
            proc = subprocess.run(argv, capture_output=True, timeout=timeout, check=False)
        except FileNotFoundError as exc:
            raise ShashokuError("shashoku を起動できません: {}".format(shashoku)) from exc
        except subprocess.TimeoutExpired as exc:
            raise ShashokuError("shashoku が {} 秒で終わりませんでした".format(timeout)) from exc
    finally:
        shutil.rmtree(html_dir, ignore_errors=True)

    stderr = proc.stderr.decode("utf-8", "replace")

    if proc.returncode not in (EXIT_OK, EXIT_DIAGNOSTICS):
        raise ShashokuError(
            "shashoku の呼び方が誤っています（終了コード {}）".format(proc.returncode),
            exit_code=proc.returncode,
            stderr=stderr,
        )

    try:
        diagnostics = json.loads(proc.stdout.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        # 終了コード 1 でも JSON が無いのは入出力の失敗（入力を読めない・出力を書けない）。
        # 診断ではないので再試行しません。
        first_line = stderr.strip().splitlines()[0] if stderr.strip() else "診断が出ませんでした"
        raise ShashokuError(first_line, exit_code=proc.returncode, stderr=stderr) from exc

    return diagnostics


def render_with_retry(
    generate: Callable[[Any, Optional[Dict[str, Any]]], str],
    request: Any,
    *,
    shashoku: str,
    out_path: str,
    max_attempts: int = 3,
    width: Optional[int] = None,
    height: Optional[int] = None,
    fonts: Sequence[str] = (),
    images: Optional[Mapping[str, str]] = None,
    strict: bool = True,
    extra_args: Sequence[str] = (),
    timeout: float = 30.0,
    log=None,
) -> RenderResult:
    """HTML を作り、描き、失敗したら診断を渡して作り直す。上限 ``max_attempts`` 回。

    ``generate(request, diagnostics)`` は HTML の文字列を返します。``diagnostics`` は
    初回だけ ``None``、以降は直前の診断 JSON（``errors`` の各要素は ``kind`` / ``message``
    / ``hint`` / ``line`` / ``column`` / ``offset`` / ``warning``）。**ここが LLM の
    差し替え口**です。

    出力は一時ファイルに書き、成功したときだけ ``out_path`` に rename します。CLI 自身も
    失敗時には出力を書きませんが、呼び出し側でも同じ規律を示しています。諦めたときに
    ``out_path`` が作られたり上書きされたりすることはありません。

    診断ではない失敗（``ShashokuError``）は再試行せずそのまま送出します。
    """
    if max_attempts < 1:
        raise ValueError("max_attempts は 1 以上にしてください")

    out_dir = os.path.dirname(os.path.abspath(out_path))
    diagnostics: Optional[Dict[str, Any]] = None
    html = ""

    for attempt in range(1, max_attempts + 1):
        html = generate(request, diagnostics)

        handle, tmp_png = tempfile.mkstemp(dir=out_dir, prefix=".shashoku-", suffix=".png")
        os.close(handle)
        try:
            diagnostics = run_shashoku(
                shashoku,
                html,
                tmp_png,
                width=width,
                height=height,
                fonts=fonts,
                images=images,
                strict=strict,
                extra_args=extra_args,
                timeout=timeout,
            )
            if diagnostics.get("ok"):
                os.replace(tmp_png, out_path)  # 成功したときだけ目的の名前にする
                tmp_png = ""
                _log(
                    log,
                    "attempt {}/{}: ok, wrote {} ({}x{})".format(
                        attempt,
                        max_attempts,
                        out_path,
                        diagnostics.get("width"),
                        diagnostics.get("height"),
                    ),
                )
                return RenderResult(
                    ok=True, path=out_path, attempts=attempt, html=html, diagnostics=diagnostics
                )
        finally:
            if tmp_png:
                _remove_quietly(tmp_png)

        _log(
            log,
            "attempt {}/{}: {} error(s)".format(
                attempt, max_attempts, len(diagnostics.get("errors", []))
            ),
        )
        for line in format_diagnostics(diagnostics):
            _log(log, "  " + line)

    diagnostics = diagnostics or {}
    advice = _advice(diagnostics, max_attempts)
    _log(log, advice)
    return RenderResult(
        ok=False,
        path=None,
        attempts=max_attempts,
        html=html,
        diagnostics=diagnostics,
        advice=advice,
    )


def format_diagnostics(diagnostics: Mapping[str, Any]) -> List[str]:
    """診断を、LLM にも人にも渡せる行の並びにする（CLI の人向け出力と同じ形）。

    機械が頼ってよいのは ``kind`` / ``warning`` / ``edge`` の識別子と位置
    （``line`` / ``column`` / ``offset``。分からなければ ``null``）です。
    ``message`` / ``detail`` / ``hint`` の文面は版で変わりえます（ガイド §6.1）。
    """
    lines: List[str] = []
    for error in diagnostics.get("errors", []):
        lines.append(
            "error[{}]{}: {}".format(
                error.get("kind"), _at(error), error.get("message", "")
            )
        )
        if error.get("hint"):
            lines.append("  hint: {}".format(error["hint"]))
    for warning in diagnostics.get("warnings", []):
        lines.append(
            "warning[{}]: {}{}".format(
                warning.get("kind"), warning.get("detail", ""), _at(warning)
            )
        )
    if diagnostics.get("truncated"):
        lines.append("note: 診断が上限に達して打ち切られました（残りは直したあとに出ます）")
    return lines


def _at(entry: Mapping[str, Any]) -> str:
    line = entry.get("line")
    if line is None:
        return ""
    return " at {}:{}".format(line, entry.get("column"))


def _advice(diagnostics: Mapping[str, Any], max_attempts: int) -> str:
    if diagnostics.get("truncated"):
        return (
            "診断が打ち切られています（既定 100 件）。上限を上げるか、入力を分けて"
            "小さくしてからもう一度試してください"
        )
    return "{} 回試しても診断が残りました。診断を添えて呼び出し側に失敗を返します".format(
        max_attempts
    )


def _log(log, message: str) -> None:
    if log is not None:
        print(message, file=log)


def _remove_quietly(path: str) -> None:
    try:
        os.remove(path)
    except OSError:
        pass


# ---------------------------------------------------------------------------
# ここから下は「差し替え口」のダミー。本番では LLM を呼ぶ実装に置き換えます。
# ---------------------------------------------------------------------------

# 対応外のプロパティを 3 つ（うち 1 つは hint つき）わざと混ぜたテンプレート。
# 実際の LLM もこの手の宣言を書くので、1 回目は必ず失敗します。
CARD_TEMPLATE = """<style>
  .card  { padding: 24px; border: 1px solid #dfe3e8; border-radius: 12px; background: #ffffff;
           box-sizing: border-box; box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08); }
  .title { margin: 0 0 8px 0; font-size: 20px; font-weight: bold; color: #1b2733;
           text-transform: uppercase; }
  .body  { margin: 0; font-size: 15px; line-height: 1.8; color: #44546a; }
</style>
<div class="card">
  <p class="title">{{title}}</p>
  <p class="body">{{body}}</p>
</div>
"""


def fill_template(template: str, values: Mapping[str, Any]) -> str:
    """テンプレートの ``{{name}}`` を値で置き換える。

    値は必ず HTML エスケープします。``&`` や ``<`` をそのまま流し込むと
    ``html-parse`` エラーになり、しかも再試行では直りません（ガイド §3）。
    """
    out = template
    for key, value in values.items():
        out = out.replace("{{" + key + "}}", html_escape(str(value)))
    return out


def drop_reported_declarations(source: str, diagnostics: Mapping[str, Any]) -> str:
    """診断の ``offset`` を頼りに、対応外の CSS 宣言を機械的に削る。

    **意図的に小さな直し方**で、「診断を読んで直す」様子を見せるためだけのものです。
    本物のアプリはここで LLM に診断を渡して HTML を書き直させます
    （このファイルの末尾、``main`` の手前のコメントを見てください）。

    ``offset`` は入力の先頭からの **バイト数** なので、バイト列のまま扱います。
    後ろから削れば、前の位置がずれません。
    """
    data = bytearray(source.encode("utf-8"))
    offsets = sorted(
        (
            error["offset"]
            for error in diagnostics.get("errors", [])
            if error.get("kind") in FIXABLE_KINDS and error.get("offset") is not None
        ),
        reverse=True,
    )
    for offset in offsets:
        span = _declaration_span(data, offset)
        if span is not None:
            del data[span[0] : span[1]]
    return data.decode("utf-8")


def _declaration_span(data: bytearray, offset: int):
    """``offset`` を含む CSS 宣言（``prop: value;``）の範囲を返す。

    ``unsupported-property`` はプロパティ名を、``unsupported-value`` は値を指すので、
    区切り（``{`` ``;`` ``}``）まで前後に広げて宣言ごと削ります。
    """
    if offset < 0 or offset >= len(data):
        return None
    start = offset
    while start > 0 and data[start - 1] not in b"{;":
        start -= 1
    end = offset
    while end < len(data) and data[end] not in b"};":
        end += 1
    if end < len(data) and data[end] == ord(";"):
        end += 1
    return start, end


class DummyGenerator:
    """LLM の代わりのダミー生成器。

    1 回目はテンプレートをそのまま返し（対応外のプロパティを含む）、2 回目からは
    診断を見て該当の宣言を削ります。状態（直前の HTML）を自分で持つのが要点で、
    LLM を使う場合も会話か履歴として同じものを持ちます。
    """

    def __init__(self, template: str = CARD_TEMPLATE):
        self.template = template
        self.html = ""

    def __call__(self, request: Mapping[str, Any], diagnostics: Optional[Dict[str, Any]]) -> str:
        if diagnostics is None:
            self.html = fill_template(self.template, request)
        else:
            self.html = drop_reported_declarations(self.html, diagnostics)
        return self.html


# 本番で LLM を呼ぶときの骨格。ネットワークに触れるのはこの関数の中だけです。
# この実例はネットワークを使わないので、コメントのまま置いてあります。
#
#     SYSTEM_PROMPT = open("docs/guide/writing-html-for-shashoku.md").read()
#
#     def generate(request, diagnostics):
#         messages = [{"role": "user", "content": "次の内容でカードの HTML を書いてください:\n"
#                                                 + request["body"]}]
#         if diagnostics is not None:
#             # 直前の出力と診断を渡して直させる。文面ではなく kind と位置が契約なので、
#             # format_diagnostics() の行をそのまま貼れば十分です。
#             messages.append({"role": "assistant", "content": previous_html})
#             messages.append({"role": "user", "content":
#                              "shashoku が次の診断を返しました。直して HTML 全体を書き直して"
#                              "ください（対応表はガイド §2、代替表は §5）:\n"
#                              + "\n".join(format_diagnostics(diagnostics))})
#         return call_llm(SYSTEM_PROMPT, messages)   # ここだけがネットワークに触れる


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="shashoku の CLI を上限つきで再試行しながら呼ぶ実例",
    )
    parser.add_argument(
        "--shashoku",
        default=os.environ.get("SHASHOKU_BIN", "shashoku"),
        help="shashoku の実行ファイル（既定: 環境変数 SHASHOKU_BIN か PATH 上の shashoku）",
    )
    parser.add_argument("--out", required=True, help="成功したときに PNG を書く場所")
    parser.add_argument("--width", type=int, default=640, help="ビューポートの幅（CSS px）")
    parser.add_argument(
        "--height", type=int, default=None, help="高さ。省略すると内容に追従する"
    )
    parser.add_argument("--max-attempts", type=int, default=3, help="再試行の上限（既定 3）")
    args = parser.parse_args(argv)

    request = {
        "title": "冪等性",
        "body": "同じ操作を何回行っても、1 回だけ行ったときと同じ結果になる性質のこと。"
        "通信の再試行を安全にする。",
    }

    try:
        result = render_with_retry(
            DummyGenerator(),
            request,
            shashoku=args.shashoku,
            out_path=args.out,
            max_attempts=args.max_attempts,
            width=args.width,
            height=args.height,
            log=sys.stderr,
        )
    except ShashokuError as exc:
        # 診断ではない失敗。再試行しても直らないので、そのまま失敗させます。
        print("error: {}".format(exc), file=sys.stderr)
        return 1

    return 0 if result.ok else 1


if __name__ == "__main__":
    sys.exit(main())
