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
- 文字参照（`&amp;` `&lt;` `&gt;` `&nbsp;` `&#12354;`）は使えます。
  `&nbsp;`（U+00A0）は**そこで行を折らない空白**になります（数字と単位を結ぶのに使えます。§5）
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
| ボックス | `display` `box-sizing` `width` `height` `margin` `padding` `border` `border-width` `border-style` `border-color` `border-radius` `background` `background-color` |
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
- `margin: auto` は使えます。ブロックの左右中央寄せのほかに、**flex 項目では余りを吸って寄せます**
  （横並びの子に `margin-left: auto` で右端へ、縦並びの子に `margin-top: auto` で下端へ。§4.14）

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

> `font-family` が照合するのは、**渡したフォントが自分で名乗っている family 名**だけです。
> 並びのどれも満たせないと（渡していない名前と、`serif` / `monospace` などの解釈できない総称しか
> 書いていないと）**`warning[font-not-found]` が出て、渡したフォントの先頭で描かれます**（描画は続きます）。
> `sans-serif` / `system-ui` / `ui-sans-serif` は**「渡したフォントの先頭で描いてよい」**という意味なので
> 警告は出ません（`--font` で明朝だけを渡していれば明朝で描きます。**shashoku はフォントの書体を
> 判定しません**）（明朝・等幅にする方法は §7）。

**記号は「フォントに入っているものだけ」出ます。** 入っていない字は □（豆腐）になり
`warning[missing-glyph]` が出ます。埋め込みの既定フォント（Noto Sans JP）で実際に描いて
確かめた一覧です（`--font` で別のフォントを渡したときは、そのフォント次第で変わります）。

| 分類 | 出る記号 |
|---|---|
| 矢印 | `→` `←` `↑` `↓` `⇒` `⇐` `⇔` `↔` `⇨` `➡` |
| 図形 | `●` `○` `◎` `■` `□` `◆` `◇` `▲` `△` `▼` `▽` `▶` `▷` `◀` `◁` `▪` `▫` `★` `☆` |
| 記号・数式 | `✓` `×` `✚` `※` `〓` `¬` `∞` `≠` `≦` `≧` `√` `∴` `∵` `−` `±` `÷` `‰` `℃` `°` `′` `″` |
| ダッシュ・約物 | `–` `—` `―` `…` `〜` `～` `「」` `『』` `（）` `〔〕` `【】` `〈〉` `《》` `・` |
| 通貨・記載 | `€` `¥` `£` `$` `©` `®` `™` `§` `¶` `†` `‡` `№` `①` `②` `③` `Ⅰ` `Ⅱ` `Ⅲ` |
| そのほか | `♦` `♥` `♠` `♣` `♪` `☀` `☁` `☂` `☃` `☎` `✂` `⌘` `⏎` `⇧` `⚠` `❖` |

**□ になったもの**（同じ確かめ方で実際に警告が出たもの）:

- 絵文字はすべて。`🎉` `😀` `✅` `❗` `⭐` `❤` `⚡` `⏰` `✈` `✉` `⬛` `⬜`
- チェック・バツの多く。`✔` `✕` `✗` `✘` `✖` `☑` `☐` `☒`（**出るのは `✓` と `×` だけ**）
- `≒` `✦` `✳` `✴` `➔` `⌥`

`⚠️` のように異体字セレクタ（`U+FE0F`）を付けた書き方は、セレクタが無視されて
**白黒の `⚠`** になります。色の付いた絵文字は出せません。

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

## 3. 先に知っておく 5 つのこと

検証で AI が実際に引っかかった順です。**ここだけで反復の大半が消えます。**
(1) だけは落とし穴ではなく「知っていると書く量が減ること」です。

### (1) `box-sizing` は両方使える。既定は content-box

`content-box`（既定）と `border-box` の両方が使えます。**既定はブラウザと同じ content-box** なので、
何も書かなければ `width` / `height` は **padding と border を含みません**。外寸を決めたいときは引き算します。

```
1200×630 の OG 画像、padding 64px、枠なし
  → width: 1072px;  height: 502px;  padding: 64px;
     （1072 + 64×2 = 1200、502 + 64×2 = 630）
枠 2px を足すなら、さらに 2×2 = 4px を引く
  → width: 1068px;  height: 498px;  padding: 64px;  border: 2px solid #333;
```

**先頭に `* { box-sizing: border-box }` を 1 行書けば、この引き算は要りません。**
`width` / `height` が箱の外寸そのものになります。

```html
<style>
  * { box-sizing: border-box }
  .card { width: 600px; height: 160px; padding: 24px; border: 2px solid #333;
          background: #ffffff; font-size: 17px; line-height: 1.8; color: #1b2733; }
</style>
<div class="card">外寸（600×160）をそのまま書けます。padding と border は内側に入ります。</div>
```

