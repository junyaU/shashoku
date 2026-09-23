# shashoku で PNG になる HTML の書き方

**対応エンジン: shashoku 0.1.0**（`shashoku --version` の版と合わせて使ってください。
版が違うと対応範囲が違います）

shashoku は HTML を PNG にするエンジンです。ブラウザではありません。
**対応表にないタグ・属性・プロパティ・値は、黙って無視されずエラーになります**（fail loudly）。
だから「ブラウザで動く HTML」ではなく「この文書の範囲の HTML」を書いてください。
範囲内で書けば 1 回で PNG になります。

---

## 0. この文書の使い方

3 通りの使い方を想定しています。

1. **AI への指示に丸ごと貼る** — 「この範囲で HTML を書いて」と言って、この文書をそのまま渡す
2. **開発時にテンプレートを作る** — §4 の定石から骨格を作り、あとは文字列を差し替えるだけにする
3. **サーバーで毎回作らせる** — LLM にこの文書を system prompt として渡し、出てきた HTML を
   CLI に通す。失敗したらエラー（§6）をそのまま LLM に返して直させる

**盛らないでください。** ここに書いていないプロパティは対応していません。
迷ったら §5 の代替表を見てください。

---

## 1. 入力の形

- **断片で書く**。`<!DOCTYPE>` `<html>` `<head>` `<body>` は**書くとエラー**になります
- 文字コードは **UTF-8 のみ**
- スタイルは `<style>` 要素と `style` 属性の両方が使えます
  （カスケードは UA 既定 < `<style>` < `style` 属性）。`<style>` は何個あっても構いません
- HTML コメント `<!-- … -->` は使えます
- 文字参照（`&amp;` `&lt;` `&gt;` `&nbsp;` `&#12354;`）は使えます
- **背景を指定しない領域は透明**（RGBA の 0,0,0,0）になります。
  白い紙が欲しいなら、いちばん外側の要素に `background` を置いてください

最小の入力:

```html
<style>.card { padding: 24px; background: #ffffff; font-size: 16px; }</style>
<div class="card">こんにちは。</div>
```

---

## 2. 対応表

ここに無いものは**すべてエラー**です。

### 2.1 タグと属性

| | |
|---|---|
| タグ | `div` `span` `p` `h1`〜`h6` `img` `br` `ruby` `rt` `rp` `style` |
| 属性（共通） | `style` `class` `id` |
| 属性（`img` のみ追加） | `src` `width` `height` `alt` |

- `<img src="名前">` の**名前**は、CLI の `--image 名前=ファイル.png` で渡した名前です。
  URL でもファイルパスでもありません。画像は **PNG のみ**
- `<ruby>` は `<rt>` ごとに 1 組になります（`<ruby>東<rt>とう</rt>京<rt>きょう</rt></ruby>` でモノルビ）。
  `<rp>` は解釈されますが描画されません。`<ruby>` の中に `<ruby>` `<img>` `<br>` は入れられません
- `<span>`（インライン）の中にブロック級の箱（`display: block` / `flex` の要素）は入れられません

### 2.2 セレクタ

使えるのは**単一の複合セレクタ**と、そのカンマ区切りだけです。

| 使える | 例 |
|---|---|
| タグ | `p` |
| クラス | `.note` |
| ID | `#main` |
| 全称 | `*` |
| 上の組み合わせ（間に空白を入れない） | `p.note` `.a.b` `div.card#main` |
| カンマ区切り | `h1, h2, .lead` |

**使えないもの**: 子孫セレクタ（`.card p`）、`>` `+` `~`、属性セレクタ（`[href]`）、
擬似クラス・擬似要素（`:hover` `::before`）、`@media` `@font-face` などの at-rule、`!important`、
CSS 変数（`--x`）、`calc()`。

> 子孫セレクタが無いので、**スタイルを当てたい要素にはクラスを直接書いてください**。

### 2.3 プロパティ

| 分類 | プロパティ |
|---|---|
| ボックス | `display` `width` `height` `margin` `padding` `border` `border-width` `border-style` `border-color` `border-radius` `background` `background-color` |
| margin / padding の個別辺 | `margin-top` `margin-right` `margin-bottom` `margin-left`、`padding-top` `padding-right` `padding-bottom` `padding-left` |
| flexbox | `flex-direction` `justify-content` `align-items` `gap` `row-gap` `column-gap` `flex` `flex-grow` `flex-shrink` `flex-basis` |
| テキスト | `color` `font-size` `font-family` `font-weight` `line-height` `letter-spacing` `text-align` `line-break` `overflow-wrap`（`word-wrap` は別名） |
| 縦書き | `writing-mode` |

- **ショートハンドは使えます**: `margin: 10px` `margin: 10px 20px` `margin: 10px 20px 30px 40px`、
  `padding` も同じ。`border: 1px solid #ccc`、`flex: 1 1 0`、`flex: none`、`gap: 8px 16px`、
  `background: #eef`（色だけ）
