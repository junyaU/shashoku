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
- Character references (`&amp;` `&lt;` `&gt;` `&nbsp;` `&#12354;`) are allowed.
  `&nbsp;` (U+00A0) is a space the line breaker will **not** break at — use it to tie a number to
  its unit (§5)
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
| Box | `display` `box-sizing` `width` `height` `margin` `padding` `border` `border-width` `border-style` `border-color` `border-radius` `background` `background-color` |
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
- `margin: auto` works. Besides centring a block horizontally, **on a flex item it absorbs the free
  space**: `margin-left: auto` on a child of a row pushes it to the right edge, `margin-top: auto`
  on a child of a column pushes it to the bottom (§4.14)

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

> `font-family` only matches the **family name a font file declares for itself**. If nothing in the
> list can be satisfied (names you did not pass, plus generics shashoku cannot interpret such as
> `serif` or `monospace`), you get **`warning[font-not-found]` and the text is drawn with the first
> font you passed** (rendering continues). `sans-serif`, `system-ui` and `ui-sans-serif` mean
> **"the first font passed is fine"** in shashoku, so they raise nothing — pass only a serif face
> with `--font` and that serif face draws the text, still without a warning. **The engine never
> classifies a font's style** (see §7 for serif and monospace).

**A symbol only appears if the font has it.** Anything missing renders as □ (tofu) and produces a
`warning[missing-glyph]`. The list below was checked by actually drawing it with the embedded
default font (Noto Sans JP); with a different font passed via `--font` the answer changes.

| Group | Symbols that render |
|---|---|
| Arrows | `→` `←` `↑` `↓` `⇒` `⇐` `⇔` `↔` `⇨` `➡` |
| Shapes | `●` `○` `◎` `■` `□` `◆` `◇` `▲` `△` `▼` `▽` `▶` `▷` `◀` `◁` `▪` `▫` `★` `☆` |
| Signs and maths | `✓` `×` `✚` `※` `〓` `¬` `∞` `≠` `≦` `≧` `√` `∴` `∵` `−` `±` `÷` `‰` `℃` `°` `′` `″` |
| Dashes and punctuation | `–` `—` `―` `…` `〜` `～` `「」` `『』` `（）` `〔〕` `【】` `〈〉` `《》` `・` |
| Currency and reference | `€` `¥` `£` `$` `©` `®` `™` `§` `¶` `†` `‡` `№` `①` `②` `③` `Ⅰ` `Ⅱ` `Ⅲ` |
| Others | `♦` `♥` `♠` `♣` `♪` `☀` `☁` `☂` `☃` `☎` `✂` `⌘` `⏎` `⇧` `⚠` `❖` |

**These came out as □** (the same check actually produced a warning for each):

- Every emoji. `🎉` `😀` `✅` `❗` `⭐` `❤` `⚡` `⏰` `✈` `✉` `⬛` `⬜`
- Most check marks and crosses. `✔` `✕` `✗` `✘` `✖` `☑` `☐` `☒`
  (**only `✓` and `×` render**)
- `≒` `✦` `✳` `✴` `➔` `⌥`

Writing `⚠️` with a variation selector (`U+FE0F`) drops the selector and gives you the
**monochrome `⚠`**. There is no way to get a colour emoji.

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

## 3. Five things to know before you start

In the order real AI-written HTML tripped over them during testing.
**Getting these right removes most of the retry loop.**
(1) is not a trap but a shortcut: knowing it saves you arithmetic.

### (1) `box-sizing` accepts both values; the default is content-box

Both `content-box` (the default) and `border-box` work. **The default is content-box, same as a
browser**, so if you write nothing, `width` / `height` **exclude padding and border** and you
subtract them yourself to hit an outer size.

```
1200×630 OG image, padding 64px, no border
  → width: 1072px;  height: 502px;  padding: 64px;
     (1072 + 64×2 = 1200, 502 + 64×2 = 630)
Add a 2px border and subtract another 2×2 = 4px
  → width: 1068px;  height: 498px;  padding: 64px;  border: 2px solid #333;
```

