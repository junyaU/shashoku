---
name: shashoku-html
description: shashoku で PNG にできる HTML を書き、エラーを読んで直す
---

# shashoku-html

shashoku（HTML→PNG エンジン、v0.1.0）向けの HTML を書き、CLI で PNG にし、
エラーが出たら読んで直すための skill。

**shashoku はブラウザではありません。対応表にないタグ・属性・プロパティ・値は
黙って無視されずエラーになります。** だから「ブラウザで動く HTML」を書いてはいけません。

## 使う手順

1. **HTML を書く前に**
   [writing-html-for-shashoku.md](writing-html-for-shashoku.md) を読む（英語版:
   [writing-html-for-shashoku.en.md](writing-html-for-shashoku.en.md)）。
   対応表は §2、先に知っておくことは §3、定石の最小例は §4、代替表は §5 にある。
   LLM に書かせるなら、この文書を丸ごとプロンプトに貼る
2. **骨格は [examples/](examples/) から写す**。14 個の定石（カード・横並び・表・箇条書き・
   タグ・縦中央揃え・左右の位置合わせ・見出しと本文・OG 画像・縦書き・ルビ・画像・
   flex の入れ子・引用カード）が、`exit 0`・警告 0 で PNG になることを確かめた形で置いてある
3. **段を横に並べる図（フロー図）を書くなら、先に下の「図解を書くとき」を読む**
4. **CLI で描く**（下記）
5. **失敗したらガイド §6**（エラーの読み方・直す順序・現状と予定）
6. **成功しても PNG を開いて目で見る**。エラーが無いことは絵が正しいことを意味しない

先に知っておく 6 点（ガイド §3-(1) / §3-(3) / §2.2 / §4.2 / §4.13 / §5）:

- **`box-sizing` は `content-box`（既定。ブラウザと同じ）と `border-box` の両方が使える。**
  先頭に `* { box-sizing: border-box }` を書けば、`width` / `height` から padding と border を
  引く必要はない（`flex-basis` と `<img>` にも効く）。ガイド §4 の例は content-box のまま
- **`display: flex` の直接の子は block 化される。** `span` でも `div` でも同じ絵になり、
  `padding` / `border` / `width` を付けてよい。例外は `<img>` / `<ruby>` / `<br>`（inline のまま）。
  文章の中に置いた `span`（flex の孫）は inline のままで、箱のプロパティは `unsupported-layout`
- **`flex: 1 1 0` の等分は「分割できない一番長い語の幅」までしか縮まない。** URL・ハッシュ・
  長い英単語が入るとその子だけ広がり、合計が親を超えるとはみ出す。`overflow-wrap: anywhere` を
  付けて直す（**`break-word` は効かない**）
- **子孫セレクタ（`.th .key`）は書けない。** 使えるのは `tag` / `.class` / `#id` / `*` と、
  **空白を入れずに**繋げた複合（`.tr.th`）と、そのカンマ区切りだけ。空白を入れると
  `error[css-parse] … descendant combinators … are not supported` で止まる。
  **当てたい要素にクラスを直接付ける**（`<div class="td key hkey">`）
- **「ここでは割らない」と指定する手段は `&nbsp;`（U+00A0）と `white-space: nowrap`。** 数字と単位は `17&nbsp;ms`、
  数字と助数詞は `2027&nbsp;年` と結ぶ（行分割器がその空白を割らない。見た目はふつうの半角スペース）。
  ただし**守れるのはその空白 1 か所**で、和文は語の中でも折れる（`2027&nbsp;年度` は `年` / `度` に割れうる）。
  語全体を 1 かたまりにするなら次の `white-space: nowrap`（ガイド §5）
- **図のラベル・数字と単位・タグを 1 行に保つには `white-space: nowrap`。** 継承するので、
  親に付ければ中身すべてに効く（`<br>` で改行位置を自分で決めてもよい）。折らないぶん
  `flex: 1 1 0` の段は「ラベル 1 行ぶん」より縮まないので、**段の数 x ラベルの幅 + 矢印 + gap +
  padding** がだいたいの最小の `--width` になる。超えると紙面からはみ出して
  `warning[content-overflow]` が出る（親の箱から出ているだけで紙面の中なら警告は出ない）

## 図解を書くとき（段を横に並べる絵。ガイド §4.13 / §4.5）

**等分（`flex: 1 1 0`）の段に文字が入るかは、書く前に見積もる。** 入らないと語の途中で折れる
（`.name` に `white-space: nowrap` を付ければ折れずに段が広がり、紙面の外に出れば `warning[content-overflow]` になる）。

```
段の外寸 = (--width − 外枠の左右 padding − 矢印の幅 × 本数 − gap × 隙間の数) ÷ 段数
段の内寸 = 段の外寸 − 段の左右 padding − 左右の border
1 行に入る全角文字数 ≒ 段の内寸 ÷ font-size   （全角 1 文字 ≒ font-size px）
```

例: `--width 1000`・外枠の padding 28px・矢印 20px × 5 本・gap 8px × 10・6 段
→ 外寸 (1000 − 56 − 100 − 80) ÷ 6 = **127.3px**、内寸 127.3 − 12×2 − 1×2 = **101.3px**
→ 14px の見出しは **7 文字/行**（`スタイル付きツリー` は `スタイル付きツ` / `リー` で折れる）、
12px の説明は **8 文字/行**。

- **改行位置は `<br>` で自分で決めてよい**（`&nbsp;` の「ここでは割らない」と対になる手段）。
  入りきらない見出しは `スタイル付き<br>ツリー` と書けば、望む位置で 2 行になる