- **辺ごとの border はありません**（`border-top` `border-left` などは非対応）。
  罫線は `height: 1px; background: <色>` の `div` で引いてください（§4.3）
- **隅ごとの border-radius はありません**（`border-radius: 8px` のように 1 値だけ）
- `margin: auto` は使えます（左右中央寄せ）

### 2.4 値・単位・色

| 項目 | 対応する値 |
|---|---|
| 長さ | `<数値>px` / `<数値>em` / 単位なしの `0` |
| `%` | **`width` と `flex-basis` だけ**（縦書きでは `width` の `%` も不可） |
| `display` | `block` `flex` `inline` `none`（**`inline-block` は無い**） |
| `flex-direction` | `row` `column` |
| `justify-content` | `flex-start` `flex-end` `center` `space-between` `space-around` `space-evenly` |
| `align-items` | `stretch`（既定）`flex-start` `flex-end` `center`（**`baseline` は無い**） |
| `text-align` | `start` `end` `left` `right` `center` `justify` |
| `font-weight` | `normal`（400）`bold`（700）、`100`〜`900` の 100 刻み（`bolder` / `lighter` は不可） |
| `line-height` | **単位なしの数値（`1.8`）／ `<数値>px` ／ `<数値>em` ／ `normal`** |
| `border-style` | **`solid` と `none` だけ**（`dashed` `dotted` は不可） |
| `writing-mode` | `horizontal-tb` `vertical-rl` |
| `line-break` | `auto` `strict` `normal` `loose` |
| `overflow-wrap` | `normal` `anywhere` `break-word` |
| 色 | `#rgb` `#rgba` `#rrggbb` `#rrggbbaa`、`rgb()` `rgba()`、CSS の色名、`transparent`、`currentColor`（`border-color` のみ） |

> `line-height` は**単位なしの数値が使えます**。単位なしは「倍率が継承され、子はその
> `font-size` を基準に計算する」、`em` は「計算済みの px が継承される」という違いがあるので、
> 文字サイズの違う子を持つ要素では**単位なし**を選んでください。
> `font-size` はキーワード（`large` など）を受け付けません。

### 2.5 UA の既定スタイル

自分で上書きしない限り、次が効いています（要らなければ自分で `margin: 0` を書いてください）。

```css
div, p, h1, h2, h3, h4, h5, h6 { display: block }
span, ruby, rt, img, br        { display: inline }
style, rp                      { display: none }
h1 { font-size: 2em;    font-weight: bold; margin: 0.67em 0 }
h2 { font-size: 1.5em;  font-weight: bold; margin: 0.83em 0 }
h3 { font-size: 1.17em; font-weight: bold; margin: 1em 0 }
h4 { font-size: 1em;    font-weight: bold; margin: 1.33em 0 }
h5 { font-size: 0.83em; font-weight: bold; margin: 1.67em 0 }
h6 { font-size: 0.67em; font-weight: bold; margin: 2.33em 0 }
p  { margin: 1em 0 }
rt { font-size: 0.5em }
```

マージンの相殺は**隣り合う兄弟ブロックの間だけ**です（親子の間では相殺しません）。

---

## 3. 先に知っておく 5 つの落とし穴

検証で AI が実際に引っかかった順です。**ここだけで反復の大半が消えます。**

### (1) `box-sizing` は content-box だけ

`width` / `height` は **padding と border を含みません**。外寸を決めたいときは引き算します。

```
1200×630 の OG 画像、padding 64px、枠なし
  → width: 1072px;  height: 502px;  padding: 64px;
     （1072 + 64×2 = 1200、502 + 64×2 = 630）
枠 2px を足すなら、さらに 2×2 = 4px を引く
  → width: 1068px;  height: 498px;  padding: 64px;  border: 2px solid #333;
```

### (2) `display: inline` の要素に箱のプロパティは付かない

`span`（と、`display` を書かなかったインライン要素）には
`width` `height` `margin` `padding` `border*` `border-radius` を**付けられません**。
付けると `error[unsupported-layout]` になります。

```html
<!-- だめ -->
<span style="padding: 4px 8px; background: #eef">タグ</span>
<!-- よい: inline に効くのは色と文字まわりだけ -->
<span style="background: #eef; color: #14509b">強調</span>
```

インラインに効くのは `color` `background-color` `font-size` `font-family` `font-weight`
`letter-spacing` などです。`font-size` の違う `span` は**ベースラインで揃います**。

### (3) flex 項目の `span` は block 化されない

ブラウザでは flex コンテナの子は自動で block 級になりますが、**shashoku はしません**。
`display: inline` のままなので、(2) の制限がそのまま効きます。

> **flex コンテナの子は、かならず `div` にしてください。**

### (4) `display: inline-block` が無い → 「flex の親 + `flex: none` の子」

内容ぶんの幅しか取らない箱（タグ・pill・バッジ・スタンプ）はこう作ります。

```html
<div style="display: flex; gap: 8px">
  <div style="flex: none; padding: 5px 12px; border-radius: 13px; background: #e7f0ff">政策</div>
  <div style="flex: none; padding: 5px 12px; border-radius: 13px; background: #e7f0ff">AI</div>
</div>
```