**Put `* { box-sizing: border-box }` at the top and none of that subtraction is needed** —
`width` / `height` then *are* the outer size of the box.

```html
<style>
  * { box-sizing: border-box }
  .card { width: 600px; height: 160px; padding: 24px; border: 2px solid #333;
          background: #ffffff; font-size: 17px; line-height: 1.8; color: #1b2733; }
</style>
<div class="card">外寸（600×160）をそのまま書けます。padding と border は内側に入ります。</div>
```

- `border-box` applies to `flex-basis` and to `<img>` as well (both the CSS `width` / `height` and
  the `width` / `height` attributes), not just to `width` / `height` on a box
- If there is nothing left to subtract (`width: 10px; padding: 20px`), the content size stops at 0
  and the box ends up larger than the value you wrote. Browsers do the same
- It works the same in vertical writing (the padding subtracted is the one on the inline axis)
- **Every example in §4 below is written for content-box.** If you switch to `border-box`, add the
  padding and border back into the `width` / `height`

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

### (3) The **direct children** of a flex container are blockified (grandchildren are not)

A direct child of a `display: flex` element is blockified even if you wrote it as a `span`
(CSS Display 3 §2.7). It takes `padding` / `border` / `width`, and **a `div` and a `span` produce
exactly the same picture** (a byte-identical PNG).

```html
<!-- these two are the same -->
<div style="display: flex; gap: 8px">
  <span style="flex: none; padding: 5px 12px; background: #e7f0ff">Policy</span>
  <div style="flex: none; padding: 5px 12px; background: #e7f0ff">AI</div>
</div>
```

There are three exceptions — **`<img>`, `<ruby>` and `<br>`** — which stay inline even as direct
children of a flex container. `padding` on a `<ruby>` is still `error[unsupported-layout]`; wrap it
in a `div` or a `span` if it needs a box. `<img>` works as a flex item exactly as before (§4.12).

**Only direct children are blockified.** A `span` sitting inside running text (a grandchild of the
flex container) stays inline, and trap (2) applies to it unchanged.

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
do not fit overflow the parent (**overflowing the parent box is not detected**; only content that
leaves the canvas is reported, as `warning[content-overflow]` — see §6.4).

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
`flex: none; width: 180px` for a fixed column. A child may be a `div` or a `span` (§3-(3)).
For flex inside flex (arrows between steps, a row of tags inside a card) see §4.13.

**An unbreakable long word breaks the equal split.** A `flex: 1 1 0` child never gets narrower than
**the width it cannot shrink past** (the longest unbreakable word plus its `padding` and `border`).
Put a URL, a long English word, a run of alphanumerics or a hash in one and that child grows while
the others shrink; once the total exceeds the parent it **overflows the parent** (and, if it leaves
the canvas, raises `warning[content-overflow]`). Add **`overflow-wrap: anywhere`** to any child that
may receive a long word. **`overflow-wrap: break-word` has no effect on a `flex: 1 1 0` child** (it
does not count towards the shrinkable width; CSS Text 3 §5.4). Worked example in §4.3.

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

**An empty cell has no height.** An empty `div` has a content height of 0, so a row whose cells are
**all empty** is only as tall as its padding (20px for the `.td` above: 10 + 10). To keep one line
of height, put a `&nbsp;` in it — it is accepted as a character reference and carries a height of
`font-size × line-height` (25.5px here, for a 45.5px row). If any cell in the row has content, the
empty ones are stretched by `stretch` anyway and need no `&nbsp;`.

**Use `&nbsp;` when a narrow column splits a number from its unit.** In a 70px box at
`font-size: 15px`, `Idle 17 ms (steady)` breaks as `Idle 17` / `ms` / `(steady)`; written
`17&nbsp;ms` it becomes `Idle` / `17 ms` / `(steady)`, keeping the number and the unit on one
line (§5).

