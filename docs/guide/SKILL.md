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
2. **骨格は [examples/](examples/) から写す**。12 個の定石（カード・横並び・表・箇条書き・
   タグ・縦中央揃え・左右の位置合わせ・見出しと本文・OG 画像・縦書き・ルビ・画像）が、
   `exit 0`・警告 0 で PNG になることを確かめた形で置いてある
3. **CLI で描く**（下記）
4. **失敗したらガイド §6**（エラーの読み方・直す順序・現状と予定）
5. **成功しても PNG を開いて目で見る**。エラーが無いことは絵が正しいことを意味しない

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
shashoku --version                                              # 版（この skill は 0.1.0 向け）
```

押さえておく点（詳細はガイド §7）:

- `--width` の既定は 1200。**`--height` を省くと内容の高さに追従する**（縦書きでは必須）
- 画像は **PNG のみ**。`--image 名前=ファイル` で渡し、HTML には `<img src="名前">` と書く
  （URL でもパスでもない）
- フォントは `--font`（**指定順がフォールバック順**）。省略すると埋め込みの既定フォント。
  HTML 側に Web フォントを書くことはできない
- 終了コードは 0 成功 / 1 レンダリング・入出力の失敗 / 2 引数の誤り
- **成功時は何も出力しない**（v0.1.0）。`--height` を省いたときの実際の高さは PNG から読む
- 同じ入力からは常にバイト単位で同じ PNG が出る

## 診断（v0.1.0 の現状）

```
error[unsupported-value] at 82:77: `border-style: dashed` is not supported (supported: solid, none)
warning[missing-glyph]: no font has a glyph for U+1F600 at 1:6
```

- 機械が頼ってよいのは **識別子**（`unsupported-value` など）と **位置**（`行:桁`）。文面は変わりうる
- **エラーは 1 回に 1 件しか出ない。** 同じ誤りが何か所にあっても 1 件ずつなので、
  直しては再実行するループになる
- 警告は `missing-glyph`（豆腐）だけ。**紙面からのはみ出しは検出されない**
- 一括報告・`hint:` の行・`warning[content-overflow]`・`--strict`・`--diagnostics json` は
  設計済みで**未実装**（ガイド §6.4 と ARCHITECTURE.md A46）

## この skill の版

対応エンジンは **shashoku 0.1.0**。`shashoku --version` が別の版を出したら、その版の
`docs/guide/` を見てください（ガイドとエンジンの版はそろえて配布しています）。
