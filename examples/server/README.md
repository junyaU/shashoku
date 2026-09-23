# サーバー連携の小さな実例

AI が HTML を作る → shashoku の CLI で描く → 診断 JSON を読む → 直して再試行（上限つき）。
この一連を**呼び出し側のアプリがどう書くか**だけを示す最小の実例です。

- [`render_with_retry.py`](render_with_retry.py) — 本体（python3 の標準ライブラリだけ。依存なし）
- [`test_render_with_retry.py`](test_render_with_retry.py) — 本物の CLI を呼ぶテスト

HTTP サービスでも、汎用のエージェント基盤でもありません。**shashoku 自身は AI を呼びません。**
何回まで直させるか、諦めたときに何を返すかは呼び出し側のアプリの責任です（DESIGN.md §0）。

## 動かす

```bash
python3 examples/server/render_with_retry.py --shashoku build/release/tools/shashoku/shashoku --out out.png
```

同梱のダミー生成器は、まず対応外のプロパティを 3 つ含む HTML を出し、診断を見て直し、2 回目で成功します。

```
attempt 1/3: 3 error(s)
  error[unsupported-property] at 3:12: `box-sizing` is not a supported property
    hint: content-box only: subtract padding and border from `width` / `height`
  error[unsupported-property] at 3:36: `box-shadow` is not a supported property
  error[unsupported-property] at 5:12: `text-transform` is not a supported property
attempt 2/3: ok, wrote out.png (640x141)
```

テスト（ctest には入っていません。手で走らせます）:

```bash
python3 examples/server/test_render_with_retry.py
```

CLI の場所は `SHASHOKU_BIN` で指定できます。指定が無ければ `build/release/` → `build/dev/` →
`build/asan/` の順に探します。

## 2 つの使い方

どちらを選ぶかで、**LLM が動く場所と回数**が変わります。この実例は (B) の形ですが、
(A) でも同じ関数がそのまま使えます（`generate` が LLM を呼ばず、テンプレートに値を流し込むだけになる）。

### (A) 開発時にテンプレートを作る（まずこちらを検討する）

```
  開発用の AI（skill + ガイドを読む）
        │  カードの HTML テンプレートを書く          ← 1 回だけ
        ▼
  shashoku CLI ──診断──▶ AI が直す（人が絵を見て確かめる）
        │  ok
        ▼
  テンプレートをリポジトリに置く
        │
        ▼
  本番: 値を流し込んで描くだけ。LLM は動かない
```

定型のカードや OG 画像はこれで足ります。組版が変わるのはテンプレートを直したときだけなので、
本番の失敗は「値が長すぎてはみ出した」に絞られます。速く、安く、再現します。
テンプレートの作り方は [`docs/guide/SKILL.md`](../../docs/guide/SKILL.md) と
[`docs/guide/examples/`](../../docs/guide/examples/) にあります。

### (B) 本番で毎回 LLM に作らせる

```
  リクエスト ──▶ generate(request, diagnostics) ──▶ HTML
                      ▲                              │
                      │ 診断 JSON（上限 N 回）        ▼
                      └───── shashoku --diagnostics json --strict
                                                     │ ok
                                                     ▼
                                                    PNG
```

内容ごとに紙面の構成が変わる場合（図解、要約カード、可変の表）はこちらです。
`render_with_retry.py` はこの形で、差し替えるのは `generate` だけです。

```python
def generate(request, diagnostics):
    """HTML を返す。diagnostics は初回だけ None、以降は直前の診断 JSON。"""
```

LLM を呼ぶ場合の骨格は `render_with_retry.py` の末尾（`main` の手前）にコメントで置いてあります。
プロンプトには [`docs/guide/writing-html-for-shashoku.md`](../../docs/guide/writing-html-for-shashoku.md)
を丸ごと貼ります。診断は `format_diagnostics()` が返す行をそのまま渡せば十分です。

## 上限つきで再試行する理由

診断は「直し方つきで一度に全部」出るので（A46）、多くの場合 1 回の書き直しで直ります。
それでも上限が要るのは、**直らない失敗があるから**です。