**Add `overflow-wrap: anywhere` to any column that may hold a long word.** Cells are `flex: 1 1 0`,
so an unbreakable token (a URL, a hash, a run of alphanumerics) widens that one column and **the
header row and the body row stop lining up** (§4.2). Putting a 40-character commit hash in the
`.tbl` above and rendering at `--width 400`:

| | Header row columns | Body row columns | Result |
|---|---|---|---|
| as written | 132.7 / 132.7 / 132.7 | 54.0 / 349.8 / 59.6 | columns misaligned, 64.4px past the right edge with `warning[content-overflow]` |
| `overflow-wrap: anywhere` | 132.7 / 132.7 / 132.7 | 132.7 / 132.7 / 132.7 | aligned; the hash wraps mid-token |
| `overflow-wrap: break-word` | — | same as "as written" | **no effect** |

```html
<style>
  /* add to the .tbl / .tr / .td / .rule above */
  .long { overflow-wrap: anywhere; }
</style>
<div class="tr"><div class="td">コミット</div><div class="td long">9f2c1b4e8d7a6503f1e2c9b8a7d6e5f4c3b2a190</div><div class="td">main の先端</div></div>
```

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
Writing the children as `<span class="pill">` gives the same result (direct children of a flex
container are blockified; §3-(3)).

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
on the parent (content-box, so subtract the padding; with `box-sizing: border-box` you write the
outer size directly and subtract nothing — §3-(1)).
To centre the body text and still pin a byline or a date to the bottom edge, see §4.14.

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
  (content-box, so add the `padding` and `border` of everything to its left; with
  `box-sizing: border-box` each `width` is already the outer size, so there is nothing to add).
  Change any of those sizes by 1px and you must update this number too
- To push **only one** item to the right edge, `margin-left: auto` does the same thing without a
  spacer `div`

How to compute the spacer width:

1. Add up the outer size of **every preceding sibling**. One sibling's outer size is
   `width + left/right padding + left/right border` (content-box, so `width` excludes them;
   with `border-box`, `width` *is* the outer size)
2. Add one `gap` for **every gap you cross** (n preceding siblings means n gaps)
3. Give the note's row the **same `padding-left`** as the row above it — any difference shifts
   the note by exactly that amount
4. Check it with `--dump-stage box`: the first number of the note's `rect` must equal the first
   number of the target element's `rect`

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

One cell measures `160 + 10×2 + 1×2 = 182` on the outside and we cross one gap, so the spacer is
`182 + 12 = 194px`. With `shashoku notes.html --dump-stage box --width 600`, the second cell and
the note both start their `rect` at `210` (= 16 + 194).

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
`1072 = 1200 − 64×2`, `502 = 630 − 64×2` (§3-(1)). Add `* { box-sizing: border-box }` at the top and
you can write `width: 1200px; height: 630px` instead.
`flex: 1 1 0` on the title **makes it grow so the footer sticks to the bottom**.

### 4.10 Vertical writing — [vertical.html](examples/vertical.html)

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

#### Placing things along the block axis (left and right)

"Title at the right edge, signature at the left edge" is built by making the outermost element a
`display: flex`. The axes rotate by 90° in vertical writing, so settle which property acts in which
direction first (this table was checked with `--dump-stage box`):

| `flex-direction` | Main axis (`justify-content`) | Cross axis (`align-items`) |
|---|---|---|
| `row` (default) | **vertical** (top → bottom). `flex-start` = top, `flex-end` = bottom | **horizontal** (right → left). `flex-start` = **right**, `flex-end` = **left** |
| `column` | **horizontal** (right → left). `flex-start` = **right**, `flex-end` = **left** | **vertical** (top → bottom). `flex-start` = top, `flex-end` = bottom |

`margin` is the one thing that ignores this table and **stays physical**. To open space along the
block axis use `margin-right` (gap to the block on its right) and `margin-left` (gap to the block on
its left).

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