- `border-box` は `width` / `height` のほか **`flex-basis` と `<img>`**（CSS の `width` / `height` と
  `width` / `height` 属性の両方）にも同じように効きます
- 引ききれないとき（`width: 10px; padding: 20px` など）は中身の幅が 0 で止まり、箱は指定値より
  大きくなります。ブラウザと同じです
- 縦書きでも同じです（字送り方向の寸法から、その方向の padding 2 辺と border を引きます）
- この文書の §4 の例は**すべて content-box のまま**書いてあります。`border-box` に切り替えるなら
  `width` / `height` に padding と border を足し戻してください

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

### (3) flex コンテナの**直接の子**は block 化される（孫は (2) のまま）

`display: flex` の直接の子は、`span` と書いても自動で block 級になります（CSS Display 3 §2.7）。
`padding` / `border` / `width` を付けてよく、**`div` と `span` でまったく同じ絵**（バイト単位で
同じ PNG）が出ます。

```html
<!-- どちらも同じ結果 -->
<div style="display: flex; gap: 8px">
  <span style="flex: none; padding: 5px 12px; background: #e7f0ff">政策</span>
  <div style="flex: none; padding: 5px 12px; background: #e7f0ff">AI</div>
</div>
```

例外は **`<img>` / `<ruby>` / `<br>`** の 3 つで、flex の直接の子でも inline のままです。
`<ruby>` に `padding` を書くと `error[unsupported-layout]` になるので、箱が要るなら
`div` か `span` で包んでください。`<img>` は従来どおりそのまま flex 項目にできます（§4.12）。

**block 化されるのは直接の子だけです。** 文章の中に置いた `span`（= flex コンテナの孫）は
inline のままなので、(2) の制限がそのまま効きます。

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
子は `div` でも `span` でも同じです（§3-(3)）。
flex の中に flex を入れて組み立てる形（段の間の矢印、カードの中のタグ列）は §4.13。

**分割できない長い語があると等分になりません。** `flex: 1 1 0` の子は、
**これ以上縮められない幅**（= 分割できない一番長い語の幅 + `padding` + `border`）より狭くなりません。
URL・長い英単語・連続する英数字・ハッシュ値が入ると、その子だけ広がって他が縮み、
合計が親を超えれば**親からはみ出します**（紙面の外まで出れば `warning[content-overflow]`）。
長い語が入りうる子には **`overflow-wrap: anywhere`** を付けてください。
**`overflow-wrap: break-word` は `flex: 1 1 0` の子では効きません**（縮められる幅の計算に
入らないため。CSS Text 3 §5.4）。実例は §4.3。

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

**空のセルは高さを持ちません。** 中身の無い `div` の内容高さは 0 なので、その行のセルが**全部空**だと、
行の高さは `padding` のぶんだけになります（上の `.td` なら 10 + 10 = 20px）。
1 行ぶんの高さを保ちたいときは `&nbsp;` を入れてください
（文字参照として通り、`font-size × line-height` の高さを持ちます。上の例なら 15 × 1.7 = 25.5px で
行は 45.5px になります）。同じ行に中身のあるセルが 1 つでもあれば、空のセルも `stretch` で
引き伸ばされるので `&nbsp;` は要りません。

**数字と単位が列の端で割れるときも `&nbsp;` で結びます。** 幅 70px・`font-size: 15px` の箱で
`常駐 17 ms（待機時）` は `常駐 17` / `ms（待機` / `時）` と割れますが、`17&nbsp;ms` と書くと
`常駐` / `17 ms` / `（待機時）` になり、数字と単位は同じ行に残ります（§5）。

**長い語が入る列には `overflow-wrap: anywhere` を付けてください。** セルは `flex: 1 1 0` なので、
分割できない語（URL・ハッシュ・連続する英数字）があるとその列だけ広がり、**見出し行と本文行で
列がずれます**（§4.2）。上の `.tbl` に 40 桁のコミットハッシュを入れて `--width 400` で描くと:

| | 見出し行の列幅 | 本文行の列幅 | 結果 |
|---|---|---|---|
| そのまま | 132.7 / 132.7 / 132.7 | 54.0 / 349.8 / 59.6 | 列がずれ、右に 64.4px はみ出して `warning[content-overflow]` |
| `overflow-wrap: anywhere` | 132.7 / 132.7 / 132.7 | 132.7 / 132.7 / 132.7 | そろう。ハッシュは途中で折り返す |
| `overflow-wrap: break-word` | — | そのままと同じ | **効きません** |

