# 再計測（a52 / remeasure2）: 普段どおりの AI 生成 HTML 10 件を診断だけで直す

- 使ったもの: `R/bin/shashoku`（既定フォント埋め込み）、入力は `R/inputs/caseNN.html`、想定サイズは `R/inputs/NOTES.md`
- 規則: 前回と同じ（外枠を外す / 未対応タグは div・span / 未対応プロパティは hint に従い、無ければ削る / 未対応の値は最も近い対応値 / 未対応セレクタはクラス / 擬似要素の飾りは実要素 / 絵文字は消さない）
- ガイド（`R/guide/writing-html-for-shashoku.md`）は**一度も開いていない**。hint が指す節番号（§3-(4), §4.2, §4.3, §5）は hint 本文だけで意味が取れた
- 拒否された操作: なし

## 表

| case | 初回 errors | exit 0 まで | 絵が正しくなるまで | usable | fidelity | box_sizing の引き算 | undiagnosed |
|---|---|---|---|---|---|---|---|
| case01 比較表 | 70 | 1 | 1 | 5 | 4 | 0 | なし |
| case02 フロー図 | 30 | 1 | 2 | 4 | 3 | 0 | 矢印が上端に張り付く（align-self を落とした結果） |
| case03 用語カード | 28 | 1 | 1 | 5 | 3 | 0 | なし（角丸の欠けは overflow の記録に） |
| case04 手順カード | 54 | 2 | 2 | 5 | 4 | 0 | なし（2 往復目は診断あり） |
| case05 読書メモ | 31 | 1 | 1 | 5 | 3 | 0 | なし |
| case06 OG 画像 | 41 | 2 | 2 | 5 | 4 | 0 | なし（2 往復目は image-not-found） |
| case07 イベント告知 | 44 | 1 | 1 | 5 | 3 | 0 | なし |
| case08 週次レポート | 29 | 2 | 2 | 5 | 3 | 0 | なし（2 往復目は診断あり） |
| case09 ニュース要約 | 28 | 1 | 1 | 5 | 4 | 0 | なし |
| case10 短歌（縦書き） | 34 | 1 | 2 | 5 | 3 | 0 | 行末の全角スペースが詰まらず折り返しが 1 文字ずれる |

- 初回 errors 合計 389（最小 28 / 最大 70）。truncated はどの case も false（全件が一度に出た）
- exit 0 までの往復: **中央値 1、最大 2**（10 件中 7 件が 1 往復）
- 絵が正しくなるまで: **中央値 1.5、最大 2**（5 件が 1 往復、5 件が 2 往復）
- 10 件すべて `final: pass`。`gave_up` なし
- usable 平均 4.9 / fidelity 平均 3.4
- `--strict` が通ったのは 3 件（case01 / case06 / case09）。残り 7 件は missing-glyph 8 件・font-not-found 5 件が警告として残る

## box-sizing

- **引き算は 0 か所**（前回は 26 か所）。幅・高さから padding や border を引いた箇所は 1 つもない
- `* { box-sizing: border-box }` は 10 件すべてにあり、**10 件すべてそのまま通った**（診断は一度も出ていない）

## 2 往復目以降に出た診断（何が隠れていたか）

3 件で 2 往復目に新しいエラーが出た。いずれも「1 往復目で外側を直すまで見えなかった」もの。

- **case04（3 件）/ case08（2 件）: インライン要素に箱のプロパティ**（`unsupported-layout`）。
  r0 では `code { … }` が「`code` はタグとして未対応なのでこの規則は当たらない」、`.metric .unit { … }` が「子孫セレクタは未対応」としか言われず、**規則の中身は評価されていない**。
  セレクタを直した瞬間に、中の `border` / `padding` / `margin-left` が新しく 5 件出た。
- **case06（1 件）: image-not-found**。`<img src="avatar.png">` は r0 では一切触れられず、CSS が通って初めて出た。

つまり「一度に全部」は**同じ層の中では**守られているが、層（セレクタが当たる → 宣言が評価される → レイアウトが走る → 画像が解決される）をまたぐと繰り越される。
2 往復目の修正量はごく小さい（edit_lines 2〜3 行）ので実害は小さいが、「エラー 0 になるまであと 1 回」は構造的に起きる。

## 診断に出ない壊れ方（case ごと）