`shashoku letter.html -o letter.png --width 464 --height 584` puts the title at the right edge and
the signature at the left edge (the main axis of a `column` runs right to left, so `space-between`
spreads them horizontally).

**Along the inline axis (top and bottom)** use `align-items` on the same flex container (it applies
to every child) or, per child, `margin-top: auto` / `margin-bottom: auto`. The signature in the
example above does exactly that: the `auto` in `margin: auto 28px 0 0` (that is `margin-top`)
absorbs all the free space and **pins the signature to the bottom edge**. You could instead count
the distance and write `padding-top: 560px`, but then every change of body text or `font-size`
means counting again.

> The `rect` in `--dump-stage box` is in **logical coordinates**. In vertical writing it reads
> `[offset along the inline axis (from the top), offset along the block axis (from the right edge),
> inline size, block size]` — **the larger the second number, the further left**.

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
  above (not even at `line-height: 1.0`). It does look cramped that tight;
  **aim for `line-height: 1.8`**
- **The height of such a line** (horizontal writing) is
  `font-size × max(line-height, A + line-height ÷ 2)`, where `A` is the font's
  **ascent + descent expressed in em** — about **1.45** for the default font (Noto Sans JP).
  So below `2 × A ≈ 2.9` the ruby makes the line taller, and **only the top side grows**.
  Measured with the default font at `font-size: 20px`: a `line-height: 1.8` line is 36px and the
  same line with ruby is **46.95px**; at `line-height: 2.9` it stays 58px either way
- Consequently, **mixing lines with and without ruby makes the leading uneven**. Either set
  `line-height` to 2.9 or more for the whole paragraph (which is very airy) or accept it.
  The annotation itself does not participate in the line height — only the overhang that puts it
  outside the base text does
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
- **`<img>` can be a direct child of a flex container.** `.head` above *is* the flex container —
  there is no need to wrap the `<img>` in a `div`. Size it with `width` / `height`

### 4.13 Flex inside flex — [flow.html](examples/flow.html)

With no `position` and no `grid`, anything slightly involved is built by **nesting flex
containers**. Each level only has two decisions: the **direction** (`flex-direction`) and the
**cross-axis alignment** (`align-items`).

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

- **The arrow between steps** is a `flex: none` `div` holding the single character `→`.
  `align-items: center` on the outer flex centres it against the steps, so you never compute its
  position (`→` renders with the default font; see §2.4)
- **The steps are `flex: 1 1 0`** so they share the width evenly; the arrows are `flex: none` and
  take only what they need
- **Inside a step, `flex-direction: column`** stacks the number, the heading and the tag row, and
  `gap` becomes the spacing between them
- **The tag row is yet another flex** (`display: flex` with `flex: none` children) — the same shape
  as §4.5
- Nesting does not bring `flex-wrap` back. **If it does not fit, it overflows** (overflow out of a
  parent box is not detected). Raise `--width` when you add steps
- **To line up the rows below headings of different line counts**, give the heading (`.name`) a
  **fixed `height` worth two lines** (`line-height: 20px; height: 40px`). Beware: **a third line
  silently overlaps the row below** (no warning; even `--strict` succeeds). Check with
  `--dump-stage box` that the box's `lines` count is 2 or fewer. When a step name breaks mid-word,
  put the break where you want it with `<br>` (§5)

### 4.14 Quote card (body centred, byline pinned to the bottom) — [quote.html](examples/quote.html)

A fixed-height canvas with the body in the middle and the byline at the bottom edge. There is no
`position`, so you solve it with **one box that soaks up the free space**.

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

Render with `shashoku quote.html -o quote.png --width 600 --height 360`
(`552 = 600 − 24×2`, `312 = 360 − 24×2`; with `* { box-sizing: border-box }` at the top you write
`width: 600px; height: 360px` and subtract nothing — §3-(1)).