`flex: none`（= `flex: 0 0 auto`）を書かないと、幅が足りないときに既定の `flex-shrink: 1` で縮み、
中の文字が折り返します。逆に `flex: none` は縮まないので、入りきらなければ親からはみ出します
（**親の箱からのはみ出しは検出されません**。紙面の外まで出れば `warning[content-overflow]` が
出ます。§6.4）。

### (5) `border-radius` は中身をクリップしない（`<img>` だけは例外）

角丸の箱の中で子に背景色を敷くと、**角から四角くはみ出して見えます**（`overflow` が無いため）。
外枠を角丸にしたいなら、背景を敷くのは外枠だけにしてください。
`<img>` は唯一の例外で、`border-radius` が画像そのものをクリップします（真円のアイコンが作れます）。

---

## 4. 書き方の定石

すべて [`examples/`](examples/) にそのまま動く形で置いてあり、
`exit 0`・警告 0 で PNG になることを確かめてあります。

### 4.1 カード（余白・角丸・枠線）— [card.html](examples/card.html)

```html
<style>
  .card  { padding: 24px; border: 1px solid #dfe3e8; border-radius: 12px; background: #ffffff; }
  .title { margin: 0 0 8px 0; font-size: 20px; font-weight: bold; color: #1b2733; }
  .body  { margin: 0; font-size: 15px; line-height: 1.8; color: #44546a; }
</style>
<div class="card">
  <p class="title">冪等性</p>
  <p class="body">同じ操作を何回行っても、1 回だけ行ったときと同じ結果になる性質のこと。通信の再試行を安全にする。</p>
</div>
```

### 4.2 横並び（flex）— [row.html](examples/row.html)

```html
<style>
  .row  { display: flex; gap: 16px; background: #ffffff; padding: 16px; }
  .cell { flex: 1 1 0; padding: 12px; background: #f1f4f9; border-radius: 8px;
          font-size: 15px; line-height: 1.7; color: #1b2733; }
</style>
<div class="row">
  <div class="cell">受け取る<br>HTML とフォント</div>
  <div class="cell">組む<br>行分割・約物・ルビ</div>
  <div class="cell">出す<br>PNG のバイト列</div>
</div>
```

`flex: 1 1 0` で等分になります（CSS grid の `1fr` 相当）。`flex: 1` は同じ意味の短縮形です
（`flex: 1 1 0` に展開されます）。内容ぶんの幅にしたいときは `flex: none`、
幅を決め打ちにしたいときは `flex: none; width: 180px`。

### 4.3 表（flex の行 + 1px の罫）— [table.html](examples/table.html)

`<table>` は対応外です。**行を `display: flex` の `div`、セルを `flex: 1 1 0` の `div`** にします。
辺ごとの `border` が無いので、横罫は **高さ 1px の `div`** で引きます
（セルごとに枠を引くと隣り合う罫が 2 重になります）。

```html
<style>
  .tbl  { border: 1px solid #ccd3dd; background: #ffffff; font-size: 15px; }
  .tr   { display: flex; }
  .th   { background: #2f3b52; color: #ffffff; font-weight: bold; }
  .odd  { background: #f5f7fb; }
  .td   { flex: 1 1 0; padding: 10px 12px; color: #1b2733; line-height: 1.7; }
  .rule { height: 1px; background: #e3e8ef; }
</style>
<div class="tbl">
  <div class="tr th"><div class="td">項目</div><div class="td">既定</div><div class="td">備考</div></div>
  <div class="rule"></div>
  <div class="tr"><div class="td">幅</div><div class="td">1200px</div><div class="td">--width で変える</div></div>
  <div class="rule"></div>
  <div class="tr odd"><div class="td">高さ</div><div class="td">内容に追従</div><div class="td">--height で固定</div></div>
</div>
```

`align-items` の既定は `stretch` なので、**同じ行のセルの高さは自動でそろいます**（縞模様が崩れません）。
列幅を変えたいときは `flex: 1 1 0` の代わりに `flex: none; width: 180px` や `flex: 2 1 0` を使います。

### 4.4 箇条書き（flex + 丸）— [list.html](examples/list.html)

`<ul>` `<li>` `list-style` は対応外です。丸は小さい `div` で描き、
`align-items: flex-start` + `margin-top` で 1 行目の中心に合わせます。

```html
<style>
  .list { background: #ffffff; padding: 16px; }
  .item { display: flex; align-items: flex-start; gap: 10px; margin: 0 0 10px 0; }
  .dot  { flex: none; width: 7px; height: 7px; margin: 10px 0 0 0;
          border-radius: 4px; background: #2f6fd0; }
  .text { flex: 1 1 0; font-size: 15px; line-height: 1.8; color: #1b2733; }
</style>
<div class="list">
  <div class="item"><div class="dot"></div><div class="text">対応表にないタグ・プロパティ・値はエラーになる。</div></div>
  <div class="item"><div class="dot"></div><div class="text">高さを省くと内容に追従する。縦書きでは高さが必須。</div></div>
  <div class="item"><div class="dot"></div><div class="text">同じ入力からは常にバイト単位で同じ PNG が出る。</div></div>
</div>
```