1. **case02**: `align-self: center` を落とすと、矢印（height:2px の div）がカード列の**上端に張り付く**。`align-self` は「未対応」と言われるだけで hint がなく、結果（上下中央が崩れる）も出ない。矢印を flex コンテナにして `justify-content: center` で戻した（1 往復）。
2. **case10**: 行末の全角スペース（U+3000）が行末で詰められず、1 列目が「立ちつく」で折れて **「し」1 文字が 2 列目の頭に孤立**した。Chrome は「立ちつくし」まで入る。警告もエラーも出ない。NOTES が許している範囲で `height: 880px → 900px` にして戻した（1 往復）。
3. **case03 / case08**: `overflow: hidden` を落とした結果の**角丸の欠け**（下記）。これも診断は出ない。
4. **case01**: `<table>` を div にすると**列が縦に潰れる**。診断は「table は未対応」とは言うが、置き換え方（`tr` を `display:flex`、セルに `flex:1`）は教えてくれない。今回は 1 往復目でまとめて直したので往復は増えていないが、知らなければ 1 往復増える性質のもの。
5. **case04**: `li::before` の縦ガイド線を実要素にすると、li の padding 分（約 45px）だけ**線が途切れる**。position がないので連続線にはできない。

## hint の評価

**効いた（そのとおりに直せた）**

- `display: grid` → 「1 行の等幅は flex＋`flex: 1 1 0`」（case08）。gap:1px を罫線に使う書き方もそのまま通り、1 往復で済んだ。今回いちばん効いた hint
- `display: inline-block` → 「flex 親＋`flex: none` の子」（case03 / case08）。pill・バッジの直し方として具体的
- inline 要素に箱のプロパティ → 「落とす。標準的な部品なら flex 項目に」（case03 / case04 / case08）。`.tag` は flex 親の中なので宣言を残せる、`.code` は文中なので落とす、という判断がそのまま付いた
- `background-clip: text` → 「`color: transparent` も一緒に落とせ、でないと文字が消える」（case06）。**結果（文字が消える）まで言っている唯一の hint**で、実際にこれが無ければ見出しを消していた
- タグ名セレクタ（`body` / `header` / `ol` / `li` / `code` / `table` / `footer`）→ 「外側の div にクラスを付けて移すか、規則を削る」。10 件すべてで使い、**全件これで直した**。指示が一意なので迷わない
- `min-height` → 「親が stretch の flex なら落とす」（case02 / case05 / case06 / case10）
- `-webkit-writing-mode` → 「prefix を外せ」（case10）
- `writing-mode` の「最上位の要素だけが指定できる」（case10）。直し方（外側 div に移す）が読み取れた

**誤解を招いた**

- なし。今回 `hints_misleading` はゼロ。ただし次の「足りない」に挙げるとおり、**辺ごとの border の hint は二択のうち「削る」に倒すと必ず見た目を失う**ので、判断を利用者に丸投げしている感はある

**無くて困った（hint が空、または選択肢が無い）**

- `align-self`（case02）: hint なし。落とすと中央揃えが崩れることも、`justify-content` を持つラッパを足せばよいことも書かれていない。**silent な崩れの直接の原因**
- `transform`（case02 の矢印三角 / case09 の菱形）: hint なし。border トリックの三角は border-left/top/bottom の hint（「枠の一辺なら削れ」）に吸われて消える。「三角・回転は作れない」と一言あると諦めが早い
- `table` / `tr` / `td`（case01）: 「div にせよ」とも書かれていない（hint は空）。表を保つには `display:flex` + `flex:1` が要る
- `image-not-found`（case06）: 「`avatar.png` という名前の画像は渡されていない」とは言うが、**渡されている名前（`avatar`）を教えてくれない**。hint も空。`--image` で渡した名前を列挙すれば 1 往復減る
- `box-shadow` / `opacity` / `text-shadow` / `object-fit` / `vertical-align` / `list-style` / `counter-reset` / `pointer-events`: hint なし（削るだけなので実害は小さい）
- `border-radius` の複数値（case03）: 「単一長さのみ」とは言うが、**overflow:hidden を落とした後に子の角を丸める手段が無い**ことには触れない（下記）

### タグ名セレクタの診断は役に立ったか

