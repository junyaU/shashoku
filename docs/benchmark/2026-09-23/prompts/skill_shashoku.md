# shashoku で PNG にできる HTML / CSS の範囲（AI 向けの説明。README から抜粋、2026-09-23 main c157766）

shashoku は日本語組版特化の HTML→PNG エンジン。ブラウザではない。**対応表にないタグ・属性・プロパティ・値はエラーになる**（黙って無視しない）。
入力は HTML の断片（`<html>` `<head>` `<body>` `<!DOCTYPE>` は書かない。書くとエラー）。`<style>` 要素と `style` 属性は使える。文字コードは UTF-8。

## CLI

```
shashoku <input.html> [--image <name>=<file.png>]... -o <out.png> --width <N> [--height <N>] [--scale <S>]
```
- `--width` はビューポートの幅（CSS px、既定 1200）。`--height` を省くと内容の高さに追従する。縦書きは `--height` 必須
- 画像は `<img src="name">` と `--image name=file.png` で対応づける（URL やパスは解釈しない。PNG のみ）
- フォントは埋め込みの既定フォント（Noto Sans JP Regular / Bold）。`--font <otf>` で差し替え可。絵文字フォントは無い
- 中間表現のダンプ: `--dump-stage dom|style|box|display-list|svg`

## 対応している HTML / CSS

対応表にないタグ・属性・プロパティ・値は、**黙って無視せずエラーになります**（fail loudly）。

### タグ

| | |
|---|---|
| 対応 | `div` `span` `p` `h1`〜`h6` `img` `br` `ruby` `rt` `rp` `style` |
| 属性 | 共通: `style` `class` `id` ／ `img`: `src` `width` `height` `alt` |

入力は断片で構いません（`<html>` や `<body>` は不要、というより**書くとエラー**）。文字コードは UTF-8 のみ。

### CSS

セレクタは `tag` `.class` `#id` とその結合（`p.note`）、カンマ区切りのみ。
`<style>` 要素と `style` 属性の両方が使えます（カスケードは UA < `<style>` < `style` 属性）。

| 分類 | プロパティ |
|---|---|
| ボックス | `display`（`block` \| `flex` \| `inline` \| `none`）、`width` `height`、`margin`（`auto` 可）、`padding`、`border`（`<幅> solid <色>` \| `none`）、`border-width` `border-style` `border-color` `border-radius`、`background` / `background-color` |
| flexbox | `flex-direction` `justify-content` `align-items` `gap` `row-gap` `column-gap` `flex` `flex-grow` `flex-shrink` `flex-basis` |
| テキスト | `color` `font-size` `font-family` `font-weight` `line-height` `letter-spacing` `text-align`（`justify` 含む）、`line-break`（`auto` \| `strict` \| `normal` \| `loose`）、`overflow-wrap`（`word-wrap` は別名） |
| 縦書き | `writing-mode`（`horizontal-tb` \| `vertical-rl`） |

単位は `px` `em` と単位なしの `0`。`%` は `width` と `flex-basis` のみ。
色は `#rgb` `#rgba` `#rrggbb` `#rrggbbaa`、`rgb()` `rgba()`、CSS の色名、`transparent`、`currentColor`（`border-color` のみ）。

> **⚠️ `box-sizing` は content-box のみ**（プロパティ自体が対応外）。
> `width` / `height` は **padding と border を含まない**値なので、
> 1200×630 の OG 画像を padding 80px で作るなら `width: 1040px; height: 470px; padding: 80px;`
> と書きます（1040 + 80×2 = 1200、470 + 80×2 = 630）。
> 枠線があればその幅も 2 辺ぶん引いてください。

> **⚠️ 縦書きの約束事**。`writing-mode` を書けるのは**トップレベル要素だけ**で、
> 途中で向きを変えること（直交フロー）はできません。縦書きでは内容が横に伸びるので
> **出力の高さ（`viewport_height` / `--height`）が必須**です。
> `width` は「行送り方向 = 横」、`height` は「字送り方向 = 縦」の大きさを指します。
> 縦書きでの `width: %` は非対応です。

## 決定性（同じ入力から同じ PNG）

shashoku は純粋関数です（[DESIGN.md §3-5](docs/DESIGN.md)）。グローバル状態・時刻・乱数・
ネットワーク・ロケールを使わず、`render()` の呼び出しをまたぐキャッシュも持ちません。
ただし「バイト単位で同じ」がどこまで確かめてあるかは、**環境によって違います**。

**保証するもの**