- LLM が同じ誤りを繰り返す（ガイドを読ませていない、対応外の機能がどうしても要る）
- 依頼そのものが対応範囲の外（`<table>` の複雑な結合、JavaScript、グラデーション）
- 直したつもりで別の誤りを入れる（往復が終わらない）

上限に達したら、`RenderResult` に **最後の診断と最後の HTML** を入れて呼び出し側に返します。
呼び出し側はそれをログに出し、代替画像を返すか、人に回します。**黙って壊れた画像を配るより、
失敗として扱えるほうが運用は楽です。**

`truncated` が真なら診断そのものが打ち切られています（既定 100 件）。このときは再試行の上限ではなく、
**入力を分けて小さくする**のが正しい対処です。

## `--strict` を使う理由

`--strict` は警告（豆腐・紙面からのはみ出し）も失敗に格上げし、**PNG を作りません**
（既にあるファイルも上書きしません）。

警告のままだと終了コードは 0 なので、`□□□` が並んだ画像や、下が 430px 切れた画像が
そのまま配信されます。サーバーでは「**検出した問題のある画像は配らない**」ほうが安全なので、
この実例は既定で `--strict` を付けています。

格上げされた警告は `errors` 側に `"kind": "warning-as-error"` として来て、
元の種類が `"warning"` に入ります（`"content-overflow"` など）。`generate` から見れば
ほかのエラーと同じ形なので、扱いを分ける必要はありません。

`--strict` を外したいのは、はみ出しを承知で出す場合（サムネイル）だけです。
`render_with_retry(..., strict=False)` で外せます。

## 約束の範囲

**`--strict` の成功が意味するのは「対応範囲の中で、検出対象の検査を満たした」ことであって、
絵が正しいことの保証ではありません**（DESIGN.md §3-6）。

検出されないもの: 箱から文字がはみ出す（紙面ではなく固定幅の箱）、要素の重なり、
文字色と背景色の同化、意図と違う位置。**自動で配信する前に、まず目で何枚か見てください。**

機械が頼ってよいのは `kind` / `warning` / `edge` の識別子と位置（`line` / `column` / `offset`。
分からなければ `null`）です。`message` / `detail` / `hint` の**文面は版で変わりえます**（ガイド §6.1）。

## 終了コードの扱い

| 終了コード | 意味 | この実例の扱い |
|---|---|---|
| `0` | 成功。標準出力に `{"ok": true, ...}` | 一時ファイルを目的の名前に rename して返す |
| `1` | 診断あり。標準出力に `{"ok": false, ...}` | `errors` を `generate` に渡して再試行 |
| `1` | **入出力の失敗**（入力を読めない・フォントを読めない・出力を書けない）。**JSON は出ない** | `ShashokuError`。**再試行しない** |
| `2` | 引数の誤り | `ShashokuError`。**再試行しない** |

終了コード 1 が 2 通りあるのが唯一の注意点です。標準出力が JSON として読めなければ入出力の失敗で、
HTML を書き直しても直りません。

出力は一時ファイルに書き、**成功したときだけ**目的の名前に rename します。CLI 自身も失敗時には
出力を書きませんが、呼び出し側でも同じ規律を示しています（`-o` に既にあるファイルは、
諦めたときも中身が変わりません）。

## 起動コストの目安

CLI を 1 枚ごとに子プロセスで起動する形のコストです（`docs/benchmark/results_2026-09-23.md` §5、
i9-14900KF）。

| | |
|---|---|
| cold（1 枚、プロセス起動込み） | **10〜30 ms**（中央値 20）、RSS 37〜47 MB |
| 導入 | 単一バイナリ 12.5 MB（既定フォント込み）。ランタイムの追加なし |

C++ API を常駐させれば 1 枚 16.8 ms・RSS 29 MB で、プロセス起動の数 ms が消えるうえ
`LoadedFonts` / `LoadedImages` を全スレッドで共有できます（README の「連続生成」。
16 並行で 558 枚/秒）。子プロセスで足りるうちは子プロセスのほうが、障害の切り分けも
リソースの上限も簡単です。

再試行は 1 回ごとにこのコストが乗ります。上限 3 回なら最悪 60 ms 前後（LLM の時間は別）で、
そこが支配的になることはまずありません。