`margin-top` のめやすは `(font-size × line-height − 丸の直径) ÷ 2` です
（上の例は (15 × 1.8 − 7) ÷ 2 ≒ 10px）。

### 4.5 タグ / pill（内容幅の箱）— [pill.html](examples/pill.html)

```html
<style>
  .tags { display: flex; gap: 8px; align-items: center; background: #ffffff; padding: 16px; }
  .pill { flex: none; padding: 5px 12px; border-radius: 13px;
          background: #e7f0ff; color: #14509b; font-size: 14px; }
  .pill-strong { flex: none; padding: 5px 12px; border-radius: 13px;
                 background: #14509b; color: #ffffff; font-size: 14px; font-weight: bold; }
</style>
<div class="tags">
  <div class="pill-strong">新着</div>
  <div class="pill">政策</div>
  <div class="pill">AI</div>
</div>
```

`flex-wrap` が無いので**折り返しません**。数が多いときは行ごとに `.tags` を分けてください。

### 4.6 縦中央揃え — [vcenter.html](examples/vcenter.html)

2 通りあります。

1. **並べたものの上下中央** → flex の親に `align-items: center`
2. **箱の中の 1 行だけを上下中央** → `line-height` を箱の `height` と**同じ px** にする

```html
<style>
  .step  { display: flex; align-items: center; gap: 14px; background: #ffffff; padding: 16px; }
  .badge { flex: none; width: 38px; height: 38px; border-radius: 19px; background: #2f6fd0;
           color: #ffffff; font-size: 18px; font-weight: bold;
           text-align: center; line-height: 38px; }
  .label { flex: 1 1 0; font-size: 17px; line-height: 1.7; color: #1b2733; }
</style>
<div class="step">
  <div class="badge">3</div>
  <div class="label">依存ライブラリを取得する（初回だけ時間がかかります）</div>
</div>
```

**箱いっぱいの縦中央**は `display: flex; flex-direction: column; justify-content: center` と、
親に明示した `height`（content-box なので padding を引いた値）で作ります。

### 4.7 左右の位置合わせ・注記の位置決め — [space-between.html](examples/space-between.html)

`position` が無いので、位置は**流れの中で作ります**。

```html
<style>
  .bar   { display: flex; justify-content: space-between; align-items: center;
           padding: 14px 18px; background: #2f3b52; color: #ffffff; font-size: 15px; }
  .right { color: #a9c4ee; }
  .foot  { display: flex; align-items: center; padding: 14px 18px;
           background: #ffffff; font-size: 14px; color: #44546a; }
  .grow  { flex: 1 1 0; }
</style>
<div class="bar"><div>週次レポート</div><div class="right">9/14〜9/20</div></div>
<div class="foot"><div>架空新聞</div><div class="grow"></div><div>2026-09-22</div></div>
```

- 両端に寄せる → `justify-content: space-between`
- 一方だけ右に押す → 伸びる空の `div`（`flex: 1 1 0`）を挟む
- **特定の要素の真下に注記を置く** → 注記の前に `flex: none; width: <その位置まで>px` の空の `div` を置く。
  幅は自分で足し算します（content-box なので `padding` と `border` も足す）。
  上の要素の寸法を 1px でも変えたらこの値も直してください

### 4.8 見出しと本文 — [heading.html](examples/heading.html)

```html
<style>
  .doc  { padding: 24px; background: #ffffff; }
  h1    { margin: 0 0 6px 0; font-size: 26px; color: #1b2733; line-height: 1.45; }
  .lead { margin: 0 0 14px 0; font-size: 14px; color: #6b7a90; }
  p     { margin: 0 0 12px 0; font-size: 15px; line-height: 1.9; color: #2c3a4d; }
</style>
<div class="doc">
  <h1>政府、AI 生成コンテンツの表示義務化を検討</h1>
  <p class="lead">架空新聞 2026-09-22</p>
  <p>表示義務の対象は、広告と報道に用いる画像・音声・動画を想定している。違反した場合の措置は今後の議論に委ねられる。</p>
</div>
```

`h1`〜`h6` と `p` には UA 既定の `margin` が付きます（§2.5）。余白を自分で決めるなら上書きしてください。
和文の本文は `line-height: 1.7`〜`1.9` が読みやすい範囲です。

### 4.9 OG 画像 1200×630 の骨格 — [og-card.html](examples/og-card.html)

```html
<style>
  .card  { display: flex; flex-direction: column; width: 1072px; height: 502px;
           padding: 64px; background: #12263f; color: #f1f3f5; }
  .site  { font-size: 30px; color: #a5d8ff; }
  .title { flex: 1 1 0; margin: 32px 0 0 0; font-size: 62px; font-weight: bold; line-height: 1.35; }
  .foot  { display: flex; justify-content: space-between; font-size: 26px; color: #adb5bd; }
</style>
<div class="card">
  <div class="site">junya.dev</div>
  <div class="title">Rust で書き直したら速くなった、と言うために測ったこと</div>
  <div class="foot"><div>2026年9月23日</div><div>計測・ベンチマーク</div></div>
</div>
```