- Make the sheet a `flex-direction: column` and give the **body box `flex: 1 1 0`**: it takes all
  the space the byline does not. `justify-content: center` inside it centres the body, and the
  byline lands on the bottom edge
- The body is centred within **the area left over after the byline**, which sits half the byline's
  height above the centre of the canvas. To centre it on the canvas exactly, add an empty
  `flex: none; height: <byline height>px` `div` **above** the body box. Pinning the byline's
  `height` and `line-height` to the same px makes that easy: 22px for both in the example above
  puts the centre of the body at exactly 180px, the middle of the canvas
- If the body may stay at the top and only the byline needs to be pinned, you need neither the
  spacer nor `flex: 1 1 0` — just `margin-top: auto` on the byline, which absorbs all the free space
- In vertical writing `margin-top: auto` still pushes to "the end of the inline axis", i.e. the
  **bottom edge** (checked in §4.10). To reach an edge along the block axis (left or right), read
  the `justify-content` table in §4.10

---

## 5. What not to write, and what to write instead

| What you reach for | In shashoku | Instead |
|---|---|---|
| `<html>` `<head>` `<body>` `<!DOCTYPE>` | error | **Omit them.** Emit a fragment |
| `<ul>` `<li>` `<ol>` | error | Flex rows + a dot `div` (§4.4) |
| `<table>` `<tr>` `<td>` | error | Flex rows + `flex: 1 1 0` cells (§4.3) |
| `<strong>` `<b>` `<em>` `<code>` `<a>` `<section>` `<header>` | error | `span` (+ `font-weight` / `color` / `background-color`) or `div` |
| Emoji (🎉 😀 …) | **drawn as □ with a warning** | **Do not use them.** Use a symbol from the list in §2.4 (`→` `▼` `※` `✓`) or a small coloured `div` |
| `display: inline-block` | error | **Inside a flex container just drop the declaration** — a direct child is blockified and already takes box properties (§3-(3)). Elsewhere, a flex parent with a `flex: none` child (§3-(4)) |
| `position` / `top` / `left` / `z-index` | error | Flex with `justify-content` / `align-items` / an empty spacer (§4.7) |
| `grid` / `grid-template-columns` and the other `grid-*` | error | For **equal-width columns in one row**, `display: flex` on the parent and `flex: 1 1 0` on each child (the `1fr` equivalent; §4.3). For **several rows**, one flex container per row (§4.2). **Unequal columns and spanning cells (`grid-column: span 2`) have no equivalent** |
| `float` | error | Flex |
| `flex-wrap` | error | One flex container per row |
| `align-self` / `order` / `flex-flow` | error | Put the elements in the order you want |
| `min-height` | error | If the parent is a **flex container with the default `align-items: stretch`**, just **drop it** (the item already fills the cross size). If the height is known, use `height`. Otherwise drop it and let the content decide |
| `min-width` / `max-width` / `max-height` | error | A fixed `width` / `height`, or drop it |
| `overflow` | error | Size it so nothing overflows; give up on clipping rounded corners |
| Gradients in `background` (`linear-gradient`, …) | error | **A flat `background-color`** (the picture becomes flat). If it is paired with `background-clip: text` + `color: transparent`, drop **both** (see "Pairs that are dangerous to split" below) |
| `background-image` (`url(...)`, …) | error | A flat `background-color`, or an `<img>` passed with `--image` (§4.12) |
| `box-shadow` / `text-shadow` | error | Drop the shadow; show edges with a 1px border or a tinted background |
| `opacity` | error | Use a lighter colour directly (`#00000099` or a pale value) |
| `transform` (`rotate`, …) | error | Do not rotate |
| `::before` / `::after` + `content` | error | Write a **real element** (`span` / `div`). Remember there is no `position`, so it **lands in the flow** |
| `border-top` / `border-left` and friends | error | For a **divider between boxes**, insert a `div` with `height: 1px` (`width: 1px` in a row) and a background colour (§4.3) — note it **takes 1px of space in the flow**. For **one edge of a frame** there is no equivalent: use a full four-sided `border` or drop it |
| `border-style: dashed` / `dotted` | error | `solid`. Distinguish the two kinds of line by **colour** |
| Per-corner `border-radius` (4 values) | error | One `border-radius` value |
| `border-collapse` | absent | Do not border the cells; draw rules with 1px `div`s |
| `-webkit-*` / `-moz-*` / `-ms-*` | error | **Drop the prefix** (the unprefixed name usually is supported) |
| Web fonts (`@font-face`, a Google Fonts `<link>`) | error | Pass fonts with the CLI's `--font`. Never reference them from the HTML |
| `@media` / `@import` / custom properties / `calc()` / `!important` | error | Write the resolved value |
| Descendant selectors (`.card p`) | error | Put a class directly on the element |
| JPEG / SVG / WebP images | error | Convert to PNG and pass with `--image` |

