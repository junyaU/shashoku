# Chrome（headless）との比較と、差の分類

[scripts/compare/](../scripts/compare/) の使い方は [scripts/compare/README.md](../scripts/compare/README.md)。
この文書は **何を比べているか** と **出た差をどう読むか**、そして **いま出ている差の一覧**。

単体テストとゴールデンが保証しているのは「以前と同じ絵が出る」ことで、「組版として正しい絵が
出る」ことではない。過去に、テストが全部通ったまま絵が間違っていた事故が 2 回ある
（縦書きのルビが左右逆 / `font-weight: 700` なのに Regular）。#15 の比較はその穴を塞ぐための
観測手段で、**Chrome に近づけるためのものではない**。狙いは「機能同士の組み合わせで
情報や意味が落ちるのを見つける」こと。

---

## 1. 何を比べるか

ピクセル一致は目標ではない（DESIGN.md §4）。判定は 4 つだけ。

| 判定 | shashoku | Chrome |
|---|---|---|
| **改行位置** | `--dump-stage box` の各行の `fragments[].text` を `inline_start` 順に連結。**`rt` の断片は `baseline < 行の baseline` で先に落としてから並べ替える** | クラスタごとの `Range.getBoundingClientRect()` を DOM 順にたどり、inline 座標が戻って block 座標が進んだところで行を割る |
| **はみ出し** | `max(inline_start + inline_size) − rect[2]`（`overflows` フィールドは無いので計算する）。**祖先の箱との共通部分**で測る（flex アイテムは自分の箱が親より広くなる） | 断片の inline の終端が包含ブロックを超えた量 |
| **欠落** | 断片の ink（横書きは hhea の ascent / descent、縦書きは中心軸の前後 `font-size/2`）が、ブロックの箱や紙面の外へ出た量 | クラスタの `Range` の箱が同じように外へ出た量 |
| **重なり** | 同じ行の隣り合うクラスタの箱が inline 方向に重なる量と、**クラスタごとの字送り**（グリフの inline 位置の差） | 同じ |

**比べないもの**:

- **グリフ単位の x**。shashoku はグリフ原点をデバイスピクセルに丸める（A8）ので、
  サブピクセル配置の Chrome とは必ず 1px 未満の系統差が出る
- **block 方向の絶対座標**。`line-height: normal` の作り方が違う（下の §3-1）
- **ピクセルの色**。アンチエイリアスとガンマは別物

**許容差**: inline 方向 1.0 px、block 方向 2.5 px（`compare.py` の `TOL_INLINE` / `TOL_BLOCK`）。
Chrome の LayoutUnit は 1/64 px、shashoku は FreeType の 26.6 なので、0.02 px 程度の差は常に出る。

## 2. 差の 3 分類

出た差は次の 3 つに仕分ける。**`差` と出ただけでは「shashoku の誤り」ではない。**

1. **意図した差** — shashoku が仕様としてそう決めているもの。**必ず ARCHITECTURE.md の A 番号を添える**
   （添えられないものは判断記録が無いということなので、書き足す）
2. **shashoku の誤り** — 個別の issue にする
3. **Chrome の方が日本語組版として劣る** — 直さない。ここに入れた理由を書く

---

## 3. 比較の土台をそろえるためにしていること

### 3-1. フォント

shashoku に渡すのと**同じファイル**を `@font-face` で読ませる（`build/_assets/fonts/`）。
family 名は `SHK0` / `SHK1` … に付け替える。同名のシステムフォントを拾わないため。
shashoku は「フォントファイルの family 名でまとめ、その中で weight が近いものを選ぶ」
（§3.5）ので、同じ family のファイルは同じ `SHK<n>` にまとめて `font-weight` だけ変える。

**本当にそのフォントで描かれたかの確認**は 2 段構えにしてある。`document.fonts` の
`status` だけでは「ある文字だけシステムフォントに落ちた」を見逃すので、同じ文字列の幅を
shashoku と突き合わせる（欧文は字幅がフォントごとに大きく違うので効く）。結果は
`report.txt` の冒頭に出る。

### 3-2. 既定のスタイル

`src/style/ua_stylesheet.cpp` と同じ規則を Chrome 側にも入れる（`h1`〜`h6` と `p` のマージン、
`rt { font-size: 0.5em }`）。加えて:

- `line-break: strict` — CLI の既定が `--line-break strict`
- `font-synthesis: none` — shashoku は合成ボールドを作らない
- `margin: 0` — shashoku の `#root` にマージンは無い

### 3-3. 紙面の大きさ

WSL から Windows の `chrome.exe` を呼ぶと `--window-size` が当てにならない
（幅は 500 px 未満にならず、`--dump-dom` のときは `window.innerWidth` が 0 になる）。
そこで紙面は CSS で決めている: `#shk-root` を `position: absolute; left: 0; top: 0` で
物理的な左上に固定し、幅（縦書きは高さも）を指定する。スクリーンショットは 1:1 の CSS px で
撮れるので、左上が shashoku の PNG と同じ範囲になる。

### 3-4. 縦書き

shashoku は `writing-mode` を文書全体で 1 つしか持たない（A1）ので、Chrome 側でも
`html` に `writing-mode: vertical-rl` を置く。断片の `div` だけを縦にすると紙面の向きが食い違う。

測定値は **Chrome 側を論理座標に直してから**比べる（縦書きは inline = y、block = −x）。
#15 の本文は「縦書きは `display-list` を使うのが楽」としているが、Chrome 側を論理に直せば
横書きと同じ 1 本のコードで比べられるので、`box` に一本化した。

---

## 4. いま出ている差

計測日 2026-09-21 / main `ad327c0`（#16〜#19 を直す前）/ Noto Sans JP Regular /
Chrome 153.0.8010.52（Windows 側にインストールされているものを WSL から headless で呼ぶ）。
生の値は `build/compare/report.txt` と各ケースの `box.json` / `chrome.json`。

| # | ケース | 欠落 | 重なり | 改行位置 | はみ出し | 分類 |
|---|---|---|---|---|---|---|
| 1 | ルビ × letter-spacing（横） | OK | **差** 字送り 16.03 / 48.00 | OK | OK | **shashoku の誤り**（#16） |
| 2 | ルビ × letter-spacing（縦） | OK | **差** 字送り 32.00 / 80.00 | OK | OK | **shashoku の誤り**（#16） |
| 3 | ルビ × 親文字内のサイズ混在 | **差** 行外 62.7 / 0.0 px | OK | OK | OK | **shashoku の誤り**（#17） |
| 4 | ルビ × 禁則 | OK | **差** 字送り 14.00 / 20.00 | OK | OK | 意図した差（A-new-2） |
| 5 | ルビ × justify | OK | **差** 字送り 16.00 / 20.00 | OK | OK | 意図した差（A-new-2） |
| 6 | ルビ × flex | OK | OK | OK | OK | — |
| 7 | ルビ × `<br>` × `<rp>` | OK | **差** 字送り 16.00 / 20.00 | OK | OK | 意図した差（A-new-2） |
| 8 | flex × `overflow-wrap:anywhere` | OK | ?? | **差** 1 行 / 3 行 | **差** 50.2 / 0.0 px | **shashoku の誤り**（#18） |
| 9 | flex × 和文の長文 | OK | OK | OK | OK | — |
| 10 | flex × `<img>`（stretch） | OK | OK | OK | OK | 意図した差（A18。下の注） |
| 11 | サイズ混在 × line-height | OK | OK | OK | OK | — |
| 12 | サイズ混在 × letter-spacing | OK | OK | OK | OK | — |
| 13 | letter-spacing × 約物のアキ詰め | OK | OK | OK | OK | — |
| 14 | letter-spacing × justify | OK | OK | OK | OK | 意図した差（A13。1px 未満） |
| 15 | 縦書き × ルビ × 欧文の横倒し | OK | OK | OK | OK | — |
| 16 | 縦書き × `<img>` | OK | OK | OK | OK | 意図した差（候補 8） |
| 17 | 縦書き × flex | OK | OK | OK | OK | — |
| 18 | `<img>` × インライン × 行高 | OK | OK | OK | OK | — |
| 19 | フォールバック × 字間 × font-weight | OK | OK | OK | OK | — |
| 20 | 有限性 × 極端な長さ | ?? | ?? | ?? | ?? | **shashoku の誤り**（#19） |
| 21 | ルビ × letter-spacing × ルビの方が長い | OK | **差** 字送り 57.73 / 152.00 | OK | OK | 意図した差（A-new-2） |

