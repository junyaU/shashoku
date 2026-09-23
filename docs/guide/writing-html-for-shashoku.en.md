# Writing HTML that shashoku turns into a PNG

**Engine version: shashoku 0.1.0** (use the guide whose version matches `shashoku --version`;
the supported set differs between versions.)

shashoku renders HTML to PNG. It is not a browser.
**Any tag, attribute, property or value that is not in the tables below is an error, not a
silent no-op** (fail loudly). So do not write "HTML that works in a browser" — write HTML that
stays inside this document. Stay inside it and the first run produces the PNG.

This is the English edition of
[writing-html-for-shashoku.md](writing-html-for-shashoku.md); the two say the same things.

---

## 0. How to use this document

Three intended uses:

1. **Paste it into a prompt** — hand the whole document to an AI and say "write HTML within this set"
2. **Build a template once** — take a skeleton from §4, then only swap the strings at run time
3. **Generate on every request on a server** — give this document to an LLM as the system prompt,
   pipe the HTML into the CLI, and on failure feed the diagnostics (§6) straight back to the LLM

**Do not embellish.** A property that is not listed here is not supported.
When in doubt, look it up in the substitution table in §5.

---

## 1. Shape of the input

- **Write a fragment.** `<!DOCTYPE>`, `<html>`, `<head>` and `<body>` are **errors**
- The encoding is **UTF-8 only**
- Styles go in `<style>` elements and/or `style` attributes
  (cascade: UA defaults < `<style>` < `style` attribute). Any number of `<style>` elements is fine
- HTML comments `<!-- … -->` are allowed
- Character references (`&amp;` `&lt;` `&gt;` `&nbsp;` `&#12354;`) are allowed
- **Anything you do not paint is transparent** (RGBA 0,0,0,0). If you want white paper,
  put a `background` on the outermost element

The smallest useful input:

```html
<style>.card { padding: 24px; background: #ffffff; font-size: 16px; }</style>
<div class="card">Hello.</div>
```

---

## 2. What is supported

Anything not listed here **is an error**.

### 2.1 Tags and attributes

| | |
|---|---|
| Tags | `div` `span` `p` `h1`–`h6` `img` `br` `ruby` `rt` `rp` `style` |
| Attributes (all tags) | `style` `class` `id` |
| Attributes (`img` only, in addition) | `src` `width` `height` `alt` |

- The `src` of `<img src="name">` is the **name** you passed as `--image name=file.png` on the
  command line. It is neither a URL nor a file path. Images must be **PNG**
- `<ruby>` pairs each run of base text with the `<rt>` that follows it
  (`<ruby>東<rt>とう</rt>京<rt>きょう</rt></ruby>` gives mono-ruby). `<rp>` is parsed but not drawn.
  `<ruby>`, `<img>` and `<br>` cannot appear inside `<ruby>`
- A block-level box (`display: block` / `flex`) cannot appear inside an inline box such as `<span>`

### 2.2 Selectors

Only a **single compound selector**, optionally repeated in a comma-separated list.

| Allowed | Example |
|---|---|
| Tag | `p` |
| Class | `.note` |
| ID | `#main` |
| Universal | `*` |
| Combinations of the above (no space between them) | `p.note` `.a.b` `div.card#main` |
| Comma-separated list | `h1, h2, .lead` |

**Not allowed**: descendant selectors (`.card p`), `>` `+` `~`, attribute selectors (`[href]`),
pseudo-classes and pseudo-elements (`:hover` `::before`), at-rules (`@media`, `@font-face`),
`!important`, CSS custom properties (`--x`), `calc()`.

> There are no descendant selectors, so **put a class directly on every element you want to style**.

### 2.3 Properties

| Group | Properties |
|---|---|
| Box | `display` `width` `height` `margin` `padding` `border` `border-width` `border-style` `border-color` `border-radius` `background` `background-color` |
| Per-side margin / padding | `margin-top` `margin-right` `margin-bottom` `margin-left`, `padding-top` `padding-right` `padding-bottom` `padding-left` |
| Flexbox | `flex-direction` `justify-content` `align-items` `gap` `row-gap` `column-gap` `flex` `flex-grow` `flex-shrink` `flex-basis` |
| Text | `color` `font-size` `font-family` `font-weight` `line-height` `letter-spacing` `text-align` `line-break` `overflow-wrap` (`word-wrap` is an alias) |
| Vertical writing | `writing-mode` |

