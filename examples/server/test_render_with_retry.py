#!/usr/bin/env python3
"""render_with_retry.py のテスト。本物の CLI を子プロセスで呼びます。

    python3 examples/server/test_render_with_retry.py

CLI の場所は環境変数 ``SHASHOKU_BIN`` で指定できます。指定が無ければ
``build/release/`` → ``build/dev/`` → ``build/asan/`` の順に探します。
"""

import os
import shutil
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True  # examples/ に __pycache__ を残さない
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from render_with_retry import (  # noqa: E402  (sys.path を通してから import する)
    DummyGenerator,
    ShashokuError,
    drop_reported_declarations,
    fill_template,
    render_with_retry,
)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"

REQUEST = {"title": "冪等性", "body": "同じ操作を何回行っても同じ結果になる性質のこと。"}

# 対応範囲の中だけで書いた HTML（1 回で成功する）。
GOOD_HTML = """<style>
  .card { padding: 24px; border: 1px solid #dfe3e8; }
</style>
<div class="card"><p>同じ操作を何回行っても同じ結果になる性質のこと。</p></div>
"""

# 何度作り直しても直らない HTML（対応外のタグ）。
BROKEN_HTML = "<div><marquee>流れる文字</marquee></div>\n"


def find_shashoku():
    env = os.environ.get("SHASHOKU_BIN")
    if env:
        return env
    for preset in ("release", "dev", "asan"):
        path = os.path.join(REPO_ROOT, "build", preset, "tools", "shashoku", "shashoku")
        if os.path.exists(path):
            return path
    raise SystemExit(
        "shashoku の実行ファイルが見つかりません。"
        "SHASHOKU_BIN を指定するか、`cmake --build --preset release` でビルドしてください"
    )


SHASHOKU = find_shashoku()


class RenderWithRetryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="shashoku-example-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.out = os.path.join(self.tmp, "out.png")
        self.seen = []  # generate が受け取った diagnostics の並び

    def render(self, generate, **kwargs):
        def recording_generate(request, diagnostics):
            self.seen.append(diagnostics)
            return generate(request, diagnostics)

        kwargs.setdefault("width", 400)
        return render_with_retry(
            recording_generate, REQUEST, shashoku=SHASHOKU, out_path=self.out, **kwargs
        )

    def read_out(self):
        with open(self.out, "rb") as handle:
            return handle.read()

    # -- 1 回で成功 --------------------------------------------------------

    def test_succeeds_on_first_attempt(self):
        result = self.render(lambda request, diagnostics: GOOD_HTML)

        self.assertTrue(result.ok, result.diagnostics)
        self.assertEqual(result.attempts, 1)
        self.assertEqual(self.seen, [None])  # 初回の diagnostics は None
        self.assertEqual(result.path, self.out)
        self.assertTrue(self.read_out().startswith(PNG_MAGIC))
        self.assertEqual(result.diagnostics["errors"], [])
        self.assertEqual(result.diagnostics["warnings"], [])
        self.assertFalse(result.diagnostics["truncated"])
        self.assertIsInstance(result.diagnostics["width"], int)

    # -- 2 回目で成功（診断を読んで直す） ----------------------------------

    def test_succeeds_on_second_attempt(self):
        result = self.render(DummyGenerator())

        self.assertTrue(result.ok, result.diagnostics)
        self.assertEqual(result.attempts, 2)
        self.assertEqual(len(self.seen), 2)
        self.assertIsNone(self.seen[0])

        # 1 回目の診断は「見つかった分が一度に全部」出る（A46）。
        first = self.seen[1]
        self.assertFalse(first["ok"])
        kinds = [error["kind"] for error in first["errors"]]
        self.assertEqual(kinds, ["unsupported-property"] * 3)
        # 位置（契約）が全件に付いていて、直し方が分かるものには hint が付く。
        for error in first["errors"]:
            self.assertIsInstance(error["line"], int)
            self.assertIsInstance(error["column"], int)
            self.assertIsInstance(error["offset"], int)
            self.assertIsNone(error["warning"])
        self.assertTrue(any(error["hint"] for error in first["errors"]))

        self.assertTrue(self.read_out().startswith(PNG_MAGIC))

    # -- 上限で諦める ------------------------------------------------------

    def test_gives_up_at_max_attempts(self):
        result = self.render(lambda request, diagnostics: BROKEN_HTML, max_attempts=2)

        self.assertFalse(result.ok)
        self.assertIsNone(result.path)
        self.assertEqual(result.attempts, 2)
        self.assertEqual(len(self.seen), 2)  # 上限を超えて呼ばない
        self.assertEqual(result.diagnostics["errors"][0]["kind"], "unsupported-tag")
        self.assertIn("2", result.advice)
        self.assertEqual(result.html, BROKEN_HTML)  # 調査用に最後の HTML が残る
        self.assertFalse(os.path.exists(self.out))

    # -- 使い方の誤り（終了コード 2）は再試行しない ------------------------

    def test_usage_error_is_not_retried(self):
        with self.assertRaises(ShashokuError) as caught:
            self.render(lambda request, diagnostics: GOOD_HTML, extra_args=["--bogus"])

        self.assertEqual(caught.exception.exit_code, 2)
        self.assertEqual(len(self.seen), 1)
        self.assertFalse(os.path.exists(self.out))

    # -- 入出力の失敗（終了コード 1 だが JSON が出ない）も再試行しない -----

    def test_io_failure_is_not_retried(self):
        missing_font = os.path.join(self.tmp, "nope.otf")
        with self.assertRaises(ShashokuError) as caught:
            self.render(lambda request, diagnostics: GOOD_HTML, fonts=[missing_font])

        self.assertEqual(caught.exception.exit_code, 1)
        self.assertEqual(len(self.seen), 1)
        self.assertFalse(os.path.exists(self.out))

    # -- 成功したときだけ出力ができる --------------------------------------

    def test_failure_leaves_existing_output_untouched(self):
        with open(self.out, "wb") as handle:
            handle.write(b"keep me")

        result = self.render(lambda request, diagnostics: BROKEN_HTML, max_attempts=2)

        self.assertFalse(result.ok)
        self.assertEqual(self.read_out(), b"keep me")  # 上書きしない
        self.assertEqual(os.listdir(self.tmp), ["out.png"])  # 一時ファイルも残さない

    def test_success_leaves_no_temporary_files(self):
        result = self.render(DummyGenerator())

        self.assertTrue(result.ok, result.diagnostics)
        self.assertEqual(os.listdir(self.tmp), ["out.png"])

    # -- --strict は警告も失敗にする ---------------------------------------

    def test_strict_turns_overflow_warning_into_failure(self):
        html = GOOD_HTML
        strict = self.render(lambda request, diagnostics: html, max_attempts=1, height=40)
        self.assertFalse(strict.ok)
        error = strict.diagnostics["errors"][0]
        self.assertEqual(error["kind"], "warning-as-error")
        self.assertEqual(error["warning"], "content-overflow")
        self.assertFalse(os.path.exists(self.out))

        # --strict を外せば PNG は作られ、警告として出る。
        self.seen = []
        loose = self.render(
            lambda request, diagnostics: html, max_attempts=1, height=40, strict=False
        )
        self.assertTrue(loose.ok)
        warning = loose.diagnostics["warnings"][0]
        self.assertEqual(warning["kind"], "content-overflow")
        self.assertEqual(warning["edge"], "bottom")
        self.assertGreater(warning["overflow_px"], 0)
        self.assertTrue(self.read_out().startswith(PNG_MAGIC))


class HelperTest(unittest.TestCase):
    """CLI を呼ばない小さな部品のテスト。"""

    def test_fill_template_escapes_values(self):
        filled = fill_template("<p>{{body}}</p>", {"body": "A & B <c>"})
        self.assertEqual(filled, "<p>A &amp; B &lt;c&gt;</p>")

    def test_drop_reported_declarations_uses_byte_offsets(self):
        # 日本語（マルチバイト）を挟んでも offset はバイト数なのでずれない。
        source = "<style>.a{color:#000;float:left;}</style><p>あ</p>"
        offset = source.encode("utf-8").index(b"float")
        diagnostics = {
            "errors": [{"kind": "unsupported-property", "offset": offset, "hint": ""}]
        }
        self.assertEqual(
            drop_reported_declarations(source, diagnostics),
            "<style>.a{color:#000;}</style><p>あ</p>",
        )

    def test_drop_reported_declarations_removes_whole_declaration_for_a_bad_value(self):
        # unsupported-value は値を指すが、宣言ごと消す（`border-style: ;` を残さない）。
        source = "<style>.a{border-style:dashed;color:#000;}</style>"
        offset = source.encode("utf-8").index(b"dashed")
        diagnostics = {"errors": [{"kind": "unsupported-value", "offset": offset, "hint": ""}]}
        self.assertEqual(
            drop_reported_declarations(source, diagnostics),
            "<style>.a{color:#000;}</style>",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