- **行数の違う段で下の行の開始位置をそろえる**には、見出しの `div` に **2 行ぶんの `height` を固定**する
  （`line-height: 20px; height: 40px`）。ただし **3 行に溢れると黙って下の行に重なる**
  （警告は出ない。`--strict` でも exit 0）ので、`--dump-stage box` でその箱の `lines` が
  2 以下かを確かめる
- **凡例（色見本 + ラベル）はガイド §4.5 の pill の形**（`flex: none` + `padding` + `border-radius`）。
  矢印・段・タグ列の骨格はガイド §4.13

## CLI

```
shashoku <input.html> -o <out.png> [--width N] [--height N] [--scale S]
         [--font <file>]... [--image <name>=<file.png>]...
```

```bash
shashoku card.html -o card.png --width 640                      # 高さは内容に追従
shashoku og-card.html -o og.png --width 1200 --height 630       # 高さを固定
shashoku image.html --image icon=icon.png -o out.png --width 560
shashoku vertical.html -o v.png --width 480 --height 900        # 縦書きは --height 必須
shashoku card.html --dump-stage box --width 640                 # 箱の寸法を数字で見る
shashoku card.html -o card.png --width 640 --diagnostics json   # 診断を機械が読む形で
shashoku og-card.html -o og.png --width 1200 --height 630 --strict  # 警告も失敗にする
shashoku --version                                              # 版（この skill は 0.1.0 向け）
```

押さえておく点（詳細はガイド §7）:

- `--width` の既定は 1200。**`--height` を省くと内容の高さに追従する**（縦書きでは必須）
- 画像は **PNG のみ**。`--image 名前=ファイル` で渡し、HTML には `<img src="名前">` と書く
  （URL でもパスでもない）
- フォントは `--font`（**指定順がフォールバック順**）。省略すると埋め込みの既定フォント。
  HTML 側に Web フォントを書くことはできない
- 終了コードは 0 成功 / 1 レンダリング・入出力の失敗 / 2 引数の誤り
- **成功すると `wrote out.png (1200x630)` が標準エラーに 1 行出る**。`--height` を省いたときの
  実際の高さはここで分かる
- **失敗したときは出力ファイルを作らない・上書きしない**（`--strict` の失敗も同じ）
- 同じ入力からは常にバイト単位で同じ PNG が出る
- **`shashoku` のパスは利用者が指定する。** この skill は CLI の導入方法を決めない
  （リポジトリで `cmake --build --preset release` して `build/release/tools/shashoku/shashoku`、
  という前提は配布バイナリを受け取った人には当てはまらない）。パスが分からなければ利用者に聞く
- **画像を渡す手段が無い環境では、出力先のパスを本文で伝える**（相手は PNG を自分で開く）
- **折り返しの検査は `--dump-stage box`**: 各テキスト箱の `lines` の数が、その箱が何行になったか。
  JSON は大きいので、確かめたい箱の `location`（`行:桁`）で絞って読む。
  **`--dump-stage` は `--diagnostics json` と併用できない**（exit 2）

## 診断（v0.1.0）

```
error[unsupported-value] at 82:77: `border-style: dashed` is not supported (supported: solid, none)
error[unsupported-property] at 84:6: `max-width` is not a supported property
  hint: no min/max sizes: use a fixed `width` / `height`, or drop it
warning[missing-glyph]: no font has a glyph for U+1F600 at 1:6
warning[content-overflow]: content overflows the canvas by 430.0px (bottom) at 19:1
warning[font-not-found]: no requested font family is loaded (`Hiragino Mincho ProN`, `serif`); text uses `Noto Sans JP` instead at 1:57
```

- 機械が頼ってよいのは **識別子**（`unsupported-value` など）と **位置**（`行:桁`）。文面は変わりうる
- **エラーは見つかった分が一度に全部出る**（1 行 1 件）。直し方が分かるものには `  hint: …` の行が続く。
  ただし**構造の破損**（閉じ忘れ・`&` の書き忘れ・不正な UTF-8）はその場で止まるので、先に直す
- **hint は成立条件つき**のことがある（「親が stretch の flex なら削る」「不等幅・またぎは代替なし」）。
  条件を読んでから選ぶ。hint が無いのは「確かめた代替が無い」という意味
- 同じ規則に複数の要素が当たっても、**同一位置・同一文面の診断は 1 件**にまとまる
- 警告は `missing-glyph`（豆腐）、`content-overflow`（紙面からのはみ出し。切れる量と辺つき）、
  `font-not-found`（`font-family` の要求をどれも満たせず別のフォントで描いた。`sans-serif` /
  `system-ui` / `ui-sans-serif` は「渡したフォントの先頭で描いてよい」という意味なので出ない。
  エンジンは書体を判定しないので、明朝だけを渡して `sans-serif` と書いても明朝で描いて警告なし）。
  警告が出ても PNG は作られ、終了コードは 0
- **`--strict`** を付けると警告 1 件以上で失敗になり、PNG は作られない（配信前の門に使う）
- **`--diagnostics json`** で標準出力に 1 オブジェクト（成功でも失敗でも。人向けの stderr は出ない）。
  `-o` が必須で `--dump-stage` と併用不可。形はガイド §6.4
- エラーも警告も無いことは「絵が正しい」ことを意味しない。重なり・文字色と背景色の同化は検出されない

## この skill の版

対応エンジンは **shashoku 0.1.0**。`shashoku --version` が別の版を出したら、その版の
`docs/guide/` を見てください（ガイドとエンジンの版はそろえて配布しています）。