- **Shorthands work**: `margin: 10px`, `margin: 10px 20px`, `margin: 10px 20px 30px 40px`
  (same for `padding`), `border: 1px solid #ccc`, `flex: 1 1 0`, `flex: none`, `gap: 8px 16px`,
  `background: #eef` (colour only)
- **There are no per-side borders** (`border-top`, `border-left`, … are unsupported).
  Draw rules with a `div` that has `height: 1px; background: <colour>` (§4.3)
- **There are no per-corner radii** — `border-radius` takes exactly one length
- `margin: auto` works (horizontal centring)

### 2.4 Values, units and colours

| Item | Accepted |
|---|---|
| Lengths | `<number>px` / `<number>em` / bare `0` |
| `%` | **`width` and `flex-basis` only** (and not for `width` in vertical writing mode) |
| `display` | `block` `flex` `inline` `none` (**no `inline-block`**) |
| `flex-direction` | `row` `column` |
| `justify-content` | `flex-start` `flex-end` `center` `space-between` `space-around` `space-evenly` |
| `align-items` | `stretch` (default) `flex-start` `flex-end` `center` (**no `baseline`**) |
| `text-align` | `start` `end` `left` `right` `center` `justify` |
| `font-weight` | `normal` (400), `bold` (700), or `100`–`900` in steps of 100 (no `bolder` / `lighter`) |
| `line-height` | **a unitless number (`1.8`), `<number>px`, `<number>em`, or `normal`** |
| `border-style` | **`solid` and `none` only** (no `dashed`, no `dotted`) |
| `writing-mode` | `horizontal-tb` `vertical-rl` |
| `line-break` | `auto` `strict` `normal` `loose` |
| `overflow-wrap` | `normal` `anywhere` `break-word` |
| Colours | `#rgb` `#rgba` `#rrggbb` `#rrggbbaa`, `rgb()` `rgba()`, CSS colour names, `transparent`, `currentColor` (on `border-color` only) |

> `line-height` **accepts unitless numbers**. A unitless number inherits as a ratio and each child
> resolves it against its own `font-size`; an `em` value inherits as a resolved px length. Prefer
> **unitless** whenever the element has children with a different `font-size`.
> `font-size` does not accept keywords such as `large`.

### 2.5 The user-agent stylesheet

The following is in effect unless you override it (write `margin: 0` yourself if you do not want it):

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

Margins collapse **only between adjacent sibling blocks** (never between a parent and its child).

---

## 3. Five traps to know before you start

In the order real AI-written HTML tripped over them during testing.
**Getting these right removes most of the retry loop.**

### (1) `box-sizing` is content-box, always

`width` / `height` **exclude padding and border**. Subtract them yourself to hit an outer size.

```
1200×630 OG image, padding 64px, no border
  → width: 1072px;  height: 502px;  padding: 64px;
     (1072 + 64×2 = 1200, 502 + 64×2 = 630)
Add a 2px border and subtract another 2×2 = 4px
  → width: 1068px;  height: 498px;  padding: 64px;  border: 2px solid #333;
```

### (2) Inline elements do not take box properties

A `span` (and any element you left at `display: inline`) **cannot** take
`width`, `height`, `margin`, `padding`, `border*` or `border-radius`.
Doing so gives `error[unsupported-layout]`.

```html
<!-- wrong -->
<span style="padding: 4px 8px; background: #eef">tag</span>
<!-- right: inline only takes colour and text properties -->
<span style="background: #eef; color: #14509b">highlight</span>
```

Inline elements do take `color`, `background-color`, `font-size`, `font-family`, `font-weight`,
`letter-spacing`. A `span` with a different `font-size` **aligns on the baseline**.

### (3) A `span` that is a flex item is not blockified

Browsers blockify the children of a flex container. **shashoku does not.** They stay
`display: inline`, so trap (2) still applies to them.

> **Always make the children of a flex container `div`s.**

### (4) No `display: inline-block` — use "flex parent + `flex: none` child"

That is how you build a shrink-to-fit box (tag, pill, badge, stamp).

```html
<div style="display: flex; gap: 8px">
  <div style="flex: none; padding: 5px 12px; border-radius: 13px; background: #e7f0ff">Policy</div>
  <div style="flex: none; padding: 5px 12px; border-radius: 13px; background: #e7f0ff">AI</div>
</div>
```

Without `flex: none` (i.e. `flex: 0 0 auto`), the default `flex-shrink: 1` squeezes the box when
space runs short and the text inside wraps. Conversely, `flex: none` never shrinks, so items that
do not fit overflow the parent (**overflow is not detected in v0.1.0**; see §6.4).