### Change the picture, change the words

Working through the table above, it is easy to end up with **a new picture and the old wording**.
Nothing errors and nothing warns, so you find out when you look at the PNG.

- Dashed border (`dashed`) replaced by a solid one or a tint → "the part **inside the dashed box**"
  is now a lie
- An arrow image or a `::before` ornament replaced by the character `→` → "the **downward arrow**
  below" no longer matches
- A shadow (`box-shadow`) replaced by a border → "the card that **appears to float**" no longer
  matches
- A gradient replaced by a flat colour → "**the blue-to-purple gradient**" no longer matches

Treat every substitution as **two edits, the style and the prose**. Scan the legend, the captions
and the body for any phrase that points at the appearance, and fix them at the same time.

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
- **Tie a number to its counter or unit with `&nbsp;` (U+00A0).** Written `2027&nbsp;年` or
  `17&nbsp;ms`, the line breaker treats that gap as unbreakable, so the number and the counter are
  never separated at a line end (it has the same width and look as an ordinary space, and produces
  neither tofu nor a warning). In a 110px box, `法改正は 2027 年度を視野に入れる。` breaks as
  `法改正は 2027` / `年度を…`; written `2027&nbsp;年度` it becomes `法改正は` / `2027 年度を…`
- **`&nbsp;` protects only that one gap.** Japanese breaks even inside a word, so the same sentence
  in a 130px box breaks as `法改正は 2027 年` / `度を…` — **inside the counter**.
  **There is no way to keep a whole word together** (`white-space: nowrap` is not supported; it is
  an `unsupported-property` error). Widen the box or shorten the word
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

Whenever there is a known fix, an indented **`  hint: …` line** follows (verbatim output below).

```
error[unsupported-property] at 3:8: `min-height` is not a supported property
  hint: no min/max sizes. If the parent is a flex container with the default `align-items: stretch`, drop it (the item already fills the cross size); if the height is known, use `height`; otherwise drop it and let the content decide the height
error[unsupported-value] at 7:17: `display: grid` is not supported (supported: block, flex, inline, none)
  hint: no grid. For equal-width columns in one row, use `display: flex` on the parent and `flex: 1 1 0` on each child (guide §4.3); for several rows, one flex row per line (guide §4.2). Unequal or spanning grids have no equivalent
error[unsupported-layout] at 12:16: `padding-top` is not supported on an inline element (`display: inline`); only `<img>` takes box properties while inline
  hint: drop the declaration; `display: block` would accept it but breaks the surrounding text flow. If the box is a standalone part (tag / pill / badge), make it a flex item: a `div` inside a `display: flex` parent (guide §3-(4))
```

- **A hint may be conditional.** "If the parent is … drop it; if the height is known, use `height`",
  "unequal or spanning grids have no equivalent" — read the cases and pick the one you are in
- The third one is a `span` **inside running text** (a grandchild of a flex container). The same
  declaration on a **direct child** of a flex container is not an error at all (§3-(3))