### 4-1. shashoku の誤り（すでに issue がある 4 件）

比較の仕組みが役に立つかどうかの検算として、設計レビューで見つかっていた 4 件が
**この 4 つの判定だけで見つかるか**を確かめた。4 件とも見つかった。

- **#16**（ケース 1・2）: ルビの親文字に `letter-spacing` が入らない。**重なり（字送り）**で出る。
  横書きで 16.03 px（Chrome 48.00）、縦書きで 32.00 px（Chrome 80.00）。
  絵でも、親文字だけ字間が詰まっているのがはっきり見える
- **#17**（ケース 3）: 親文字の中の大きい `<span>` が行の高さに数えられない。**欠落**で出る。
  shashoku は ink が行の箱から 62.7 px はみ出して画像の外に切れる。Chrome は 0.0 px
- **#18**（ケース 8）: `overflow-wrap: anywhere` が flex アイテムの min-content に効かない。
  **改行位置**（1 行 / 3 行）と**はみ出し**（50.2 px / 0.0 px）の両方で出る
- **#19**（ケース 20）: 計算値が非有限になる。`--dump-stage box` の座標が JSON の `null`
  （= inf / NaN）になり、数値の比較そのものができない。スクリプトはこれを検出して
  「座標が非有限」と報告する。**それでも CLI は exit 0 で PNG を返す**

### 4-2. 意図した差

- **ルビ組の親文字の配置**（ケース 4・5・7・21）— **A-new-2**。
  shashoku は短い方を中央に置くだけ、Chrome は親文字の側を均等に広げて注記の幅に合わせる。
  1:2:1 の配分とルビの掛けが未対応（README の「既知の制限」）なので、いまは中央寄せで止めている
- **`<img>` を stretch で歪めない**（ケース 10）— **A18**。
  交差軸のサイズを auto にした `<img>` を `align-items: stretch` の flex アイテムに置くと、
  Chrome は行の高さまで縦に伸ばして歪める。shashoku は固有寸法のまま。**絵で見る差**で、
  4 つの数値の判定には出ない（どちらも行から出ず、改行位置も変わらないため）
- **`text-align: justify` の配分**（ケース 14）— **A13**。1 px 未満の差しか出ない。
  shashoku は約物の前の 1 箇所だけ配分から外す（26.00 px のまま）のに対し、Chrome は均等に配る
  （26.75 px）。行の幅はどちらも 240 px ぴったり
- **縦書きの `<img>` の中央揃え**（ケース 16）— #15 本文の候補 8。判定には出ない

### 4-3. 一致していて、むしろ安心できたもの

- **約物のアキ詰め**（ケース 13）: `」` と `。` が続く箇所の字送りが、両者とも 26 → **16 px**。
  行末の `。` の詰めも 25.41 / 25.39 px でほぼ一致。A16（JLREQ 3.1.4）は Chrome の
  `text-spacing-trim` の既定と同じ結果になっている
- **改行位置**: ケース 8（#18）以外の 19 件すべてで文字列が完全に一致した。
  禁則（ケース 4）、flex の中の折り返し（6・9・17）、`<br>`（7）、縦書き（15・17）を含む
- **フォント**: `Hamburgefonstiv` の幅が shashoku 161.453 / Chrome 161.422（差 0.03 px）。
  Chrome 側も同じ Noto Sans JP で描いている
- **和欧混植のフォールバック**（ケース 19）: `letter-spacing` が和文側に二重に入るようなことはない

### 4-4. 比較の仕組みの側で分かったこと

- **Chrome の `line-height: normal` は shashoku より 0.8 px 高い**（16 px の行で 24.0 と 23.171875）。
  Chrome が ascent / descent を整数 px に丸め、shashoku が FreeType の 1/64 px で持つため。
  block 方向の絶対座標を比べてはいけない理由がこれ
- **合字があるとクラスタ単位で比べられない**。Noto Sans JP には `fl` の合字があり、
  `flex` という単語を含めるとグリフ数とクラスタ数が合わなくなる。比較ケースの欧文は
  合字にならない綴りにしてある（`report.txt` に注として出る）
- **Chrome は `<rt>` のインラインボックスを親ブロックの箱から 4 px はみ出させる**。
  ルビの帯の作りが違うだけで欠落ではないので、判定からは外してある