### (5) `border-radius` does not clip content (`<img>` is the one exception)

If you paint a background on a child inside a rounded box, the child **pokes out square at the
corners** (there is no `overflow`). Paint the background on the rounded box only.
`<img>` is the exception: `border-radius` clips the image itself, so round avatars work.

---

## 4. Idioms

Every snippet below is in [`examples/`](examples/) as a runnable file, and each one has been
checked to produce a PNG with `exit 0` and zero warnings.
The sample text is Japanese because the examples are shared with the Japanese edition of this
guide; replace the text with your own — the markup is what matters.

### 4.1 Card (padding, radius, border) — [card.html](examples/card.html)

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

### 4.2 Side by side (flex) — [row.html](examples/row.html)

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

`flex: 1 1 0` gives equal columns (the equivalent of grid's `1fr`). `flex: 1` is the same thing
(it expands to `flex: 1 1 0`). Use `flex: none` for shrink-to-fit and
`flex: none; width: 180px` for a fixed column.

### 4.3 Table (flex rows + 1px rules) — [table.html](examples/table.html)

`<table>` is unsupported. Make **each row a `display: flex` `div` and each cell a `flex: 1 1 0`
`div`**. There are no per-side borders, so draw horizontal rules with a **1px-tall `div`**
(putting a border on every cell would double up adjacent rules).

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

`align-items` defaults to `stretch`, so **cells in a row end up the same height** automatically
(zebra stripes stay intact). For uneven columns use `flex: none; width: 180px` or `flex: 2 1 0`.

### 4.4 Bulleted list (flex + a dot) — [list.html](examples/list.html)

`<ul>`, `<li>` and `list-style` are unsupported. Draw the bullet as a small `div` and line it up
with the first line using `align-items: flex-start` plus a `margin-top`.

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

A good `margin-top` is `(font-size × line-height − dot diameter) ÷ 2`
(above: (15 × 1.8 − 7) ÷ 2 ≈ 10px).

### 4.5 Tag / pill (shrink-to-fit box) — [pill.html](examples/pill.html)

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

There is no `flex-wrap`, so pills **never wrap**. Split them into one `.tags` row per line.

### 4.6 Vertical centring — [vcenter.html](examples/vcenter.html)

Two cases:

1. **Centre things side by side** → `align-items: center` on the flex parent
2. **Centre a single line inside a box** → set `line-height` to the **same px as the box `height`**

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

To centre content in a **full-height** box, use
`display: flex; flex-direction: column; justify-content: center` plus an explicit `height`
on the parent (content-box, so subtract the padding).

### 4.7 Left/right alignment and placing a note — [space-between.html](examples/space-between.html)

There is no `position`, so **all placement happens in the flow**.

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

- Push to both edges → `justify-content: space-between`
- Push one item right → insert an empty growing `div` (`flex: 1 1 0`)
- **Put a note directly under one particular element** → insert an empty
  `flex: none; width: <offset>px` spacer before the note. You compute the offset yourself
  (content-box, so add the `padding` and `border` of everything to its left).
  Change any of those sizes by 1px and you must update this number too

### 4.8 Heading and body text — [heading.html](examples/heading.html)

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

`h1`–`h6` and `p` carry UA margins (§2.5). Override them if you want to control the spacing.
For Japanese body text, `line-height: 1.7`–`1.9` reads well.

### 4.9 Skeleton of a 1200×630 OG image — [og-card.html](examples/og-card.html)

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

Render with `shashoku og-card.html -o og.png --width 1200 --height 630`.
`1072 = 1200 − 64×2`, `502 = 630 − 64×2` (§3-(1)).
`flex: 1 1 0` on the title **makes it grow so the footer sticks to the bottom**.

### 4.10 Vertical writing — [vertical.html](examples/vertical.html)

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

The rules:

- `writing-mode` may be set **only on the outermost element**. You cannot switch direction
  part-way (repeating the same value on a descendant is harmless; a different value is
  `error[unsupported-layout]`)
- **`--height` is required** (omitting it is `error[invalid-option]`). Auto height only exists
  for horizontal writing
- `width: %` is unsupported in vertical writing mode
- `width` is the size along the **block axis (horizontal)**, `height` along the
  **inline axis (vertical)**
- **`margin` and `padding` stay physical** (`top` means the top of the canvas). Since lines run
  right to left and characters run top to bottom, they mean:

  | Declaration | Meaning in vertical-rl |
  |---|---|
  | `padding-top` | space before the **start** of each line |
  | `padding-bottom` | space after the **end** of each line |
  | `padding-left` | space outside the **last** line |
  | `padding-right` | space outside the **first** line (towards the right edge) |
  | `margin-right` | gap to the **previous** block (to its right) |
  | `margin-left` | gap to the **next** block (to its left) |

- **Characters that fit on one line** ≈ `(height − vertical padding) ÷ font-size`.
  Above: `(900 − 40×2) ÷ 25 = 32.8`, and the poem is 32 characters, so it fits on one line.
  If the character count is fixed, solve for `font-size`
- **Line thickness** = `font-size × line-height`; lines × that must fit inside `width`
- There is no tate-chu-yoko (`text-combine-upright`)

### 4.11 Ruby — [ruby.html](examples/ruby.html)

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

- `<rt>` is `0.5em` of its parent by UA default
- **A line with ruby grows its line box automatically**, so ruby never collides with the line
  above. It does look cramped below about `line-height: 1.0`; **aim for `line-height: 1.8`**
- When the ruby and its base differ in width, the ruby is distributed 1:2:…:2:1 (JLREQ 3.3.6) and
  any overhang is allowed only over adjacent **kana** (JLREQ 3.3.8). Latin base text also takes ruby
- `letter-spacing` has no effect inside `<rt>`

### 4.12 Images — [image.html](examples/image.html)

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

Render with `shashoku image.html --image icon=examples/icon.png -o out.png --width 560`.

- **PNG only** (no JPEG, SVG or WebP). No URL and no file path is ever resolved
- `src` is the **name** you passed to `--image`. A mismatch is `error[image-not-found]`.
  The classic mistake is writing `src="avatar.png"` while passing `--image avatar=…`
- `alt` is optional (leaving it out is not an error; it is not drawn either)
- `<img>` is the only element that takes `width` / `height` / `border-radius` while inline,
  and its `border-radius` **clips the image**

---

## 5. What not to write, and what to write instead

| What you reach for | In shashoku | Instead |
|---|---|---|
| `<html>` `<head>` `<body>` `<!DOCTYPE>` | error | **Omit them.** Emit a fragment |
| `<ul>` `<li>` `<ol>` | error | Flex rows + a dot `div` (§4.4) |
| `<table>` `<tr>` `<td>` | error | Flex rows + `flex: 1 1 0` cells (§4.3) |
| `<strong>` `<b>` `<em>` `<code>` `<a>` `<section>` `<header>` | error | `span` (+ `font-weight` / `color` / `background-color`) or `div` |
| Emoji (🎉 😀 …) | **drawn as □ with a warning** | **Do not use them.** Use characters (`→` `↓` `▼` `※`) or a small coloured `div` |
| `display: inline-block` | error | Flex parent + `flex: none` child (§3-(4)) |
| `position` / `top` / `left` / `z-index` | error | Flex with `justify-content` / `align-items` / an empty spacer (§4.7) |
| `grid` / `grid-template-columns` | error | Flex + `flex: 1 1 0` (the `1fr` equivalent) |
| `float` | error | Flex |
| `flex-wrap` | error | One flex container per row |
| `align-self` / `order` / `flex-flow` | error | Put the elements in the order you want |
| `box-sizing` | error | Subtract padding and border from the width/height (§3-(1)) |
| `min-width` / `max-width` / `min-height` / `max-height` | error | A fixed `width` / `height` |
| `overflow` | error | Size it so nothing overflows; give up on clipping rounded corners |
| Gradients in `background` (`linear-gradient`, …) | error | **A flat colour** |
| `box-shadow` / `text-shadow` | error | Drop the shadow; show edges with a 1px border or a tinted background |
| `opacity` | error | Use a lighter colour directly (`#00000099` or a pale value) |
| `transform` (`rotate`, …) | error | Do not rotate |
| `::before` / `::after` + `content` | error | Write a **real element** (`span` / `div`). Remember there is no `position`, so it **lands in the flow** |
| `border-top` / `border-left` and friends | error | Insert a 1px-tall (or 1px-wide) `div` (§4.3) |
| `border-style: dashed` / `dotted` | error | `solid`. Distinguish the two kinds of line by **colour** |
| Per-corner `border-radius` (4 values) | error | One `border-radius` value |
| `border-collapse` | absent | Do not border the cells; draw rules with 1px `div`s |
| `-webkit-*` / `-moz-*` / `-ms-*` | error | **Drop the prefix** (the unprefixed name usually is supported) |
| Web fonts (`@font-face`, a Google Fonts `<link>`) | error | Pass fonts with the CLI's `--font`. Never reference them from the HTML |
| `@media` / `@import` / custom properties / `calc()` / `!important` | error | Write the resolved value |
| Descendant selectors (`.card p`) | error | Put a class directly on the element |
| JPEG / SVG / WebP images | error | Convert to PNG and pass with `--image` |

### Pairs that are dangerous to split

**When you delete an unsupported declaration, delete its partner too.**
Removing only one half produces no error and a broken picture.

| Pair | Removing only one half |
|---|---|
| `background-clip: text` + `color: transparent` | **The text disappears entirely.** If you drop `background-clip`, restore `color` to a real colour |
| `-webkit-text-fill-color: transparent` + a gradient background | Same as above |
| `position: absolute` + `top` / `left` | Dropping `position` alone makes the box **appear large at the top of the flow**. If it was decoration, delete the element |
| `overflow: hidden` + `border-radius` | Dropping `overflow` makes corners poke out square. Paint the background on the outer box only |

### Japanese typesetting notes

- **`letter-spacing` also applies between half-width digits.** `第 12 回` reads as `第 1 2 回`,
  so do not use it on headings that contain numbers
- Line-breaking rules (no leading punctuation or closing bracket, no trailing opening bracket)
  are applied automatically. You do not have to do anything
- **There is no way to keep a number and its counter (`2027 年度`) on the same line.**
  Adjust the width or the `font-size` if the break is unacceptable
- Justified text works via `text-align: justify`

---

## 6. When it fails

### 6.1 Reading an error

On failure the CLI writes to stderr and exits non-zero
(1 = rendering or I/O failure, 2 = bad arguments). The format is:

```
error[unsupported-value] at 82:77: `border-style: dashed` is not supported (supported: solid, none)
        ^^^^^^^^^^^^^^^     ^^ ^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        identifier (stable) ln:col  what is wrong + how to fix it
```

- The **identifier** (`unsupported-value`, …) and the **location** (`line:column`) are the contract.
  That is what a program should read
- The **wording** is for humans and AIs and may change between versions
- Errors without a location (`invalid-option`, …) have no `at` part

The identifiers you will actually see:

| Identifier | Meaning | First thing to do |
|---|---|---|
| `unsupported-tag` | unsupported tag | Look it up in §5 and use `div` / `span` / `p` |
| `unsupported-attribute` | unsupported attribute | Delete it (only `style` `class` `id` survive) |
| `unsupported-property` | unsupported property | §5. If it is prefixed, drop the prefix |
| `unsupported-value` | property is fine, value is not | Use one of the values in the `supported: …` list |
| `unsupported-layout` | unsupported layout | Box properties on an inline (§3-(2)(3)) or writing mode (§4.10) |
| `css-parse` | unsupported CSS syntax / selector | Remove descendant selectors, pseudo-elements, at-rules, `!important` |
| `html-parse` / `invalid-utf8` | broken HTML | Fix the closing tags and the encoding |
| `image-not-found` | `<img src>` names no image passed via `--image` | Make the names match (§4.12) |
| `image-decode` | the file is not a readable PNG | Convert it to PNG |
| `invalid-option` | bad option | e.g. vertical writing without `--height`, or content of zero height |
| `limit-exceeded` | input exceeds a `RenderLimits` bound | Reduce characters, images or dimensions |

### 6.2 Warnings (rendering continues)

```
warning[missing-glyph]: no font has a glyph for U+1F600 at 1:6
```

`missing-glyph` means **tofu (□) was drawn** — an emoji, or a character missing from the fonts you
passed. Fix it by not using the character, or by adding a font.
**A warning still produces a PNG and still exits 0.** If you publish automatically, read stderr too.

### 6.3 What order to fix things in

1. **Tags first** (`unsupported-tag`). Wrapper tags, `ul` / `li` / `table` change the structure
2. Then **CSS syntax** (`css-parse`): selectors, at-rules, `!important`
3. Then **properties and values** (`unsupported-property` / `unsupported-value`), mechanically via §5
4. Then **layout** (`unsupported-layout`). Where box properties sat on an inline element,
   **delete the declaration rather than adding `display: block`**
   (`display: block` breaks the run of text and splits one sentence across lines).
   If the box really is a standalone component, turn it into a `div` and make it a flex item
5. **Once the errors are gone, look at the PNG.** No errors does not mean the picture is right
   (overlaps, unintended placement and text the same colour as its background are not detected)

### 6.4 Today versus planned (this is a 0.1.0 matter)

shashoku 0.1.0 today:

- **Errors come one at a time.** The same mistake in twenty places is reported once per run.
  Expect a fix-and-rerun loop (testing averaged 16.5 runs for one HTML file)
- The only warning is `missing-glyph` (tofu). **Content overflowing a fixed canvas is not detected**
  — fix `--height` too small and you silently get a cropped PNG
- On success the CLI **prints nothing**. If you omitted `--height`, read the actual height off the PNG
- Hints ("write this instead") exist only for properties with a verified substitute, and they are
  appended in parentheses at the end of the message

Designed and being implemented ([ARCHITECTURE.md A46](../ARCHITECTURE.md)). **Not available yet**:

- **All at once**: the HTML and style stages collect every problem they find before failing
- **Hints on their own line**: a `  hint: …` line, separate from the message
- **Overflow warnings**: `warning[content-overflow]` with an `overflow_px` amount
- **`--strict`**: fail (and write no PNG) if there is at least one warning — the "do not publish"
  signal for a server
- **`--diagnostics json`**: one JSON object on stdout, shaped like this:

```json
{"ok": true, "width": 1200, "height": 630, "truncated": false,
 "errors": [{"kind": "unsupported-property", "message": "…", "hint": "…",
             "line": 3, "column": 14, "offset": 120, "warning": null}],
 "warnings": [{"kind": "missing-glyph", "detail": "…", "codepoint": 128512,
               "line": 3, "column": 1, "offset": 88, "overflow_px": 0}]}
```

`line` / `column` / `offset` are `null` when there is no location, `hint` is `""` when there is none.
With `--diagnostics json`, `-o` is required and `--dump-stage` cannot be combined with it.

---

## 7. The CLI

```
shashoku <input.html> -o <out.png> [--width N] [--height N] [--scale S]
         [--font <file>]... [--image <name>=<file.png>]...
```

| Option | Meaning |
|---|---|
| `--width <N>` | Viewport width in CSS px (default 1200) |
| `--height <N>` | Viewport height. **Omit it and the height follows the content** (required in vertical writing mode) |
| `--scale <S>` | Output scale (default 1.0; `2` for retina) |
| `--font <file>` | Font, repeatable — **the order is the fallback order**. Omit for the embedded default (Noto Sans JP Regular / Bold) |
| `--image <name>=<file>` | A PNG image, referenced as `<img src="name">` |
| `--compression <0-9>` | PNG compression level (default 6). Pixels are unaffected |
| `--overflow` | Overflow policy `oidashi` (default) / `oikomi` / `burasage` |
| `--line-break` | Strictness `strict` (default) / `normal` / `loose` |
| `--trim-line-start` | Trim the space before an opening bracket at the start of a line (off by default) |
| `--no-trim-line-end` | Keep the space after a closing bracket or punctuation mark at the end of a line (trimmed by default) |
| `--no-collapse-punctuation` | Keep the space between consecutive punctuation marks (collapsed by default) |
| `--dump-stage` | Dump an intermediate form: `dom` / `style` / `box` / `display-list` / `svg` |
| `--version` / `--license` | Version / licences (`shashoku --help` lists everything) |

```bash
# height follows the content
shashoku card.html -o card.png --width 640
# OG image (fixed height)
shashoku og-card.html -o og.png --width 1200 --height 630
# with an image
shashoku image.html --image icon=examples/icon.png -o out.png --width 560
# vertical writing (--height is mandatory)
shashoku vertical.html -o v.png --width 480 --height 900
# check the box dimensions numerically
shashoku card.html --dump-stage box --width 640
```

**Bold** comes from `font-weight` alone (the default font's Bold is selected).
`font-family` only reorders the family preference; weight matching works with or without it.

The same input (HTML, fonts, images, options) always produces a **byte-identical PNG**.

---

## 8. If you get stuck

1. Check that `shashoku --version` matches the version at the top of this document
2. Do not use anything absent from the tables in §2
3. Substitute using the table in §5
4. If it still errors, use only the values listed in the message's `supported: …`
5. Once the errors are gone, **open the PNG and look at it**

The authority on the engine side is the "対応している HTML / CSS" section of
[README.md](../../README.md). If this document disagrees with it, the README wins.
