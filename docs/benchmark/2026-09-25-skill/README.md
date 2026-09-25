# 2026-09-25 ローカル skill での試作（比較表・パイプライン図・SNS カード）

`docs/guide/SKILL.md` と `docs/guide/writing-html-for-shashoku.md` をそのまま渡して、
shashoku 自身の紹介に使える絵を 3 枚作ったときの記録。**依頼集（`../requests.md`）第 2 節の
「実利用で見つかった依頼」の出どころ**がこれ。作ったのはオーケストレーター（メインの Claude）。

このディレクトリの 3 枚は **README.md への掲載を前提にしたものではありません**。
掲載するかどうかはユーザーの別判断で、ここには「skill を実際に使うと何が起きるか」の記録として置いてあります。

---

## 何をどう作ったか

1. `docs/guide/SKILL.md` の「使う手順」どおりに、先にガイド（`writing-html-for-shashoku.md`）を読む
2. `docs/guide/examples/` の定石（`table.html` / `flow.html` / `og-card.html`）を骨格として写す
3. release ビルドの CLI で描き、`--diagnostics json` で `errors` / `warnings` が空であることを確認
4. PNG を開いて目で見る

描画コマンドは各 `.html` の先頭コメントにも書いてあります。3 枚とも
**`exit 0`・警告 0**（`--strict` でも `exit 0`）で、下のコマンドを再実行すると
ここに置いてある `.png` と**バイト単位で一致**します。

```bash
S=build/release/tools/shashoku/shashoku
FONTS="--font NotoSansJP-Regular.otf --font NotoSansJP-Bold.otf"

$S docs/benchmark/2026-09-25-skill/compare.html  -o compare.png  --width 960              $FONTS
$S docs/benchmark/2026-09-25-skill/pipeline.html -o pipeline.png --width 1200             $FONTS
$S docs/benchmark/2026-09-25-skill/social.html   -o social.png   --width 1280 --height 640 $FONTS
```

（フォントは `build/<preset>/test_assets/fonts/` の Noto Sans JP Regular / Bold。
**2 つ渡さないと同じバイト列になりません**: 見出しとセルに `font-weight: bold` があります）

## 3 枚

| ファイル | 何 | 寸法 | 直しの往復 |
|---|---|---|---|
| [compare.html](compare.html) → [compare.png](compare.png) | サーバー内 HTML→PNG の 3 択の比較表 | 960×673 | **1 回** |
| [pipeline.html](pipeline.html) → [pipeline.png](pipeline.png) | 6 段パイプラインの図 | 1200×287 | **1 回** |
| [social.html](social.html) → [social.png](social.png) | SNS / OG 向けカード | 1280×640 | **0 回** |

**比較表の数値は新しく測ったものではありません。** `docs/benchmark/results_2026-09-23.md` の計測値
（Satori 0.33.5 / resvg-js 2.6.2、i9-14900KF）をそのまま引いています。画像の脚注にも出典を入れてあります。

## 困ったこと 3 つ

### 1. 子孫セレクタが書けない（比較表。往復 1 回の原因）

見出し行の 1 列目だけ色を変えようとして `.th .key { color: #ffffff }` と書き、

```
error[css-parse] at N:M: descendant combinators (a space between selectors) are not supported;
only `tag`, `.class`, `#id`, their combinations and `,` are
```

で止まった。**当てたい要素にクラスを直接書いて**（`<div class="td key hkey">`）解決。
ガイド §2.2 には書いてあるが、配布用の `SKILL.md` には無かったので **`SKILL.md` に注意を足した**（2026-09-25）。

### 2. 図のラベルが語の途中で折れる（パイプライン図。往復 1 回の原因）

等分（`flex: 1 1 0`）の 6 列を `--width 1100` で描くと、段の名前「スタイル付きツリー」が
`スタイル付きツ` / `リー` と折れた。和文は語の途中でも折れるので **`&nbsp;` では止められない**。
`white-space: nowrap` は対応外。**幅を 1200 に広げ、名前を短くして**回避した。

→ これを **`white-space: nowrap` の依頼**として `../requests.md` 第 2 節（`real01`）に足し、
`pipeline.html` を受け入れケースにした。`--width 1100` で描くと今も折れます。

### 3. `&nbsp;` の説明がガイドで「手段はありません」になっていた

比較表の「常駐 17 ms」が `常駐 17` / `ms` と割れたので回避策を探したところ、ガイド §5 が
「数字 + 助数詞が行末で割れるのを防ぐ手段はありません」と書いていた。**実際には `&nbsp;`（U+00A0）が効く**
（`17&nbsp;ms` / `2027&nbsp;年`）。`--dump-stage box` で確かめた行の分かれ方:

| 箱の幅 | そのまま | `2027&nbsp;年度` |
|---|---|---|
| 110px | `法改正は 2027` / `年度を視野に` / `入れる。` | `法改正は` / `2027 年度を視` / `野に入れる。` |
| 120px | `法改正は 2027` / `年度を視野に入` / `れる。` | `法改正は` / `2027 年度を視野` / `に入れる。` |
| 130px | `法改正は 2027 年` / `度を視野に入れ` / `る。` | **同じ**（`年` / `度` と助数詞の中で折れる） |

つまり `&nbsp;` が守るのは**その空白 1 か所だけ**で、語全体は守れない。
ガイド §5（日英）を実測に基づく説明に**訂正した**（2026-09-25）。`SKILL.md` にも同じ注意を足した。
