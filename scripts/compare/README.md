# Chrome（headless）との比較

shashoku と Chrome の組版を並べて、**機能同士の組み合わせで情報が落ちていないか**を見るための
ローカル専用のスクリプト（#15）。**CI には入れない。** 製品のビルドは Chrome に依存しない。

見るのはピクセル差ではなく **欠落 / 重なり / 改行位置 / はみ出し** の 4 つ。
Chrome は正解ではなく、差を見つけるための第 2 の意見として使う。差の読み方と結果は
[docs/chrome_compare.md](../../docs/chrome_compare.md)。

ケースは [cases/cases.json](cases/cases.json) の 25 件。1〜21 が「機能 × 機能」の
組み合わせ（#15 のコメント）、22〜25 が `examples/`（= ゴールデンの入力）。

## 走らせ方

```bash
scripts/compare/compare.py \
  --shashoku build/release/tools/shashoku/shashoku \
  --chrome '/mnt/c/Program Files/Google/Chrome/Application/chrome.exe' \
  --fonts build/_assets/fonts
```

環境変数でも渡せる（`SHASHOKU_BIN` / `SHASHOKU_CHROME` / `SHASHOKU_FONTS_DIR`）。

| オプション | 意味 |
|---|---|
| `--only 1,2,8` | ケース番号で絞る |
| `--out DIR` | 出力先（既定 `build/compare`） |
| `--no-chrome` | shashoku 側だけ動かす（Chrome が無い環境の確認用） |
| `--no-images` | 左右に並べた PNG を作らない（速い） |

必要なもの: **python3 の標準ライブラリと Chrome だけ。** Playwright も Puppeteer も Node も要らない。

## 出力

`--out` の下に:

| ファイル | 中身 |
|---|---|
| `report.txt` | ケース × 4 判定の表と、差の内訳。**まずこれを見る** |
| `report.html` | 同じ表 + 左右に並べた画像 |
| `NN/shashoku.png` `NN/chrome.png` | 各ケースの絵 |
| `NN/side-by-side.png` | 左右に並べた 1 枚 |
| `NN/box.json` `NN/chrome.json` | 判定のもとにした測定値 |

判定の印: `OK` 差なし / `差` 差あり / `??` 比較不能 / `失敗` 実行できず。
**`差` は「shashoku が間違っている」ではない。** 3 分類（意図した差 / shashoku の誤り /
Chrome の方が日本語組版として劣る）に仕分けてから読む。仕分けの表は
[docs/chrome_compare.md](../../docs/chrome_compare.md)。

## 仕組み

1. shashoku を `--dump-stage box` と `-o out.png` で 2 回走らせる
2. ケースの断片 HTML を、shashoku と同じ条件（同じフォントファイルを `@font-face` で、
   同じ UA スタイルシート、`line-break: strict`、`font-synthesis: none`）に包んだページを作る
3. `chrome.exe --headless --dump-dom --screenshot` で 1 回走らせる。
   ページ内の [`measure.js`](measure.js) が `document.fonts.ready` を待ってから
   `Range.getClientRects()` で測り、結果を base64 にして DOM に書き出す。
   compare.py はそれを `--dump-dom` の標準出力から取り出す
4. 両側の測定値を同じ形（ブロック → 行 → クラスタ）に直して 4 つの判定を出す

**CDP（WebSocket）は使わない。** 標準ライブラリに WebSocket クライアントが無いため。

## WSL から Windows の Chrome を呼ぶときの注意

- `\\wsl.localhost\...` の HTML とフォントは `file://` で読める（`@font-face` も通る）。
  パスの変換は `wslpath -w`
- **ユーザーの普段のプロファイルには触らない。** `--user-data-dir` に `--out` の下の
  使い捨てディレクトリ（`build/compare/chrome-profile`）を必ず渡している。
  Windows 側には何も書かない
- `--window-size` は当てにならない。WSL から呼ぶと幅は 500 px 未満にならず、
  `--dump-dom` のときは `window.innerWidth` が 0 になる。そのため紙面は CSS 側で決めている
  （`#shk-root` を `position: absolute` で左上に固定し、幅と高さを指定する）
- 起動のたびに crashpad のエラーが標準エラーに出るが、測定には影響しない
