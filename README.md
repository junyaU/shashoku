# shashoku（写植）

**日本語の文章を絶対に破綻させずに、HTML から PNG を一発で生成する組版エンジン。**

禁則処理・縦書き・ルビ・フォントフォールバックを備え、関数 1 個で PNG バイト列を返す C++23 ライブラリ。
OG 画像のように「任意の日本語文字列を流し込んでも組版が壊れない」ことを保証するのが目的。

> 🚧 **Phase 8 まで実装済み**。HTML → PNG が一気通貫で動きます。
> block / inline / flexbox レイアウト、禁則処理（追い出し・追い込み・ぶら下げ）、
> `<style>` と単純セレクタ、`<img>`、**ルビ**、**縦書き**、フォントフォールバックと豆腐検出、
> scale まで対応。残るは配布まわり（Phase 9）です。
> 設計は [docs/DESIGN.md](docs/DESIGN.md) と [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) を参照。

![OG 画像の例](docs/images/og_card.png)

<sub>[examples/og_card.html](examples/og_card.html) を 1200×630 で組んだもの（この画像は 600×315）。
flexbox で「アイコン + タイトル + フッター」を組み、行頭に句読点が落ちないことを保証します。</sub>

## 縦書きとルビ

`writing-mode: vertical-rl` で縦組みになります。約物は縦組み用の字形に差し替わり、
欧文と数字は横倒しになり、ルビは親文字の右側に付きます。禁則は縦書きでも同じように働きます。

| 縦書き（[examples/vertical.html](examples/vertical.html)） | ルビ（[examples/ruby.html](examples/ruby.html)） |
|---|---|
| ![縦書きの例](docs/images/vertical.png) | ![ルビの例](docs/images/ruby.png) |

`<ruby>` は `<rt>` を複数書けばモノルビになります（`<ruby>東<rt>とう</rt>京<rt>きょう</rt></ruby>`）。
`<rp>` は解釈されますが描画されません。

```bash
# 縦書きは block 方向が横なので --height が必須
shashoku examples/vertical.html --font NotoSansJP-Regular.otf -o v.png --width 520 --height 560
```

## 使い方

### C++

```cpp
#include "shashoku/shashoku.hpp"

shashoku::FontSet fonts;
fonts.add(noto_sans_jp_bytes);   // 追加順がフォールバック順。バイト列で渡す

shashoku::ImageSet images;
images.add("icon", icon_png_bytes);   // <img src="icon"> で引ける

shashoku::RenderOptions options;
options.viewport_width = 1200;
options.viewport_height = 630;        // 未指定なら内容の高さに追従する
options.line_break.overflow = shashoku::OverflowPolicy::Burasage;  // 既定は追い出し

const auto result = shashoku::render(html, fonts, images, options);
if (!result) {
  std::cerr << shashoku::to_string(result.error()) << '\n';  // error[unsupported-property] at 3:14: ...
  return 1;
}
for (const shashoku::Warning& w : result->warnings) {   // 豆腐（グリフ欠落）だけは警告
  std::cerr << w.detail << '\n';                        // no font has a glyph for U+1F600
}
write_file("out.png", result->png);                     // PNG のバイト列
```

リンクするのは `shashoku::shashoku` 1 つだけ。`include/shashoku/` のヘッダは公開 API だけを見せ、
FreeType / HarfBuzz や内部の型は一切漏れません。ネットワークにもファイルシステムにも触れないので、
フォントと画像はバイト列で渡します。

同じ入力からは常にバイト単位で同じ PNG が出ます（純粋関数）。

### CLI

```bash
shashoku examples/hello.html --font NotoSansJP-Regular.otf -o out.png --width 600
shashoku examples/og_card.html --font NotoSansJP-Bold.otf --font NotoSansJP-Regular.otf \
  --image icon=icon.png -o og.png --width 1200 --height 630
shashoku input.html --font A.otf --overflow burasage -o out.png   # あふれ処理を選ぶ
shashoku input.html --font A.otf --scale 2 -o out@2x.png          # Retina 向け 2 倍
shashoku input.html --font A.otf --trim-line-start -o out.png     # 行頭の括弧を天付きに
shashoku input.html --font A.otf --dump-stage box                 # 中間表現を見る
```

主なオプション:

| オプション | 意味 |
|---|---|
| `--font <file>` | フォント（複数指定可。**指定順がフォールバック順**） |
| `--image <name>=<file>` | PNG 画像。`<img src="name">` で引く |
| `--width` / `--height` / `--scale` | ビューポート（CSS px）と出力倍率。縦書きでは `--height` 必須 |
| `--overflow` | あふれ処理 `oidashi`（既定）\| `oikomi` \| `burasage` |
| `--line-break` | 行分割の厳しさ `strict`（既定）\| `normal` \| `loose` |
| `--trim-line-start` | 行頭の始め括弧の前の空きを詰める（天付き。既定は詰めない） |
| `--no-trim-line-end` | 行末の終わり括弧・句読点の後ろの空きを詰めない（既定は詰める） |
| `--no-collapse-punctuation` | 連続する約物の間の空きを詰めない（既定は詰める。JLREQ 3.1.4） |
| `--dump-stage` | `dom \| style \| box \| display-list \| svg` で中間表現を出す |

終了コードは 0 成功 / 1 レンダリングエラー・入出力エラー / 2 引数の誤り。

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
| テキスト | `color` `font-size` `font-family` `font-weight` `line-height` `letter-spacing` `text-align`（`justify` 含む）、`line-break`（`auto` \| `strict` \| `normal` \| `loose`）、`overflow-wrap` |
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

## 既知の制限

- **ルビ**: 親文字とルビの幅が違うときは 1:2:1 で配分するだけで、**隣の文字へのはみ出し（ルビの掛け）はしません**。**縦中横**（`text-combine-upright`）もありません
- **flexbox**: 単一行のみ（`flex-wrap` なし）。`align-self` `order` `flex-flow` なし
- **ボックス**: `box-sizing` は content-box のみ。`max-width` / `min-width` なし。枠線と角丸は **4 辺・4 隅共通のみ**（`border-top` や隅ごとの半径は不可）。マージンの相殺は**隣り合う兄弟ブロック間だけ**（親子間はしません）
- **画像**: **PNG のみ**（JPEG / SVG / WebP は非対応）。URL もファイルパスも解釈せず、バイト列で渡します
- **絵文字**: カラー絵文字フォント（CBDT / sbix / COLR）は描けません。グリフが無ければ **□ を描いて警告**を返します（`WarningKind::MissingGlyph`）

## やらないこと

JavaScript の実行 / 外部リソースの取得（URL 参照の画像・Web フォント。すべてバイト列で渡してもらいます） /
grid・float・table・`position` / アニメーション / メディアクエリ / CJK 以外の複雑スクリプトのシェーピング品質保証 /
ブラウザとのピクセル一致（目標は「正しい日本語組版」であって「Chrome の再現」ではありません）。

詳細は [DESIGN.md §4](docs/DESIGN.md)。

## ビルド

必要なもの: CMake 3.22+ / Ninja / clang-18 + libc++-18（C++23 の `std::expected` を使うため）

```bash
# Ubuntu 22.04（apt.llvm.org の llvm-toolchain-jammy-18 リポジトリが必要）/ 24.04
sudo apt install ninja-build clang-18 clang-format-18 clang-tidy-18 libc++-18-dev libc++abi-18-dev pngcheck
```

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

GCC を使う場合は 13 以上: `CXX=g++-14 cmake --preset gcc`