`shashoku og-card.html -o og.png --width 1200 --height 630` で出します。
`1072 = 1200 − 64×2`、`502 = 630 − 64×2`（§3-(1)）。
タイトルに `flex: 1 1 0` を付けると、**タイトルが伸びてフッターが下端に張り付きます**。

### 4.10 縦書き — [vertical.html](examples/vertical.html)

```html
<style>
  .sheet  { writing-mode: vertical-rl; width: 400px; height: 820px; padding: 40px;
            background: #f7f3e8; color: #23201a; }
  .poem   { margin: 0; font-size: 25px; line-height: 2; }
  .author { margin: 0 28px 0 0; padding: 560px 0 0 0; font-size: 16px; color: #6b6355; }
</style>
<div class="sheet">
  <div class="poem">ゆふぐれの　駅のホームに　立ちつくし　届かぬ返事を　もう一度読む</div>
  <div class="author">架空　花</div>
</div>
```

約束事:

- `writing-mode` を書けるのは**いちばん外側の要素だけ**です。途中で向きは変えられません
  （子に同じ値を書き足すのは無害ですが、違う値を書くと `error[unsupported-layout]`）
- **`--height` が必須**です（省くと `error[invalid-option]`）。省略時の内容追従は横書きだけの機能です
- 縦書きでの `width: %` は使えません
- `width` は**行送り方向（横）**、`height` は**字送り方向（縦）**の大きさです
- **`margin` / `padding` は物理方向のままです**（`top` は上、`right` は右）。縦書きでは
  「行は右から左」「字は上から下」なので、意味はこうなります:

  | 書き方 | 縦書きでの意味 |
  |---|---|
  | `padding-top` | 行の**先頭**側（字送りの始め）の空き |
  | `padding-bottom` | 行の**末尾**側の空き |
  | `padding-right` | **1 行目の外側**（紙の右端との間）の空き |
  | `padding-left` | 最終行の外側の空き |
  | `margin-right` | **前のブロック**（右隣）との間 |
  | `margin-left` | **次のブロック**（左隣）との間 |

- **1 行に入る字数のめやす** = `(height − padding上下) ÷ font-size`。
  上の例は `(900 − 40×2) ÷ 25 = 32.8` 字で、歌の 32 字が 1 行に収まります。
  収めたい字数が決まっているなら `font-size` を逆算してください
- **行の太さ** = `font-size × line-height`。行数 × これが `width` に収まる必要があります
- 縦中横（`text-combine-upright`）はありません

### 4.11 ルビ — [ruby.html](examples/ruby.html)

```html
<style>
  .doc  { padding: 24px; background: #ffffff; font-size: 20px; line-height: 1.8; color: #1b2733; }
  .mono { margin: 12px 0 0 0; font-size: 20px; line-height: 1.8; }
</style>
<div class="doc">
  <ruby>冪等性<rt>べきとうせい</rt></ruby>とは、同じ操作を何回行っても結果が変わらない性質のことをいいます。
  <p class="mono">モノルビ: <ruby>東<rt>とう</rt>京<rt>きょう</rt></ruby></p>
</div>
```

- `<rt>` の大きさは UA 既定で親の `0.5em` です
- **ルビのある行は行ボックスが自動で広がる**ので、上の行と重なることはありません。
  ただし `line-height: 1.0` 前後だと窮屈に見えます。**`line-height: 1.8` 前後**を目安にしてください
- 親文字とルビの幅が違うときは JLREQ 3.3.6 の 1:2:…:2:1 で配り、はみ出したぶんは隣の**仮名**にだけ掛けます。
  親文字が欧文でもルビは付きます（広めに配られます）
- `<rt>` に `letter-spacing` は効きません

### 4.12 画像 — [image.html](examples/image.html)

```html
<style>
  .head { display: flex; align-items: center; gap: 16px; padding: 20px; background: #ffffff; }
  .icon { width: 72px; height: 72px; border-radius: 36px; }
  .name { font-size: 20px; font-weight: bold; color: #1b2733; }
  .sub  { font-size: 14px; color: #6b7a90; }
</style>
<div class="head">
  <img class="icon" src="icon" alt="アイコン">
  <div>
    <div class="name">shashoku（写植）</div>
    <div class="sub">ブラウザ不要の HTML→PNG エンジン</div>
  </div>
</div>
```

`shashoku image.html --image icon=examples/icon.png -o out.png --width 560` で出します。

- **PNG のみ**（JPEG / SVG / WebP は不可）。URL もファイルパスも解釈しません
- `src` には `--image` で渡した**名前**をそのまま書きます。名前が合わないと `error[image-not-found]`。
  よくある間違いは `src="avatar.png"` と書いて `--image avatar=…` で渡すこと