```html
<style>
  /* 上の .tbl / .tr / .td / .rule に足す */
  .long { overflow-wrap: anywhere; }
</style>
<div class="tr"><div class="td">コミット</div><div class="td long">9f2c1b4e8d7a6503f1e2c9b8a7d6e5f4c3b2a190</div><div class="td">main の先端</div></div>
```

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
子を `<span class="pill">` と書いても同じです（flex の直接の子は block 化されます。§3-(3)）。

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
親に明示した `height`（content-box なので padding を引いた値。`box-sizing: border-box` を書けば
引き算は不要で、外寸をそのまま書けます。§3-(1)）で作ります。
本文を縦中央に置きつつ、署名や日付だけを下端に張り付けたいときは §4.14。

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
  幅は自分で足し算します（content-box なので `padding` と `border` も足す。`box-sizing: border-box` を
  書いているなら `width` がすでに外寸なので足す必要はありません）。
  上の要素の寸法を 1px でも変えたらこの値も直してください
- **一方だけ右端に寄せる**なら `margin-left: auto` でも同じです（spacer の `div` が要りません）

spacer の幅の出し方:

1. 狙う要素より**前にある兄弟**の外寸を全部足す。1 つぶんの外寸は
   `width + padding左右 + border左右`（content-box なので `width` に含まれていません。
   `border-box` なら `width` がそのまま外寸です）
2. その間で**またぐ `gap` の数**だけ `gap` を足す（兄弟が n 個なら gap は n 個）
3. 注記の行の `padding-left` を、上の行の `padding-left` と**同じ**にする（違うとその差だけずれます）
4. `--dump-stage box` で確かめる。`rect` の 1 つ目の数が、狙った要素の `rect` の 1 つ目と一致すれば合っています

```html
<style>
  .row   { display: flex; gap: 12px; padding: 16px; background: #ffffff; }
  .cell  { flex: none; width: 160px; padding: 10px; border: 1px solid #ccd3dd;
           border-radius: 8px; font-size: 15px; color: #1b2733; }
  .notes { display: flex; padding: 0 16px 16px 16px; background: #ffffff; }
  .sp    { flex: none; width: 194px; }
  .note  { flex: none; font-size: 13px; color: #c0392b; }
</style>
<div class="row">
  <div class="cell">一次案</div>
  <div class="cell">二次案</div>
  <div class="cell">最終案</div>
</div>
<div class="notes"><div class="sp"></div><div class="note">▲ ここだけ差し替えた</div></div>
```

セル 1 つの外寸は `160 + 10×2 + 1×2 = 182`、gap を 1 つまたぐので spacer は `182 + 12 = 194px`。
`shashoku notes.html --dump-stage box --width 600` で見ると、2 つ目のセルも注記も
`rect` の先頭が `210`（= 16 + 194）でそろいます。

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
先頭に `* { box-sizing: border-box }` を足せばこの引き算は要らず、`width: 1200px; height: 630px` と
書けます。
タイトルに `flex: 1 1 0` を付けると、**タイトルが伸びてフッターが下端に張り付きます**。

### 4.10 縦書き — [vertical.html](examples/vertical.html)