- 同じ shashoku の版（= 同じ依存の版。zlib / FreeType / HarfBuzz はすべてバージョンと SHA256 を
  固定して取得します）・同じ HTML・同じフォントのバイト列・同じ画像のバイト列・同じ `RenderOptions`
  からは、**同じ PNG のバイト列**が出ます。同じプロセスで何度呼んでも、プロセスを分けても同じです
- 上限（`RenderLimits`）も入力の一部です。**超過しない限り、緩めても出力は 1 ビットも変わりません**
- **ピクセルの一致**は、x86-64 Linux の 2 つのツールチェーン（clang-18 + libc++ / gcc-14 + libstdc++）で
  CI が検査しています（4 ジョブが同じ `tests/golden/*.png` に通っています）
- PNG のバイト列のうち **shashoku 自身が決める部分**（IHDR・行ごとのフィルタの選択・フィルタ後の
  走査線・チャンクの並びと CRC）は、期待値をリポジトリに固定して同じ 2 つのツールチェーンで
  検査しています（`tests/png/determinism_test.cpp`）

**保証しないもの**（確かめていないので約束しません）

- **x86-64 Linux 以外**: aarch64 / macOS / Windows・MSVC では何も検査していません。
  浮動小数点は `-ffast-math` を禁止し `-ffp-contract=off` を指定したうえ、レイアウトとラスタライズでは
  `+ - * /` `sqrt` `floor / ceil / round / trunc` `min / max / abs` しか使っていません（libm の版で
  結果が変わる `pow` / `exp` / `sin` などは使いません）が、**「だから一致する」ところまでは
  確かめていません**
- **依存ライブラリの版を変えた場合**: zlib の圧縮出力も FreeType のラスタライズ結果も版で変わります。
  版を上げたらゴールデン画像は作り直しになります
- **zlib の deflate 出力そのもの**: 版と設定（圧縮レベルなど）で決まります。設定を変えれば、
  ピクセルは同じままバイト列だけが変わります。ここは期待値を固定していません
- **別のフォントファイル**: 同じ「Noto Sans JP」でも版が違えばグリフの輪郭が変わります。
  ゴールデン画像を持つなら、フォントもハッシュで固定してください（このリポジトリは
  `cmake/TestAssets.cmake` でそうしています）
- **既定フォントの版**: `--font` を省いたときに使われる埋め込みフォントの版が変われば、
  同じ HTML からでも出力は変わります。どの版で組んだかは `shashoku --version` が出します
  （リリースごとにコミット SHA で固定しています）

## 既知の制限

- **ルビ**: 親文字とルビの幅が違うときは JLREQ 3.3.6 の 1:2:…:2:1 で配り、はみ出したぶんは隣の**仮名**にだけ掛けます（JLREQ 3.3.8。漢字・欧文・数字・約物には掛けません）。`<rt>` に `letter-spacing` は効きません（ARCHITECTURE.md A37）。**縦中横**（`text-combine-upright`）はありません
- **flexbox**: 単一行のみ（`flex-wrap` なし）。`align-self` `order` `flex-flow` なし
- **ボックス**: `box-sizing` は content-box のみ。`max-width` / `min-width` なし。枠線と角丸は **4 辺・4 隅共通のみ**（`border-top` や隅ごとの半径は不可）。マージンの相殺は**隣り合う兄弟ブロック間だけ**（親子間はしません）
- **画像**: **PNG のみ**（JPEG / SVG / WebP は非対応）。URL もファイルパスも解釈せず、バイト列で渡します
- **絵文字**: カラー絵文字フォント（CBDT / sbix / COLR / OT-SVG）は**色では描けません**。ビットマップ専用のフォント（CBDT / sbix）は読み込みでエラーにします。COLR は**ベースの輪郭があればその輪郭を単色で**描き、輪郭が無い（色レイヤーだけで絵を作る）グリフは **□ を描いて警告**を返します（`WarningKind::MissingGlyph`）。グリフが無いときも同じです

## やらないこと

JavaScript の実行 / 外部リソースの取得（URL 参照の画像・Web フォント。すべてバイト列で渡してもらいます） /
grid・float・table・`position` / アニメーション / メディアクエリ / CJK 以外の複雑スクリプトのシェーピング品質保証 /
ブラウザとのピクセル一致（目標は「正しい日本語組版」であって「Chrome の再現」ではありません）。

詳細は [DESIGN.md §4](docs/DESIGN.md)。

ピクセル一致は目指しませんが、**組み合わせで情報が落ちていないか**を見るために Chrome（headless）と
構造を突き合わせる仕組みはあります（欠落 / 重なり / 改行位置 / はみ出しの 4 つ）。
ローカル専用で CI には入れていません: [docs/chrome_compare.md](docs/chrome_compare.md)。