- `alt` は任意です（書かなくてもエラーになりません。描画にも使われません）
- `<img>` は inline のまま `width` / `height` / `border-radius` を取れる唯一の要素で、
  `border-radius` は**中身をクリップ**します

---

## 5. やってはいけないこと / 代替

| 書きたくなるもの | shashoku では | 代わりに |
|---|---|---|
| `<html>` `<head>` `<body>` `<!DOCTYPE>` | エラー | **書かない**。断片で出す |
| `<ul>` `<li>` `<ol>` | エラー | flex の行 + 丸の `div`（§4.4） |
| `<table>` `<tr>` `<td>` | エラー | flex の行 + `flex: 1 1 0` のセル（§4.3） |
| `<strong>` `<b>` `<em>` `<code>` `<a>` `<section>` `<header>` | エラー | `span`（+ `font-weight` / `color` / `background-color`）または `div` |
| 絵文字（🎉 😀 など） | **□ になって警告** | **使わない**。文字（`→` `↓` `▼` `※`）か、色付きの小さな `div` で代用 |
| `display: inline-block` | エラー | flex の親 + `flex: none` の子（§3-(4)） |
| `position` / `top` / `left` / `z-index` | エラー | flex と `justify-content` / `align-items` / 空の spacer（§4.7） |
| `grid` / `grid-template-columns` | エラー | flex + `flex: 1 1 0`（`1fr` 相当） |
| `float` | エラー | flex |
| `flex-wrap` | エラー | 行ごとに flex コンテナを分ける |
| `align-self` / `order` / `flex-flow` | エラー | 並び順を HTML の順で書く |
| `box-sizing` | エラー | 幅・高さから padding と border を引く（§3-(1)） |
| `min-width` / `max-width` / `min-height` / `max-height` | エラー | 固定値の `width` / `height` |
| `overflow` | エラー | はみ出さない寸法にする。角丸のクリップは諦める |
| `background` のグラデーション（`linear-gradient` ほか） | エラー | **単色**にする |
| `box-shadow` / `text-shadow` | エラー | 影は諦める。境界は 1px の枠線か薄い背景色で表す |
| `opacity` | エラー | 色そのものを薄くする（`#00000099` や淡い色） |
| `transform`（`rotate` など） | エラー | 傾けない |
| `::before` / `::after` + `content` | エラー | **実要素**（`span` / `div`）として書く。ただし `position` が無いので**流れの中に落ちる**ことに注意 |
| `border-top` / `border-left` など辺ごとの枠 | エラー | 高さ（幅）1px の `div` を挟む（§4.3） |
| `border-style: dashed` / `dotted` | エラー | `solid`。破線と実線の描き分けは**色**で代える |
| 隅ごとの `border-radius`（4 値） | エラー | 1 値の `border-radius` |
| `border-collapse` | 無い | セルに枠を付けず、1px の `div` で罫を引く |
| `-webkit-*` / `-moz-*` / `-ms-*` | エラー | **接頭辞を外す**（外した名前が対応表にあれば通ります） |
| Web フォント（`@font-face` / Google Fonts の `<link>`） | エラー | フォントは CLI の `--font` で渡す。HTML 側には書かない |
| `@media` / `@import` / CSS 変数 / `calc()` / `!important` | エラー | 値を直接書く |
| 子孫セレクタ（`.card p`） | エラー | 当てたい要素にクラスを直接書く |
| JPEG / SVG / WebP の画像 | エラー | PNG に変換して `--image` で渡す |

### 削ると危険な組み合わせ

**対応外の宣言を消すときは、対になっている宣言も一緒に消してください。**
片方だけ消すと、エラーは出ないのに絵が壊れます。

| 組 | 片方だけ消すと |
|---|---|
| `background-clip: text` + `color: transparent` | **文字が完全に消える**。`background-clip` を外すなら `color` も実際の色に戻す |
| `-webkit-text-fill-color: transparent` + グラデーション背景 | 同上 |
| `position: absolute` + `top` / `left` | `position` だけ外すと、その箱が**流れの先頭に大きく出てくる**。飾りなら要素ごと消す |
| `overflow: hidden` + `border-radius` | `overflow` を外すと角が四角くはみ出す。背景を外枠だけにする |

### 日本語の組版で気をつけること

- **`letter-spacing` は半角数字の間にも入ります**。`第 12 回` が `第 1 2 回` に見えるので、
  数字を含む見出しには使わないでください
- 禁則処理（行頭の句読点・閉じ括弧、行末の開き括弧）は自動で効きます。何もしなくて構いません
- 「2027 年度」のような**数字 + 助数詞が行末で割れるのを防ぐ手段はありません**。
  割りたくないときは幅か `font-size` を調整してください
- 両端揃えは `text-align: justify` で効きます

---

## 6. 失敗したときの直し方

### 6.1 エラーの読み方

CLI は失敗すると標準エラーに出し、終了コードは 0 以外になります（1 = レンダリング／入出力の失敗、
2 = 引数の誤り）。形式はこうです。