```html
<style>
  .sheet  { writing-mode: vertical-rl; display: flex; flex-direction: column;
            width: 400px; height: 820px; padding: 40px;
            background: #f7f3e8; color: #23201a; }
  .poem   { margin: 0; font-size: 25px; line-height: 2; }
  .author { margin: auto 28px 0 0; font-size: 16px; color: #6b6355; }
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

#### 行送り方向（左右）に置き分ける

「右端に題、左端に署名」のような配置は、いちばん外側の要素を `display: flex` にして作ります。
縦書きでは軸が 90 度回るので、どのプロパティがどちらに効くかを先に押さえてください
（`--dump-stage box` で確かめた表です）。

| `flex-direction` | 主軸（`justify-content` が効く向き） | 交差軸（`align-items` が効く向き） |
|---|---|---|
| `row`（既定） | **縦**（上 → 下）。`flex-start` = 上、`flex-end` = 下 | **横**（右 → 左）。`flex-start` = **右**、`flex-end` = **左** |
| `column` | **横**（右 → 左）。`flex-start` = **右**、`flex-end` = **左** | **縦**（上 → 下）。`flex-start` = 上、`flex-end` = 下 |

`margin` だけは上の表と無関係に**物理方向のまま**です。行送り方向に空けたいときは
`margin-right`（右隣との間）と `margin-left`（左隣との間）を使います。

```html
<style>
  .sheet { writing-mode: vertical-rl; display: flex; flex-direction: column;
           justify-content: space-between; width: 400px; height: 520px; padding: 32px;
           background: #f7f3e8; color: #23201a; }
  .title { font-size: 30px; font-weight: bold; }
  .body  { font-size: 20px; line-height: 2; }
  .by    { font-size: 15px; color: #6b6355; }
</style>
<div class="sheet">
  <div class="title">秋の便り</div>
  <div class="body">風が冷たくなりました。庭の柿が色づき、夕暮れの早さに驚いています。</div>
  <div class="by">架空　花</div>
</div>
```

`shashoku letter.html -o letter.png --width 464 --height 584` で、題が右端・署名が左端に付きます
（`column` の主軸が右 → 左なので、`space-between` が寄せるのは左右です）。

**字送り方向（上下）の位置決め**は、同じ flex の `align-items`（全部の子に効く）か、
子ごとの `margin-top: auto` / `margin-bottom: auto` です。上の例の署名がこれで、
`margin: auto 28px 0 0` の `auto`（= `margin-top`）が余りを全部吸って**署名を下端に貼り付けます**。
`padding-top: 560px` のように数えて押すこともできますが、その場合は本文の長さや `font-size` を
変えるたびに数え直しになります。

> `--dump-stage box` の `rect` は**論理座標**です。縦書きでは
> `[字送り（上）からの位置, 行送り（右端）からの位置, 字送り方向の大きさ, 行送り方向の大きさ]`
> の順で、**2 つ目の数が大きいほど左**にあります。

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
- **ルビのある行は行ボックスが自動で広がる**ので、上の行と重なることはありません
  （`line-height: 1.0` でも重なりません）。ただし窮屈に見えるので **`line-height: 1.8` 前後**を目安に
- **行の高さの式**（横書き）: ルビのある行の高さは
  `font-size × max(line-height, A + line-height ÷ 2)` です。`A` はフォントの
  **ascent + descent を em で表した値**で、既定フォント（Noto Sans JP）では **約 1.45**。
  つまり `line-height` が `2 × A ≒ 2.9` 未満だとルビのぶんだけ行が高くなり、**増えるのは上側だけ**です。
  実測（既定フォント、`font-size: 20px`）: `line-height: 1.8` の行は 36px、同じ行にルビがあると
  **46.95px**。`line-height: 2.9` なら 58px で、ルビがあっても変わりません
- そのため、**ルビのある行とない行が混ざると行送りが不ぞろいに見えます**。そろえたいなら
  段落ごと `line-height` を 2.9 以上にする（かなり空きます）か、不ぞろいを受け入れてください。
  注記（`<rt>`）そのものは行の高さに参加せず、参加するのは「親文字の外に出るための張り出し」だけです
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
- **`<img>` は flex の直接の子に置けます。** 上の例の `.head` がそのまま flex コンテナで、
  `<img>` を `div` で包む必要はありません。大きさは `width` / `height` で決めます

### 4.13 flex の入れ子 — [flow.html](examples/flow.html)

`position` も `grid` も無いので、少し込み入った紙面は **flex の中に flex** を入れて作ります。
入れ子の各段で決めることは 2 つだけです: **並べる向き**（`flex-direction`）と、
**交差方向の揃え**（`align-items`）。

```html
<style>
  .flow  { display: flex; align-items: center; gap: 12px; padding: 20px; background: #ffffff; }
  .step  { flex: 1 1 0; display: flex; flex-direction: column; gap: 8px;
           padding: 14px; border-radius: 10px; background: #f1f4f9; }
  .no    { font-size: 12px; color: #6b7a90; }
  .name  { font-size: 17px; font-weight: bold; line-height: 1.5; color: #1b2733; }
  .tags  { display: flex; gap: 6px; }
  .tag   { flex: none; padding: 2px 8px; border-radius: 9px;
           background: #e7f0ff; color: #14509b; font-size: 12px; }
  .arrow { flex: none; font-size: 22px; color: #9aa7b8; }
</style>
<div class="flow">
  <div class="step">
    <div class="no">1</div>
    <div class="name">受け取る</div>
    <div class="tags"><div class="tag">HTML</div><div class="tag">フォント</div></div>
  </div>
  <div class="arrow">→</div>
  <div class="step">
    <div class="no">2</div>
    <div class="name">組む</div>
    <div class="tags"><div class="tag">行分割</div><div class="tag">約物</div></div>
  </div>
</div>
```

- **段の間の矢印**は「`→` を 1 文字入れた `flex: none` の `div`」です。外側の
  `align-items: center` で段の縦中央に来るので、矢印の位置を計算する必要はありません
  （§2.4 のとおり `→` は既定フォントで出ます）
- **段（`.step`）は `flex: 1 1 0`** で等分に。矢印は `flex: none` なので幅を食いません
- **段の中は `flex-direction: column`** にして、番号・見出し・タグ列を縦に積みます。
  `gap` が段の中の行間になります
- **タグ列はさらに内側の flex**（`display: flex` + 子に `flex: none`）。§4.5 と同じ形です
- 入れ子にしても `flex-wrap` はありません。**入りきらなければはみ出します**（親の箱からの
  はみ出しは検出されません）。段の数を増やすときは `--width` も増やしてください

### 4.14 引用カード（本文を縦中央、署名を下端）— [quote.html](examples/quote.html)

高さを固定した紙面で「本文は真ん中、署名は下端」にする形です。`position` が無いので、
**余りを吸う箱**を 1 つ作って解きます。

```html
<style>
  .sheet { display: flex; flex-direction: column; width: 552px; height: 312px;
           padding: 24px; background: #fbf8f2; color: #23201a; }
  .body  { flex: 1 1 0; display: flex; flex-direction: column; justify-content: center; }
  .quote { margin: 0; font-size: 26px; line-height: 1.9; }
  .by    { flex: none; text-align: right; font-size: 15px; color: #7a7266; }
</style>
<div class="sheet">
  <div class="body"><p class="quote">おそれるな。おそれは、まだ起きていないことの影にすぎない。</p></div>
  <div class="by">架空　花『影の書』</div>
</div>
```

`shashoku quote.html -o quote.png --width 600 --height 360` で出します
（`552 = 600 − 24×2`、`312 = 360 − 24×2`。`* { box-sizing: border-box }` を先頭に書けば
引き算は不要で、`width: 600px; height: 360px` と書けます。§3-(1)）。

- 紙面を `flex-direction: column` にして、**本文の箱に `flex: 1 1 0`** を与えると、署名以外の
  余りを全部その箱が取ります。中で `justify-content: center` すれば本文が縦中央、署名は下端です
- 本文が中央に来るのは「**署名を除いた領域**」の中央で、紙面の中央より署名の高さの半分だけ上です。
  厳密に紙面の中央に置きたいなら、**署名と同じ高さの空の `div`**（`flex: none; height: <署名の高さ>px`）を
  本文の箱の**上**に足してください。署名の高さは `height` と `line-height` を同じ px にして決め打ちにすると
  合わせやすく、上の例なら両方 22px にすると本文の中心が紙面のちょうど中央（180px）に来ます
- **本文は上端のままでよく、署名だけ下端に張り付けたい**なら、spacer も `flex: 1 1 0` も要りません。
  署名に `margin-top: auto` を書くだけです（余りをその margin が全部吸います）
- 縦書きでも `margin-top: auto` は「字送りの終わり = **下端**」に押します（§4.10 で確かめた形）。
  行送り方向（左右）の端に寄せたいときは、§4.10 の `justify-content` の表を見てください

---

## 5. やってはいけないこと / 代替

| 書きたくなるもの | shashoku では | 代わりに |
|---|---|---|
| `<html>` `<head>` `<body>` `<!DOCTYPE>` | エラー | **書かない**。断片で出す |
| `<ul>` `<li>` `<ol>` | エラー | flex の行 + 丸の `div`（§4.4） |
| `<table>` `<tr>` `<td>` | エラー | flex の行 + `flex: 1 1 0` のセル（§4.3） |
| `<strong>` `<b>` `<em>` `<code>` `<a>` `<section>` `<header>` | エラー | `span`（+ `font-weight` / `color` / `background-color`）または `div` |
| 絵文字（🎉 😀 など） | **□ になって警告** | **使わない**。§2.4 の一覧にある記号（`→` `▼` `※` `✓`）か、色付きの小さな `div` で代用 |
| `display: inline-block` | エラー | **flex コンテナの中なら宣言を削るだけ**（直接の子は block 化され、そのまま箱のプロパティを取ります。§3-(3)）。それ以外は flex の親 + `flex: none` の子（§3-(4)） |
| `position` / `top` / `left` / `z-index` | エラー | flex と `justify-content` / `align-items` / 空の spacer（§4.7） |
| `grid` / `grid-template-columns` ほか `grid-*` | エラー | **等幅の 1 行**なら親に `display: flex`・子に `flex: 1 1 0`（`1fr` 相当。§4.3）。**複数行**なら 1 行 1 flex コンテナ（§4.2）。**不等幅・セルのまたぎ（`grid-column: span 2`）は代替がありません** |
| `float` | エラー | flex |
| `flex-wrap` | エラー | 行ごとに flex コンテナを分ける |
| `align-self` / `order` / `flex-flow` | エラー | 並び順を HTML の順で書く |
| `min-height` | エラー | 親が **既定の `align-items: stretch` の flex** なら**削るだけ**（その子はもう交差方向いっぱいです）。高さが分かっているなら `height`。どちらでもなければ削って内容に高さを決めさせる |
| `min-width` / `max-width` / `max-height` | エラー | 固定値の `width` / `height` にするか、削る |
| `overflow` | エラー | はみ出さない寸法にする。角丸のクリップは諦める |
| `background` のグラデーション（`linear-gradient` ほか） | エラー | **単色**の `background-color` にする（絵は平坦になります）。`background-clip: text` + `color: transparent` と組で使っているときは**両方**外す（下の「削ると危険な組み合わせ」） |
| `background-image`（`url(...)` ほか） | エラー | 単色の `background-color` か、`--image` で渡した `<img>`（§4.12） |
| `box-shadow` / `text-shadow` | エラー | 影は諦める。境界は 1px の枠線か薄い背景色で表す |
| `opacity` | エラー | 色そのものを薄くする（`#00000099` や淡い色） |
| `transform`（`rotate` など） | エラー | 傾けない |
| `::before` / `::after` + `content` | エラー | **実要素**（`span` / `div`）として書く。ただし `position` が無いので**流れの中に落ちる**ことに注意 |
| `border-top` / `border-left` など辺ごとの枠 | エラー | **箱と箱の区切り線**なら `height: 1px`（横並びなら `width: 1px`）+ 背景色の `div` を挟む（§4.3）。ただし**流れの中で 1px ぶん場所を取ります**。**枠の一辺だけ**を出す代替はありません: 4 辺の `border` にするか、諦めて削る |
| `border-style: dashed` / `dotted` | エラー | `solid`。破線と実線の描き分けは**色**で代える |
| 隅ごとの `border-radius`（4 値） | エラー | 1 値の `border-radius` |
| `border-collapse` | 無い | セルに枠を付けず、1px の `div` で罫を引く |
| `-webkit-*` / `-moz-*` / `-ms-*` | エラー | **接頭辞を外す**（外した名前が対応表にあれば通ります） |
| Web フォント（`@font-face` / Google Fonts の `<link>`） | エラー | フォントは CLI の `--font` で渡す。HTML 側には書かない |
| `@media` / `@import` / CSS 変数 / `calc()` / `!important` | エラー | 値を直接書く |
| 子孫セレクタ（`.card p`） | エラー | 当てたい要素にクラスを直接書く |
| JPEG / SVG / WebP の画像 | エラー | PNG に変換して `--image` で渡す |

### 絵を代えたら、文章も直す

上の表のとおりに置き換えると、**絵は変わったのに文章がそのまま**になりがちです。
エラーも警告も出ないので、気づくのは PNG を見たときです。

- 破線の枠（`dashed`）を実線や薄い色に代えた → 「**破線で囲んだ部分**は…」という凡例が嘘になります
- 矢印の画像や `::before` の飾りを `→` の文字に代えた → 「**下向きの矢印**が…」が合わなくなります
- 影（`box-shadow`）を枠線に代えた → 「**浮いて見えるカード**が…」が合わなくなります
- グラデーションを単色に代えた → 「**青から紫へのグラデーション**」が合わなくなります

置き換えは**スタイルと文章の 2 か所で 1 組**だと思ってください。凡例・キャプション・本文のうち、
見た目を指している言葉を探して同時に直します。

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
- **数字と助数詞・単位の間は `&nbsp;`（U+00A0）で結べます。** `2027&nbsp;年`、`17&nbsp;ms` と書くと
  行分割器はそこを「割らない」と扱うので、数字と助数詞・単位が行末で離れません
  （幅も見た目もふつうの半角スペースと同じで、豆腐にも警告にもなりません）。
  幅 110px の箱で `法改正は 2027 年度を視野に入れる。` は `法改正は 2027` / `年度を…` と割れますが、
  `2027&nbsp;年度` と書くと `法改正は` / `2027 年度を…` になります
- ただし **`&nbsp;` が守るのはその空白 1 か所だけ**です。和文は語の途中でも折れるので、
  同じ文を幅 130px で描くと `法改正は 2027 年` / `度を…` と**助数詞の中**で折れます。
  **語全体を 1 かたまりにする手段（`white-space: nowrap`）は今はありません**
  （`white-space` は `unsupported-property` のエラーになります）。幅を増やすか、語を短くしてください
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

直し方が分かるものには、**`  hint: …` の行が続きます**（下は実際の出力そのままです）。

```
error[unsupported-property] at 3:8: `min-height` is not a supported property
  hint: no min/max sizes. If the parent is a flex container with the default `align-items: stretch`, drop it (the item already fills the cross size); if the height is known, use `height`; otherwise drop it and let the content decide the height
error[unsupported-value] at 7:17: `display: grid` is not supported (supported: block, flex, inline, none)
  hint: no grid. For equal-width columns in one row, use `display: flex` on the parent and `flex: 1 1 0` on each child (guide §4.3); for several rows, one flex row per line (guide §4.2). Unequal or spanning grids have no equivalent
error[unsupported-layout] at 12:16: `padding-top` is not supported on an inline element (`display: inline`); only `<img>` takes box properties while inline
  hint: drop the declaration; `display: block` would accept it but breaks the surrounding text flow. If the box is a standalone part (tag / pill / badge), make it a flex item: a `div` inside a `display: flex` parent (guide §3-(4))
```

- **hint には成立条件が書かれていることがあります。**「親が …なら削る／高さが分かるなら `height`」
  「不等幅・またぎは代替なし」のように場合分けしてあるので、自分の HTML がどれに当たるかを見てから選んでください
- 3 つ目の `unsupported-layout` は**文章の中に置いた `span`**（flex コンテナの孫）の例です。
  flex コンテナの**直接の子**なら block 化されるので、同じ宣言でもエラーになりません（§3-(3)）
- hint が無いのは「確かめた代替が無い」という意味です。§5 の表を見てください
- 同じ規則に複数の要素が当たっても、**同一位置・同一文面の診断は 1 件**にまとめられます
- 機械で読むなら `--diagnostics json`。`hint` は同じ文字列がそのまま入ります（§6.4）

主な識別子:

| 識別子 | 意味 | まずやること |
|---|---|---|
| `unsupported-tag` | 対応外のタグ | §5 の代替表を見て `div` / `span` / `p` に置き換える |
| `unsupported-attribute` | 対応外の属性 | 属性を消す（`style` `class` `id` だけ） |
| `unsupported-property` | 対応外のプロパティ | §5 の代替表。接頭辞なら外す |
| `unsupported-value` | プロパティは対応、値が対応外 | 文面の `supported: …` に挙がった値にする |
| `unsupported-layout` | 対応外のレイアウト | 文中の inline への箱プロパティ（§3-(2)）／縦書きの向き（§4.10） |
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

```
warning[font-not-found]: no requested font family is loaded (`Hiragino Mincho ProN`, `serif`); text uses `Noto Sans JP` instead at 1:57
```

`font-not-found` は**`font-family` に書いた名前をどれも満たせず、別のフォントで描いた**という
意味です（明朝を指定したのにゴシックで出る、が典型）。`--font` でそのフォントを渡すか、
`font-family` を外すか、`sans-serif` にします。`sans-serif` / `system-ui` / `ui-sans-serif` は
shashoku では**「渡したフォントの先頭で描いてよい」**という意味なので、警告は出ません
（明朝だけを渡していれば明朝で描きます。**エンジンは書体を判定しません**）。`serif` / `monospace`
などの総称は「先頭で描いてよい」とは読めない具体的な要求で、shashoku には満たせたか判定できないので、
それだけでは満たせていません（§7）。

**警告が出ても PNG は作られ、終了コードは 0 です。** 自動配信するなら stderr も見てください。
**`--strict` を付ければ警告も失敗**になり、PNG は作られません（既にあるファイルも上書きされません）。

### 6.3 直す順序

1. **タグの誤り**（`unsupported-tag`）から直す。外枠タグ・`ul` / `li` / `table` は構造が変わるので最初に
2. 次に **CSS の構文**（`css-parse`）。セレクタ・at-rule・`!important`
3. 次に **プロパティと値**（`unsupported-property` / `unsupported-value`）。§5 の表で機械的に置換
4. 最後に **レイアウト**（`unsupported-layout`）。inline に箱プロパティを付けていた場所は、
   **`display: block` を足すのではなく宣言を削る**のが基本です
   （`display: block` にすると文の流れが切れて、1 文が複数行に割れます）。
   その箱が独立した部品（タグ・pill・バッジ）なら、`display: flex` の親の**直接の子**にします
   （`div` でも `span` でもよい。§3-(3)）
5. **エラーが消えたら PNG を見る。** エラーが無いことは「絵が正しい」ことを意味しません
   （重なり・意図と違う位置・文字色と背景色の同化は検出されません）

### 6.4 現状と予定（ここは version 0.1.0 の話）

いまの shashoku（0.1.0）は次の状態です。

- **エラーは見つかった分が一度に全部出ます。** ① HTML と ② スタイルの段は、安全に読み進められる
  問題（対応外のタグ・属性・プロパティ・値・セレクタ）を集めてから失敗します。1 件直すたびに
  走らせ直す必要はありません。ただし**構造が壊れている場合**（閉じ忘れ、`&` の書き忘れ、不正な UTF-8）は
  その場で止まるので、まずそれを直してからもう一度走らせてください
- **「直し方」（hint）は独立した行**に出ます（`  hint: …`）。確かめた代替があるものにだけ付き、
  **成立条件があるときは条件つきで**書かれます（「親が stretch の flex なら削る」「不等幅・またぎは
  代替なし」など）。代替が無いものには hint が付きません
- **同じ規則に複数の要素が当たっても、同一位置・同一文面の診断は 1 件**です
- 警告は 3 種類です。`missing-glyph`（豆腐）、`content-overflow`（**固定した紙面からのはみ出し**）、
  `font-not-found`（**`font-family` の要求をどれも満たせなかった**）。
  `--height` を固定して中身が多いと、切れる量と辺つきで警告が出ます
- **`--strict`** を付けると、警告 1 件以上で失敗になり **PNG は作られません**（既にあるファイルも
  上書きしません）。サーバーで「検出した問題のある画像は配らない」判断に使えます
- **`--diagnostics json`** で、標準出力に診断を 1 オブジェクトで出せます（成功でも失敗でも）
- 成功すると CLI は `wrote out.png (1200x630)` を標準エラーに 1 行出します。
  `--height` を省いたときの実際の高さはここで分かります

診断 JSON の形（下は実際の出力を折り返しただけのものです）:

```json
{"ok": false, "width": null, "height": null, "truncated": false,
 "errors": [{"kind": "unsupported-property",
             "message": "`max-width` is not a supported property",
             "hint": "no min/max sizes: use a fixed `width` / `height`, or drop it",
             "line": 2, "column": 11, "offset": 18, "warning": null}],
 "warnings": []}
```

```json
{"ok": true, "width": 400, "height": 120, "truncated": false, "errors": [],
 "warnings": [{"kind": "content-overflow",
               "detail": "content overflows the canvas by 128.0px (bottom) at 4:1",
               "codepoint": 0, "line": 4, "column": 1, "offset": 100,
               "overflow_px": 128, "edge": "bottom"},
              {"kind": "missing-glyph",
               "detail": "no font has a glyph for U+1F389 at 4:19",
               "codepoint": 127881, "line": 4, "column": 19, "offset": 118,
               "overflow_px": 0, "edge": null}]}
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

### 明朝・等幅にする（`--font` と `font-family`）

**埋め込みの既定フォントは Noto Sans JP の Regular と Bold だけです。**
`font-family: serif` や `font-family: monospace` と書いても、**字面は 1px も変わりません**
（解釈できない総称と、渡していないフォントの名前は読み飛ばされます）。ただし黙ってはいません:
並びのどれも満たせないと **`warning[font-not-found]` が出ます**。明朝や等幅にするには、そのフォント
ファイルを `--font` で渡してください。

`sans-serif` / `system-ui` / `ui-sans-serif` だけは警告が出ません。この 3 つは shashoku では
**「渡したフォントの先頭で描いてよい」**という意味だからです。**明朝だけを `--font` で渡して
`font-family: sans-serif` と書いた場合も、明朝で描かれて警告は出ません**（shashoku はフォントの
書体を判定しません）。書体を確実に指定したいなら、総称ではなく**そのフォントが名乗っている
family 名**を書いてください。

- `font-family` が照合するのは、**フォントファイルが自分で名乗っている family 名**です
  （大文字小文字と前後の空白は無視されます）。`NotoSansJP-Regular.otf` なら `Noto Sans JP`、
  `NotoSans-Regular.ttf` なら `Noto Sans`。ファイル名でも CSS の総称名でもありません
- 照合した family が先頭に来るだけで、**残りは `--font` の順で後ろに続きます**。
  先頭の family に無い字は次のフォントへ落ちます
- **`--font` を 1 つでも書くと、既定フォントは使われません。** 和文が要るなら和文のフォントも
  自分で渡してください（欧文フォントだけを渡すと、和文が全部 □ になります）
- family 名が分からなければ `font-family` を書かず、**`--font` の順だけで決める**のが確実です

```html
<p style="font-family: 'Noto Sans'">Hamburgefonstiv 0123 / 写植</p>
```

```bash
shashoku doc.html -o doc.png --width 460 \
  --font NotoSansJP-Regular.otf --font NotoSans-Regular.ttf
```

この 1 行では欧文が Noto Sans で、`写植` は Noto Sans に無いので次の Noto Sans JP に落ちます。
明朝や等幅も同じで、`--font` にそのファイルを足し、`font-family` にそのファイルの family 名
（`Noto Serif JP`、`Noto Sans Mono` など）を書きます。

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
