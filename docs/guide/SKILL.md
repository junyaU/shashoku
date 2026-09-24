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
   対応表は §2、落とし穴は §3、定石の最小例は §4、代替表は §5 にある。
   LLM に書かせるなら、この文書を丸ごとプロンプトに貼る
2. **骨格は [examples/](examples/) から写す**。14 個の定石（カード・横並び・表・箇条書き・
   タグ・縦中央揃え・左右の位置合わせ・見出しと本文・OG 画像・縦書き・ルビ・画像・
   flex の入れ子・引用カード）が、`exit 0`・警告 0 で PNG になることを確かめた形で置いてある
3. **CLI で描く**（下記）
4. **失敗したらガイド §6**（エラーの読み方・直す順序・現状と予定）
5. **成功しても PNG を開いて目で見る**。エラーが無いことは絵が正しいことを意味しない

つまずきやすい 2 点（ガイド §3-(3) / §4.2）:

- **`display: flex` の直接の子は block 化される。** `span` でも `div` でも同じ絵になり、
  `padding` / `border` / `width` を付けてよい。例外は `<img>` / `<ruby>` / `<br>`（inline のまま）。
  文章の中に置いた `span`（flex の孫）は inline のままで、箱のプロパティは `unsupported-layout`
- **`flex: 1 1 0` の等分は「分割できない一番長い語の幅」までしか縮まない。** URL・ハッシュ・
  長い英単語が入るとその子だけ広がり、合計が親を超えるとはみ出す。`overflow-wrap: anywhere` を
  付けて直す（**`break-word` は効かない**）

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

## 診断（v0.1.0）

```
error[unsupported-value] at 82:77: `border-style: dashed` is not supported (supported: solid, none)
error[unsupported-property] at 84:6: `box-sizing` is not a supported property
  hint: content-box only: subtract padding and border from `width` / `height`
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
  `system-ui` / `ui-sans-serif` は既定フォントで満たすので出ない）。
  警告が出ても PNG は作られ、終了コードは 0
- **`--strict`** を付けると警告 1 件以上で失敗になり、PNG は作られない（配信前の門に使う）
- **`--diagnostics json`** で標準出力に 1 オブジェクト（成功でも失敗でも。人向けの stderr は出ない）。
  `-o` が必須で `--dump-stage` と併用不可。形はガイド §6.4
- エラーも警告も無いことは「絵が正しい」ことを意味しない。重なり・文字色と背景色の同化は検出されない

## この skill の版

対応エンジンは **shashoku 0.1.0**。`shashoku --version` が別の版を出したら、その版の
`docs/guide/` を見てください（ガイドとエンジンの版はそろえて配布しています）。