```
error[unsupported-value] at 82:77: `border-style: dashed` is not supported (supported: solid, none)
        ^^^^^^^^^^^^^^^     ^^ ^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        識別子（安定）       行:桁  何がだめか + 直し方
```

- **識別子**（`unsupported-value` など）と**位置**（`行:桁`）は契約です。機械はここを読んでください
- **文面**は人と AI が読むためのもので、版が変わると変わりえます
- 位置が無いエラー（`invalid-option` など）は `at` の部分がありません

主な識別子:

| 識別子 | 意味 | まずやること |
|---|---|---|
| `unsupported-tag` | 対応外のタグ | §5 の代替表を見て `div` / `span` / `p` に置き換える |
| `unsupported-attribute` | 対応外の属性 | 属性を消す（`style` `class` `id` だけ） |
| `unsupported-property` | 対応外のプロパティ | §5 の代替表。接頭辞なら外す |
| `unsupported-value` | プロパティは対応、値が対応外 | 文面の `supported: …` に挙がった値にする |
| `unsupported-layout` | 対応外のレイアウト | inline への箱プロパティ（§3-(2)(3)）／縦書きの向き（§4.10） |
| `css-parse` | CSS の構文・セレクタが対応外 | 子孫セレクタ・擬似要素・at-rule・`!important` を外す |
| `html-parse` / `invalid-utf8` | HTML が壊れている | 閉じタグと文字コードを直す |
| `image-not-found` | `<img src>` の名前が `--image` に無い | 名前をそろえる（§4.12） |
| `image-decode` | 画像が PNG として読めない | PNG に変換する |
| `invalid-option` | オプションの誤り | 縦書きで `--height` が無い／内容の高さが 0 など |
| `limit-exceeded` | 入力が上限を超えた | 文字数・画像数・寸法を減らす |

### 6.2 警告（描画は続く）

```
warning[missing-glyph]: no font has a glyph for U+1F600 at 1:6
```

`missing-glyph` は**豆腐（□）が出た**という意味です。絵文字か、渡したフォントに無い文字です。
絵文字は使わない、フォントを足す、のどちらかで直します。

```
warning[content-overflow]: content overflows the canvas by 430.0px (bottom) at 19:1
```

`content-overflow` は**固定した紙面から中身が出ていて、その分が切れている**という意味です。
`(bottom)` は出た辺で、直し方が変わります: `bottom` なら `--height` を増やすか中身を減らす、
`right` なら `--width` を増やすか箱の幅・余白を減らします。`--height` を省いていれば
（内容の高さに追従）縦には出られないので、この警告は横方向だけになります。

**警告が出ても PNG は作られ、終了コードは 0 です。** 自動配信するなら stderr も見てください。
**`--strict` を付ければ警告も失敗**になり、PNG は作られません（既にあるファイルも上書きされません）。

### 6.3 直す順序

1. **タグの誤り**（`unsupported-tag`）から直す。外枠タグ・`ul` / `li` / `table` は構造が変わるので最初に
2. 次に **CSS の構文**（`css-parse`）。セレクタ・at-rule・`!important`
3. 次に **プロパティと値**（`unsupported-property` / `unsupported-value`）。§5 の表で機械的に置換
4. 最後に **レイアウト**（`unsupported-layout`）。inline に箱プロパティを付けていた場所は、
   **`display: block` を足すのではなく宣言を削る**のが基本です
   （`display: block` にすると文の流れが切れて、1 文が複数行に割れます）。
   その箱が独立した部品なら、`div` に変えて flex 項目にします
5. **エラーが消えたら PNG を見る。** エラーが無いことは「絵が正しい」ことを意味しません
   （重なり・意図と違う位置・文字色と背景色の同化は検出されません）

### 6.4 現状と予定（ここは version 0.1.0 の話）

いまの shashoku（0.1.0）は次の状態です。

- **エラーは見つかった分が一度に全部出ます。** ① HTML と ② スタイルの段は、安全に読み進められる
  問題（対応外のタグ・属性・プロパティ・値・セレクタ）を集めてから失敗します。1 件直すたびに
  走らせ直す必要はありません。ただし**構造が壊れている場合**（閉じ忘れ、`&` の書き忘れ、不正な UTF-8）は
  その場で止まるので、まずそれを直してからもう一度走らせてください
- **「直し方」（hint）は独立した行**に出ます（`  hint: …`）。確かめた代替があるものにだけ付きます
- 警告は 2 種類です。`missing-glyph`（豆腐）と `content-overflow`（**固定した紙面からのはみ出し**）。
  `--height` を固定して中身が多いと、切れる量と辺つきで警告が出ます
- **`--strict`** を付けると、警告 1 件以上で失敗になり **PNG は作られません**（既にあるファイルも
  上書きしません）。サーバーで「検出した問題のある画像は配らない」判断に使えます
- **`--diagnostics json`** で、標準出力に診断を 1 オブジェクトで出せます（成功でも失敗でも）
- 成功すると CLI は `wrote out.png (1200x630)` を標準エラーに 1 行出します。
  `--height` を省いたときの実際の高さはここで分かります