- No hint means "there is no verified substitute". Fall back to the table in §5
- Even when several elements match one rule, **identical diagnostics at one location are reported
  once**
- To read them from a program use `--diagnostics json`; `hint` carries the same string (§6.4)

The identifiers you will actually see:

| Identifier | Meaning | First thing to do |
|---|---|---|
| `unsupported-tag` | unsupported tag | Look it up in §5 and use `div` / `span` / `p` |
| `unsupported-attribute` | unsupported attribute | Delete it (only `style` `class` `id` survive) |
| `unsupported-property` | unsupported property | §5. If it is prefixed, drop the prefix |
| `unsupported-value` | property is fine, value is not | Use one of the values in the `supported: …` list |
| `unsupported-layout` | unsupported layout | Box properties on an inline inside running text (§3-(2)) or writing mode (§4.10) |
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

```
warning[content-overflow]: content overflows the canvas by 430.0px (bottom) at 19:1
```

`content-overflow` means **content sticks out of the fixed canvas and that part is cut off**.
`(bottom)` is the edge it went over, and the fix depends on it: `bottom` means raise `--height` or
cut content, `right` means raise `--width` or shrink the box's width and padding. If you omit
`--height` (the height follows the content) nothing can overflow vertically, so only the horizontal
edges are checked.

```
warning[font-not-found]: no requested font family is loaded (`Hiragino Mincho ProN`, `serif`); text uses `Noto Sans JP` instead at 1:57
```

`font-not-found` means **none of the families in `font-family` could be satisfied, so another font
drew the text** (asking for a serif face and getting the sans default is the typical case). Fix it by
passing that font with `--font`, by dropping `font-family`, or by writing `sans-serif`.
`sans-serif`, `system-ui` and `ui-sans-serif` mean **"the first font passed is fine"**, so they raise
nothing whatever that font is (**the engine does not classify font styles**). `serif`, `monospace`
and the other generics are a concrete request that shashoku cannot check, so on their own they are
not satisfied (§7).

**A warning still produces a PNG and still exits 0.** If you publish automatically, read stderr too.
**`--strict` turns warnings into failures**: no PNG is written and an existing file is left untouched.

### 6.3 What order to fix things in

1. **Tags first** (`unsupported-tag`). Wrapper tags, `ul` / `li` / `table` change the structure
2. Then **CSS syntax** (`css-parse`): selectors, at-rules, `!important`
3. Then **properties and values** (`unsupported-property` / `unsupported-value`), mechanically via §5
4. Then **layout** (`unsupported-layout`). Where box properties sat on an inline element,
   **delete the declaration rather than adding `display: block`**
   (`display: block` breaks the run of text and splits one sentence across lines).
   If the box really is a standalone component (a tag / pill / badge), make it a **direct child**
   of a `display: flex` parent — a `div` or a `span`, either works (§3-(3))
5. **Once the errors are gone, look at the PNG.** No errors does not mean the picture is right
   (overlaps, unintended placement and text the same colour as its background are not detected)

### 6.4 Today versus planned (this is a 0.1.0 matter)

shashoku 0.1.0 today:

- **Every error found is reported at once.** The HTML and style stages collect every problem they
  can step over (unsupported tags, attributes, properties, values, selectors) before failing, so you
  do not have to rerun after every single fix. **Structural breakage stops the run on the spot**
  though (an unclosed tag, a bare `&`, invalid UTF-8) — fix that first and run again