**はっきり役に立った。** 10 件すべてに出て（`body` 10 件、ほかに `html` `header` `footer` `ol` `li` `code` `table`）、直し方が一意（外側 div にクラスを付けて移す）。
これが無ければ「規則を書いたのに効かない」という**一番気づきにくい壊れ方**を黙って通すことになる。
副作用として、規則が当たるようになった途端に中身の未対応が新しく出る（case04 / case08、上記）。

### font-not-found の警告は役に立ったか

**半分。** 5 件で出て（明朝 4 件・等幅 1 件）、「なぜゴシックになったか」は即座に分かる。位置も代替先（`Noto Sans JP`）も具体的。
ただし**打つ手がない**：埋め込みフォントしか無い状況では HTML 側で直せず、規則上も警告は直さない。結果として case03 / case05 / case07 / case10 の fidelity を 3 に押し下げた（明朝／ゴシックの混植、短歌、用語カードはいずれも書体が設計の中心）。
`--strict` を使う運用では、この警告があるだけで 7 件中 5 件が落ちる。「フォントを追加すれば消える」と分かる形（`--font` の示唆）があると親切。

## 辺ごとの border の代替は用途を満たせたか

| 用途 | 件数 | どうしたか | 満たせたか |
|---|---|---|---|
| 箱と箱の区切り線（見出し下・フッタ上・行間・列間） | 13 か所（case01×4 種, 02, 04×2, 06, 07, 08×2, 09×2） | 1px / 2px の div を挿入 | **満たせた**。レイアウトも 1〜2px ずれるだけ |
| 枠の一辺＝色のアクセント帯 | 3 か所（case02 `.step` 上 4px、case03 `.example` 左 4px、case07 `.sp` 左 4px） | hint の指示どおり削除 | **満たせない**。3 件とも「段・引用・登壇者カードを色で識別する」という設計意図がそのまま消えた |

- 区切り線の代替は素直。ただし **case06 のように親が `justify-content: space-between` の flex だと、div を 1 枚足すと項目数が変わって配置が壊れる**。ラッパ div をもう 1 枚足して回避した（hint はこの副作用に触れていない）
- case01 の列間の縦罫は、`tr` を flex 行にしたので hint の「行の中なら `width: 1px`」がそのまま使えた
- アクセント帯の 3 件は、今回の fidelity 低下のうち見た目への影響が最も大きい部類

## overflow: hidden を落として角が崩れた件数

**2 件（case03 / case08）。**

- case03: カード上端 6px のグラデーション帯（実 div 化したもの）が、`border-radius: 14px` の角の外に四角く残る
- case08: 色付きの見出しブロックが `border-radius: 16px` の上 2 つの角を覆い、**カードが角丸に見えなくなった**（影響が大きい）

どちらも `border-radius` が単一長さしか取れない（`16px 16px 0 0` は `unsupported-value`）ため、**「上だけ丸める」代替が無く直せない**。
残り 8 件のうち overflow:hidden を使っていたのは case05 / case06 / case07 / case10 で、いずれも角丸の子要素が無いので無害だった。

## 目視した PNG

- `R/out/fix/case01.png`（参照 `R/chrome_ref/case01.png` と比較）
- `R/out/fix/case02_r1.png`, `R/out/fix/case02.png`
- `R/out/fix/case03.png`（＋ 角の拡大 `R/out/fix/_crop.png`）
- `R/out/fix/case04.png`
- `R/out/fix/case05.png`
- `R/out/fix/case06.png`
- `R/out/fix/case07.png`（参照 `R/chrome_ref/case07.png` と比較）
- `R/out/fix/case08.png`（＋ 角の拡大 `R/out/fix/_crop08.png`）
- `R/out/fix/case09.png`
- `R/out/fix/case10_r1.png`, `R/out/fix/case10.png`（参照 `R/chrome_ref/case10.png` と比較）

## 前回との比較で目立つ点（規則は変えていない）

- **box-sizing の引き算が 26 → 0**。`* { box-sizing: border-box }` が 10 件すべてそのまま通り、幅・高さを手で調整する必要が消えた
- exit 0 までが中央値 1 往復。残る 2 往復の 3 件は「層をまたいで繰り越される診断」（inline の箱プロパティ 2 件、画像 1 件）
- 絵の崩れで往復を使ったのは 2 件だけ（case02 の矢印、case10 の折り返し）