診断 JSON の形:

```json
{"ok": true, "width": 1200, "height": 630, "truncated": false,
 "errors": [{"kind": "unsupported-property", "message": "…", "hint": "…",
             "line": 3, "column": 14, "offset": 120, "warning": null}],
 "warnings": [{"kind": "missing-glyph", "detail": "…", "codepoint": 128512,
               "line": 3, "column": 1, "offset": 88, "overflow_px": 0, "edge": null},
              {"kind": "content-overflow", "detail": "…", "codepoint": 0,
               "line": 19, "column": 1, "offset": 700, "overflow_px": 430, "edge": "bottom"}]}
```

- `line` / `column` / `offset` は位置が無ければ `null`、`hint` が無ければ `""`
- `warning` は `--strict` で格上げしたエラー（`"kind": "warning-as-error"`）だけ、
  元の警告の種類（`"missing-glyph"` など）が入ります。それ以外は `null`
- `edge` は `content-overflow` のとき `"top"` / `"right"` / `"bottom"` / `"left"`、それ以外は `null`
- `truncated` は診断が多すぎて記録を打ち切った（既定 100 件）ことを表します
- `--diagnostics json` のときは `-o` が必須で、`--dump-stage` とは併用できません。
  人向けの標準エラー出力は出ません

**まだできないこと**: 固定幅の箱から文字がはみ出す（箱のはみ出し）の検出、重なりの検出、
文字色と背景色の同化の検出。エラーも警告も無いことは「絵が正しい」ことを意味しません。

---

## 7. CLI

```
shashoku <input.html> -o <out.png> [--width N] [--height N] [--scale S]
         [--font <file>]... [--image <name>=<file.png>]...
```

| オプション | 意味 |
|---|---|
| `--width <N>` | ビューポートの幅（CSS px、既定 1200） |
| `--height <N>` | ビューポートの高さ。**省くと内容の高さに追従します**（縦書きでは必須） |
| `--scale <S>` | 出力倍率（既定 1.0。`2` で Retina 向けの 2 倍） |
| `--font <file>` | フォント（複数指定可。**指定順がフォールバック順**）。省くと埋め込みの既定フォント（Noto Sans JP Regular / Bold） |
| `--image <name>=<file>` | PNG 画像。`<img src="name">` で引く |
| `--compression <0-9>` | PNG の圧縮レベル（既定 6）。絵は変わりません |
| `--overflow` | あふれ処理 `oidashi`（既定）/ `oikomi` / `burasage` |
| `--line-break` | 行分割の厳しさ `strict`（既定）/ `normal` / `loose` |
| `--trim-line-start` | 行頭の始め括弧の前の空きを詰める（天付き。既定は詰めない） |
| `--no-trim-line-end` | 行末の終わり括弧・句読点の後ろの空きを詰めない（既定は詰める） |
| `--no-collapse-punctuation` | 連続する約物の間の空きを詰めない（既定は詰める） |
| `--dump-stage` | 中間表現を出す `dom` / `style` / `box` / `display-list` / `svg` |
| `--strict` | 警告（豆腐・紙面からのはみ出し）も失敗にする。PNG は作りません |
| `--diagnostics` | 診断の出し方 `human`（既定）/ `json`。`json` は `-o` が必須で `--dump-stage` と併用不可 |
| `--version` / `--license` | 版 / ライセンス（全オプションは `shashoku --help`） |

```bash
# 内容に合わせた高さで
shashoku card.html -o card.png --width 640
# OG 画像（高さ固定）
shashoku og-card.html -o og.png --width 1200 --height 630
# 画像つき
shashoku image.html --image icon=examples/icon.png -o out.png --width 560
# 縦書き（--height 必須）
shashoku vertical.html -o v.png --width 480 --height 900
# 箱の寸法を数字で確かめる
shashoku card.html --dump-stage box --width 640
# 診断を機械が読む形で（成功でも失敗でも 1 オブジェクト）
shashoku card.html -o card.png --width 640 --diagnostics json
# 警告（豆腐・はみ出し）も失敗にする（PNG は作られません）
shashoku og-card.html -o og.png --width 1200 --height 630 --strict
```

**太字**は `font-weight` を書けば出ます（既定フォントの Bold が選ばれます）。
`font-family` は family の優先順を変えるだけで、太さの照合は書かなくても働きます。

同じ入力（HTML・フォント・画像・オプション）からは、常に**バイト単位で同じ PNG** が出ます。

---

## 8. 迷ったら

1. `shashoku --version` の版が、この文書の冒頭の版と同じか確かめる
2. §2 の対応表に無いものは使わない
3. §5 の代替表で置き換える
4. それでもエラーが出たら、メッセージの `supported: …` に挙がった値だけを使う
5. エラーが消えたら **PNG を開いて目で見る**

エンジン側の正は [README.md](../../README.md) の「対応している HTML / CSS」です。
この文書と食い違っていたら README を正としてください。