- **Hints ("write this instead") are on their own line** (`  hint: …`), only where there is a
  verified substitute, and **conditional when the substitute depends on the situation** ("if the
  parent is a stretch flex container, drop it", "unequal or spanning grids have no equivalent").
  No hint means there is no substitute
- **Identical diagnostics at one location are reported once**, even when several elements match the
  same rule
- There are three warnings: `missing-glyph` (tofu), `content-overflow` (**content that does not fit
  the fixed canvas**) and `font-not-found` (**no requested `font-family` could be satisfied**).
  Set `--height` too small and you are told how much is cut off, and on which edge
- **`--strict`** turns any warning into a failure and **writes no PNG** (an existing file is left
  untouched). That is the "do not publish" signal for a server
- **`--diagnostics json`** writes one JSON object to stdout (on success and on failure)
- On success the CLI writes one line to stderr: `wrote out.png (1200x630)`. That is where you read
  the actual height when you omitted `--height`

The diagnostics JSON (real output, only line-wrapped here):

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

- `line` / `column` / `offset` are `null` when there is no location, `hint` is `""` when there is none
- `warning` carries the original warning kind (`"missing-glyph"`, …) only on an error promoted by
  `--strict` (`"kind": "warning-as-error"`); otherwise it is `null`
- `edge` is `"top"` / `"right"` / `"bottom"` / `"left"` on `content-overflow`, `null` otherwise
- `truncated` means the diagnostics were cut off at the limit (100 entries by default)
- With `--diagnostics json`, `-o` is required, `--dump-stage` cannot be combined with it, and the
  human-readable stderr output is suppressed

**Still not detected**: text overflowing a fixed-size box (as opposed to the canvas), overlaps, and
text the same colour as its background. No errors and no warnings does not mean the picture is right.

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
| `--strict` | Turn warnings (tofu, canvas overflow) into failures. No PNG is written |
| `--diagnostics` | How to report: `human` (default) / `json`. `json` needs `-o` and cannot be combined with `--dump-stage` |
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
# diagnostics in machine-readable form (one object, success or failure)
shashoku card.html -o card.png --width 640 --diagnostics json
# treat warnings (tofu, overflow) as failures (no PNG is written)
shashoku og-card.html -o og.png --width 1200 --height 630 --strict
```

**Bold** comes from `font-weight` alone (the default font's Bold is selected).
`font-family` only reorders the family preference; weight matching works with or without it.

### Serif and monospace (`--font` and `font-family`)

**The embedded default font is only Noto Sans JP, Regular and Bold.** Writing
`font-family: serif` or `font-family: monospace` changes **not one pixel** (generics shashoku cannot
interpret, and the names of fonts you did not pass, are skipped). It is not silent about it, though:
if nothing in the list can be satisfied you get **`warning[font-not-found]`**.
To get a serif or a monospace face, pass that font file with `--font`.

`sans-serif`, `system-ui` and `ui-sans-serif` are the exception that raises nothing: in shashoku they
mean **"the first font passed is fine"**. **Pass only a Mincho face and write
`font-family: sans-serif` and you get Mincho, with no warning** — shashoku never classifies a font's
style. When the face matters, write the **family name the font declares** instead of a generic.

- `font-family` matches the **family name the font file declares for itself** (case and surrounding
  whitespace are ignored): `Noto Sans JP` for `NotoSansJP-Regular.otf`, `Noto Sans` for
  `NotoSans-Regular.ttf`. Not the file name, and not a CSS generic name
- A match only moves that family to the front; **the rest follow in `--font` order**. Characters the
  first family does not have fall through to the next font
- **Passing even one `--font` drops the default font.** Pass a Japanese font yourself if you need
  Japanese (a Latin-only font leaves every Japanese character as □)
- If you do not know the family name, omit `font-family` and let **`--font` order decide** — that
  always works

```html
<p style="font-family: 'Noto Sans'">Hamburgefonstiv 0123 / 写植</p>
```

```bash
shashoku doc.html -o doc.png --width 460 \
  --font NotoSansJP-Regular.otf --font NotoSans-Regular.ttf
```

Here the Latin text is set in Noto Sans and `写植`, which Noto Sans does not have, falls through to
Noto Sans JP. Serif and monospace work the same way: add the file to `--font` and put that file's
family name (`Noto Serif JP`, `Noto Sans Mono`, …) in `font-family`.

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
