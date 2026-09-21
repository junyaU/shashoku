# shashoku 実装設計書

[DESIGN.md](DESIGN.md) が「何を・なぜ作るか」、この文書が「どう作るか」。
DESIGN.md のスケッチを実装可能な粒度まで具体化し、その過程で下した設計判断を記録する。
**両者が食い違ったら、§1 の判断記録に理由が書いてある限りこの文書が優先**（書いていなければバグなので直す）。

モジュール間の契約は文章ではなくヘッダで固定してある。まずそれを読むこと:

| 契約ヘッダ | 結ぶもの |
|---|---|
| [include/shashoku/error.hpp](../include/shashoku/error.hpp), [src/core/result.hpp](../src/core/result.hpp) | 全モジュール共通のエラー型 |
| [src/core/](../src/core/) の各ヘッダ | 基本型（Rect / Color / Bitmap / ID）、UTF-8、JSON ダンプ |
| [src/html/dom.hpp](../src/html/dom.hpp) | ① html → ② style |
| [src/style/computed_style.hpp](../src/style/computed_style.hpp) | ② style → ③ layout |
| [src/text/text_measurer.hpp](../src/text/text_measurer.hpp) | ③ layout ⇄ ④ text |
| [src/linebreak/line_breaker.hpp](../src/linebreak/line_breaker.hpp) | ③ layout → 行分割器 |
| [src/raster/display_list.hpp](../src/raster/display_list.hpp) | ⑤a paint → ⑤b raster |
| [src/raster/glyph_source.hpp](../src/raster/glyph_source.hpp) | ⑤b raster ⇄ ④ text |

契約ヘッダは勝手に変えない。実装してみて契約が間違っている・足りないと分かったら、
最小限の変更にとどめ、**何をなぜ変えたかを必ず報告する**（他のモジュールが同じ契約に依存している）。

---

## 1. 設計判断の記録

DESIGN.md のスケッチから変えた点・決めた点。番号は議論で参照するためのもの。

**A1. レイアウトは論理座標（inline / block）で行い、物理座標への変換は paint で 1 回だけ行う。**
縦書き（Phase 8）を「主軸の転置」で済ませるため。`writing-mode` は文書全体で 1 つ。
指定できるのはトップレベル要素（合成ルート `#root` の直接の子）だけで、`#root` はその値を採用する
（トップレベル要素どうしで値が食い違う、またはそれより深い要素で親と違う値を指定したら
`UnsupportedLayout` エラー。直交フローは扱わない）。だから
ボックスツリー全体が 1 つの論理座標系に乗り、変換は大域的な 1 回で済む。
横書きでは inline = x、block = y なので、横書きのダンプは物理座標と同じ値で読める。
CSS の物理プロパティ（width / margin-top …）は layout の入口で論理方向に読み替える。

**A2. 行分割の単位は「文字」ではなくクラスタ。** 結合文字・異体字セレクタ・絵文字の途中で
割らないため。さらに行分割器の入力は `Item`（クラスタ or 分割不能なインライン要素）とし、
画像とルビのまとまりを同じ列に流す。詳細は line_breaker.hpp の冒頭コメント。

**A3. 行分割器の出力は行の範囲 + アイテムごとの字間調整（Spacing）。** 追い込み・約物連続の
アキ詰め・行末約物の半角化はすべて「約物の前後の空きを削る」操作で、範囲だけでは表せない。

**A4. 禁則 > 幅。** 禁則を守ると行に収まらないときは、割らずにはみ出す（`Line::overflows`）。
「行頭に句読点が絶対に出ない」（DESIGN.md Phase 4 の受け入れ条件）を文字どおり守る。

**A5. `%` と `auto` はスタイル段で解決しない。** 包含ブロックの大きさが要るので layout まで
`Dimension` として残す（CSS の計算値と使用値の区別）。

**A6. シェーピングは段落（インライン整形文脈）全体で 1 回。** 行ごとに再シェーピングしない。
行分割はクラスタ境界でしか起きず、和文には行をまたぐカーニングがなく、欧文は空白で割れるため、
結果はほぼ変わらない。代わりに「計測 → 分割 → 配置」が片道になり、相互再帰が消える。
**「1 回」はレイアウト 1 回の中で 1 回**（当初は「1 つの IFC を組む中で 1 回」の意味だった）。
flex の固有寸法計測がその外側から同じ段落を何度も組み直していたので、シェーピングまで済ませた
`PreparedParagraph` を `LayoutEngine` が持ち回り、計測と配置で共有する（A29）。

**A7. HarfBuzz と FreeType は互いを知らない。** HarfBuzz は自前の OpenType 実装（hb-ot）で
メトリクスを読む（hb-ft を使わない）。FreeType はグリフのラスタライズだけに使う。
依存ライブラリ同士の結合を避け、ビルド（FetchContent）を単純に保つため。

**A8. グリフ位置はデバイスピクセルの整数に丸める。** サブピクセル位置のグリフ描画はしない。
決定性（DESIGN.md §3-5）と実装の単純さを優先。ヒンティングは無効（`FT_LOAD_NO_HINTING`）。

**A9. 浮動小数点の決定性。** `-ffp-contract=off`（設定済み）に加え、layout / raster では
`+ - * /`、`sqrt`、`floor / ceil / round / trunc`、`min / max / abs` だけを使う。
`pow / exp / log / sin / cos` などは libm の版で結果が変わりうるので使わない。

**棚卸し**（issue #10-4a。`src/` 全体を grep した結果。違反なし）:

| 場所 | 使っているもの | 決定的か |
|---|---|---|
| layout | `+ - * /`、`std::min / max`、`std::abs`（`flex_layout.cpp` の 1 か所）だけ。**丸めの関数を 1 つも使っていない** | ○ |
| raster | `std::sqrt`（角丸の輪郭）、`std::round`（グリフ原点 A8・被覆率 → 0..255）、`std::floor / ceil`（画素範囲）、`std::fabs`、`std::isfinite` | ○（A9 の許可リスト内。`isfinite` は分類述語で誤差を持たない） |
| paint | 算術のみ。`std::isfinite`（`dump_svg.cpp`）だけ | ○ |
| text | `std::lround`（`shaper.cpp` の px → 26.6 固定小数点、`freetype_glyph_source.cpp` の pixel_size → 26.6） | ○（`lround` は「絶対値の大きい方へ丸める」と仕様で決まっており、近似ではない） |
| core / style | `std::to_chars`（JSON の float。**ロケールに依らず、最短往復表現は標準が一意に定める**）、`std::lround`（`css_color.cpp` の `rgb(50%)`） | ○ |
| float → 整数のキャスト | すべて `round / floor / ceil / lround` を通したあと。切り捨て任せの暗黙変換は出力の経路にない | ○ |

`unordered_map` / `unordered_set` は `src/` と `include/` に 1 つもない（反復順が出力に出る余地がない）。
豆腐の重複除去は `std::set<MissingGlyph>`（A31）で、順序は値で決まる。

**A10. マージンの相殺は「隣り合う兄弟ブロック間」だけ実装する。** 親子間の相殺はしない。
flex コンテナの中では一切相殺しない。ブラウザとのピクセル一致は目標ではない（DESIGN.md §4）。

**A11. 枠線と角丸は 4 辺・4 隅共通のみ。** `border-top` や隅ごとの半径は `UnsupportedProperty`。
`box-sizing` は content-box のみ（プロパティ自体が対応外）。

**A12. 画像は名前で参照する。** `render()` に `ImageSet`（名前 → PNG バイト列）を渡し、
`<img src="名前">` で引く。ネットワークにもファイルシステムにも触れない。対応形式は PNG のみ。

**A13. `text-align: justify` に対応する。** 追い出しで生じた行末の空きを字間に配分するのは
日本語組版の基本動作で、「日本語の文章を正しく組むことに寄与するか？」に Yes。

**A14. ソース中の改行の扱い。** 空白の畳み込みで、改行の前後がどちらも全角文字
（East Asian Width が W / F / H のうち W・F）なら改行を消す。それ以外は空白 1 個にする
（CSS Text 3 §4.1.3 segment break transformation）。HTML を整形して書いても和文に空白が入らない。

**A15. 未知の font-family は読み飛ばす。** `font-family` はもともとフォールバック列なので、
FontSet にない名前を飛ばすのは「黙って崩す」に当たらない。列を使い切ったら FontSet の追加順で探す。


**A16. 行末の約物の空きは「収まらないときだけ」詰める。** `text-align: right` や justify の行末に
句読点が来ると、句読点の後ろ半分の空き（0.5em）のぶん、字面が版面の端より内側に見える。
これは CSS の `text-spacing-trim: normal`（allow-end）と同じ挙動で、ブラウザでも同じ絵になるので
既定はこのままにする。常に詰めたい（行末約物半角）需要が出たら `linebreak::Config` にオプションを足す。

**A17. `line-break: auto` は「エンジンの既定に従う」。** CSS では auto の中身は実装依存なので、
`RenderOptions::line_break.strictness`（既定は Strict）を使う。CSS で strict / normal / loose が
明示されたらそちらが優先。インライン要素で明示された値は、その範囲のアイテムにだけ効く（A23 / A28）。
**auto の解決先は「段落のブロックの値」ではなく「エンジンの既定」。** 段落が `strict` でも、
その中の `<span style="line-break: auto">` はエンジンの既定に戻る（CSS の auto の意味どおり）。
`line-break` は継承プロパティなので、ふつうは段落の値がそのまま降りてきて差は出ない。

**A18. `<img>` は交差軸の stretch で歪めない。** flex アイテムの `<img>` は `align-items: stretch` でも
縦横比を保つ（CSS では歪むが、OG 画像でアイコンが潰れるのは誰も望まない）。主軸方向の grow / shrink は
CSS どおりに効く。`box-sizing` は content-box のみ（A11）なので、1200×630 の箱に padding 80px を
入れるなら `width: 1040px; height: 470px` と書く。

**A19. `GlyphSource::rasterize()` は `Result<GlyphBitmap>` を返し、「成功して空のビットマップ」は
空白グリフだけを意味する。** 値返しの契約では、不正な `FontId`・`FT_Load_Glyph` の失敗・輪郭を
持たないグリフ・未対応の pixel_mode がすべて空のビットマップになり、ラスタライザがそれを空白として
読み飛ばしていた。結果、**文字の欠けた PNG が「成功」として返りうる**（issue #3。DESIGN.md §3-6 違反）。
分類:

| 事象 | 返すもの |
|---|---|
| 空白グリフ（輪郭はあるが塗る面積が 0）、`pixel_size` が 1/64 px 未満 | 成功（空のビットマップ） |
| 不正な `FontId`、非有限・非正の `pixel_size` | `Internal`（呼び出し側のバグ） |
| `FT_Set_Char_Size` / `FT_Load_Glyph` / `FT_Render_Glyph` の失敗、輪郭を持たないグリフ、未対応の pixel_mode、26.6 に収まらない `pixel_size` | `FontLoad` |

`pixel_size` が有限かつ 0 より大きいことは**呼び出し側の責務**にした。ラスタライザはもともと
非有限・非正の寸法を持つコマンドを読み飛ばす方針（§3.3）で、グリフだけ別扱いにする理由がないため。
エラーのメッセージには `FontId` / `glyph_id` / `pixel_size` / FreeType のエラーコードを必ず入れる。

フォント全体が輪郭を持たない場合（CBDT/CBLC・sbix のカラー絵文字フォント。FreeType は
`FT_FACE_FLAG_SCALABLE` を立てない）は、`FontStore::load()` が `FontLoad` で落とす（一番早い段で
落とす。実装済み）。COLR/CPAL のカラー絵文字はベースの輪郭を持つので load は通り、ベースグリフが
空なら「空白」として通ってしまう。これを警告 + 豆腐に回すかは Shaper 側の判断（未着手）。

**A20. Unicode の表は UCD から生成し、生成物をコミットする。** 行分割クラス（UAX #14）・
縦書きの字の向き（UAX #50）・東アジア幅（UAX #11）の 3 つの表は
[scripts/gen_unicode_tables.py](../scripts/gen_unicode_tables.py) が、**版と SHA256 を固定した**
UCD から生成する。生成物（`src/*/[a-z_]*_table.inc`）はコミットするので、ビルドに Python も
ネットワークも要らない（製品コードとテストは実行時に UCD を読まない。DESIGN.md §3-5）。
生成物の先頭に生成元の版・ハッシュ・スクリプト名が入るので、3 つが同じ版から作られていることは
ファイルを見れば分かる。`--check` で「再生成した結果がコミット済みの表と一致するか」を検査できる。
手で足した例外（クラスの寄せ先、既定値のブロック）は結果の範囲ではなく**規則**としてスクリプトに書く。
更新手順と、`layout` の全角表をいまだけ Unicode 15.1 相当で据え置いている理由は
[docs/UNICODE_TABLES.md](UNICODE_TABLES.md)。

**A21. 計算量の回帰は「時間」ではなく「回数」で測る。** layout に計測カウンタ
（[src/layout/counters.hpp](../src/layout/counters.hpp)）を置き、`layout()` の最後の引数
（`Counters*`、既定は nullptr）で受け取る。数えるのは layout_block / content_intrinsic /
インライン整形文脈の準備 / `shape()` の回数と文字数 / `metrics()` / 行ボックス数 /
行の構築の作業バッファ要素数 / 背景スコープの走査回数 / 文字ごとの属性の表（A27）を引くのに
行ったスタイルの比較の回数。行分割器の中の作業量は `linebreak::Counters`（A24）を
`layout::Counters` が 1 つ抱えて、`break_lines()` などに渡して足し込む。約束は 3 つ:
**出力に影響させない**（カウンタの値を読んで分岐しない）、**グローバル状態にしない**
（DESIGN.md §3-5。LayoutEngine が参照を持つ）、**公開 API に出さない**。
数え漏れが起きないよう、`TextMeasurer` の呼び出しは `LayoutEngine::shape()` / `metrics()` に通す。
時間で測らないのは、環境でぶれるうえ「遅いが通る」状態を見逃すため。
数えるのは**実際に行った仕事**で、呼び出し回数ではない（A29 のメモが効いた分は増えない）。
メモが効いているかどうかは、これらが入れ子の深さでどう増えるかで見る。

**A22. 行の構築で使う作業バッファは、行ごとではなく段落で 1 回だけ確保する。** 行を 1 本組むたびに
段落全体ぶん（アイテム数 N、スタイル数）の配列を作ると、狭い版面では行数 L が N に比例するので
O(N×L) になる（issue #4）。`InlineFormatter` は placement をメンバとして使い回し
（書いた `[line.begin, line.content_end)` しか読まないので、行をまたいで残る値は害にならない）、
「この行でもう見たスタイル」は世代印で持つ。インライン背景も、行ごとに全スコープを舐めるのをやめ、
スコープが文字位置 `begin` の昇順に並ぶこと（外側の span から順に作られる）を使って、
行が進むのに合わせて「その行と交差するスコープ」だけを保つ。行内の範囲は、アイテムの `char_begin` が
狭義単調増加なのを使って二分探索で出す。

**A23. 行分割ポリシー（`line-break` / `overflow-wrap`）はアイテムごとに持つ。** どちらも CSS では
テキスト（インラインボックス）に適用される継承プロパティなので、段落の途中の `<span>` で値が
変わりうる。`linebreak::Config` を 1 組だけ持つ形では、その指定が段の境界（layout → linebreak）で
黙って落ちていた（issue #2）。`linebreak::Item` に `std::optional<Strictness> strictness` と
`std::optional<bool> break_anywhere` を持たせ、nullopt なら `Config` の値を使う
（すべて nullopt なら出力は従来と完全に同じ）。
CSS Text Level 3 の「Line Breaking Details」（2026-09 時点の TR では §5.5。本書の他の引用が使っている
版とは節番号がずれている）は *「which elements' line-break, word-break, and overflow-wrap properties
control the determination of soft wrap opportunities at such boundaries is undefined in this level」*
としているので、境界は仕様に反しない範囲でこちらで決める:

- **`strictness` は「分割クラスの解決」にだけ使い、アイテム自身の値で解決する。** CJ（小書きの仮名・
  長音）を NS とみなすか ID とみなすか、loose の追加規則（‥ … 々 ・ ！ ％ ￥ など）で ID に
  格下げするかは、その文字 1 個の性質なので、境界の曖昧さが生じない。ペア表・文脈規則（UAX #14）は
  解決済みのクラスに対して従来どおり働く。例外は loose の「直前が ID ならハイフン ‐ – の前で
  割ってよい」だけで、これは 2 アイテムにまたがる。**行頭に来る側（= 後ろのアイテム = ハイフン
  自身）の値で決める**
- **`break_anywhere` の緊急分割は、位置の両側のアイテムがともに true のときだけ許す。**
  つまり anywhere を指定した要素の内部でだけ割れ、要素の境界では割れない。指定していない語が
  隣接のせいで割れるより、指定した範囲だけが割れる方が説明しやすい。発動条件（分割可能位置が
  1 つもない行でだけ。§3.4 (5)）と優先順（分離禁則 > 行頭・行末禁則、クラスタ内部では割らない）は
  `Config` のときと同じ。両側が true の位置が 1 つもなければ、A4「禁則 > 幅」で割らずにはみ出す
- `break_anywhere` は `min_content_width()` に影響しない（`Config` のときからの挙動。CSS の
  `break-word` 相当）。CSS Text 3 §5.4 は `anywhere` を min-content に効かせると定めているが、
  現行の `Config::break_anywhere` は `anywhere` と `break-word` を 1 つのフラグにまとめているので
  区別しない。区別が要るようになったら `Config` の意味論の問題として別に決める

layout 側がこの口に何を入れるかは A28。`Config` は**段落の既定値**として残っていて、値を持たない
`Item` にだけ効く（約物のアキ・あふれ処理など、`strictness` / `break_anywhere` 以外の設定は
`Config` にしかない）。

**A24. 行分割の仕事は N に線形。「行ごとに段落の残りを舐める」を作らない。** 狭い版面では
行数 L がアイテム数 N に比例するので、1 行あたり N の走査をすると O(N×L) になる（issue #4。
`<br>` のない 40,000 字を幅 1em に流すと 0.21 秒、うち大半が「次の強制改行を毎行探す」だった）。
守り方は 2 つ:

- **前方に探すものは `break_lines()` が持ち回る。** `begin` は前にしか進まないので、
  「次の強制改行（LB4 / LB5）」と「次の分割可能位置」は、`begin` がその位置を追い越したときだけ
  探し直せばよい。走査は段落全体で合計 O(N) になり、配列を増やさずに済む。
  そのため `scan_candidates()` は「最初の候補」を自分で探さない（打ち切り条件から
  「まだ候補を見つけていない」という除外が消え、分割可能位置のない長い列でも毎行の走査が止まる）
- **後方に遡るものは 1 回の走査で表にする。** LB30a（地域表示記号が偶数個で 1 組）は
  「直前に並ぶ RI の個数」を見るので、位置ごとに遡ると国旗の絵文字が並んだだけで二乗になる。
  偶奇だけを 1 パスで `ri_odd_` に入れる。空白越し（LB8 / LB14 / LB16 / LB17）と数値の並び
  （LB25）の遡りは、遡る前に「直前が空白 / 区切りでない」で弾けるので合計 O(N) に収まっている

計測は [line_breaker.hpp](../src/linebreak/line_breaker.hpp) の `Counters` で行う（A21 と同じ約束。
linebreak は何にも依存しないので `layout::Counters` は使えず、自前に持つ。既定 nullptr なので
呼び出し側は書き換え不要）。`tests/linebreak/complexity_test.cpp` が N = 20,000 で
「作業量 ≤ 32N」を検査する。直す前の実測は 7,500N〜20,000N だった。

**A25. 入力の上限は `RenderLimits`（[include/shashoku/limits.hpp](../include/shashoku/limits.hpp)）に
集約し、3 か所で検査する。** それまでは上限が部品に散らばっていて（入れ子 256 / 画像 2^26 px /
出力 2^26 px / scale 256 / 入力 4 GiB）、**処理全体の予算を表す場所がなかった**。出力画像が
小さくても途中の段は入力に比例したメモリを使うので、最終段の画素数だけでは事故が止まらない
（`font-size: 30000px` の 1 文字を 100x100 に描くだけで 1.2 GB を確保していた。実測 1,197 MB。
issue #6）。上限は**入力の一部**なので純粋関数の性質は壊れない（DESIGN.md §3-5）: 同じ HTML と
同じ `RenderLimits` からは同じ PNG か同じエラーが出る。判定はすべてサイズ・個数で行い、
経過時間や実メモリ使用量では行わない（決定的であること）。

検査する場所は 3 つ:

| | 場所 | 見るもの |
|---|---|---|
| (a) | 入力を受けた時点（パースより前） | `html_bytes` / `images` |
| (b) | パース・計算値化のあと（api が DOM とスタイル付きツリーを 1 回ずつ辿る） | `nesting_depth` / `dom_nodes` / `text_code_points` / `style_rules` / `font_size_device_px` / `scale` |
| (c) | 大きな確保の直前（確保する前に判定する） | `image_pixels` / `total_image_pixels` / `device_pixels` |

既定値と根拠（「OG 画像 1200x630 @2x・数千文字・画像数枚には十分広く、事故は止まる」）:

| フィールド | 既定 | 根拠 |
|---|---|---|
| `html_bytes` | 4 MiB | 和文 100 万字を超える。OG 用の断片は数 KB |
| `images` | 64 枚 | OG 画像で使うのはアイコン・ロゴ数枚。64 枚でも合計画素数が先に効く |
| `nesting_depth` | 256 | 既存値の据え置き。`Node` のデストラクタが深さだけ再帰する |
| `dom_nodes` | 20,000 | OG カードは 100 未満。`style_rules` との積（4x10^7 回の照合）が 0.1 秒に収まる量 |
| `text_code_points` | 50,000 | 「数千文字」の 10 倍以上。行分割と配置が入力に比例して効く |
| `style_rules` | 2,000 | セレクタの照合は 規則数 x 要素数。`dom_nodes` との積で決めた |
| `font_size_device_px` | 2,048 | グリフのビットマップは pixel_size の 2 乗。2048^2 = 4 MB で頭打ちになる。見出しは @2x でも 300 px 程度。実行ごとのグリフキャッシュ（A34）1 項目の上界もこれで決まる |
| `scale` | 256 | 既存値の据え置き。「1.0 のつもりが 1000」を弾く |
| `image_pixels` | 2^24 | 4096x4096（RGBA で 64 MB）。OG に貼る素材には十分 |
| `total_image_pixels` | 2^25 | 128 MB。「1 枚 2^26 px x 枚数無制限」だったのを塞ぐ |
| `device_pixels` | 2^26 | 既存値の据え置き。1200x630 @2x の 22 倍 |

決めたこと:

- **「無制限」を表す特別な値は作らない。** `0` は文字どおり 0 で、事実上外したいときは型の最大値を
  入れる。「0 = 無制限」にすると、ゼロ初期化した構造体が最も危険な設定になってしまう
- 各モジュールは自分の既定値を定数で持つ（`html::kMaxNestingDepth` / `style::kMaxStyleRules` /
  `png::kMaxPixels` / `raster::kMaxDevicePixels`）が、**api は必ず `RenderLimits` の値を引数で渡す**
  （グローバル状態は使わない）。両者が食い違わないことは `src/api/render.cpp` の `static_assert` が
  機械的に検査する。モジュール側の定数は「api を通さず単体で使うとき」の既定に過ぎない
- **絶対上限**（実装の都合。`RenderLimits` では緩められない）として残すもの: HTML の 4 GiB
  （`SourceLocation::offset` が 32 bit。`html::kMaxSourceBytes`）、出力の 1 辺 2^32-1 px
  （`Bitmap::width` が `std::uint32_t`）、画素数 x 4 が `std::size_t` に収まること
- 超過は専用の `ErrorKind::LimitExceeded`。message には「どの上限を・いくつに対して・いくつだったか」と
  `RenderLimits` のどのフィールドで変えられるかを入れ、入力位置が分かるもの（font-size を指定した要素、
  深すぎる要素、上限を超えたノード）には location を付ける。HTML の 4 GiB 超過を `InvalidOption` で
  返していたのも `LimitExceeded` に直した
- `<style>` の中身は `text_code_points` に数えない（「組む対象のテキスト」ではないため）。その量は
  `html_bytes` と `style_rules` が押さえる
- **計測・シェーピングの回数の予算は入れていない。** レイアウトの計算量そのもの（#4 の O(NxL)、
  #5 の 2^depth）は別途直す。いまは `dom_nodes` x `style_rules` と `text_code_points` で間接的に抑えている

**A26. メモリ不足は、公開関数の境界でだけ例外を捕まえて `ErrorKind::OutOfMemory` にする。**
これは「例外を投げない・捕まえない」（§2 / CLAUDE.md コード規約）の**唯一の例外規定**。
`render()` / `dump()` の全オーバーロードが `std::bad_alloc` と `std::length_error` を捕まえて
`RenderError` に変換する。内部では従来どおり例外を使わず、失敗は `Result<T>` で返す
（`catch` は `src/api/render.cpp` の `catch_out_of_memory()` 1 か所だけ）。

理由: 「例外をライブラリ境界の外へ出さない」という契約を守るには、境界に `catch` が要る。
`terminate` に倒す案もあったが、OG 画像を生成するサーバがリクエスト 1 本で落ちるのは割に合わない。

**これは最善努力であり、保証は `RenderLimits` の側で行う。** Linux の既定のオーバーコミットでは
確保そのものが成功して、あとから OOM killer にプロセスごと殺されるので、`bad_alloc` が来ないことがある。
「メモリを使い切らないこと」を保証するのは A25 の上限の仕事で、`OutOfMemory` は最後の網に過ぎない。
依存ライブラリ（FreeType / HarfBuzz / zlib）の確保失敗は A19 の `Result` 化で拾う。
`noexcept` は公開関数のどれにも付けない（付けると変換する前に `terminate` する）。

**A27. 文字ごとの属性は層に分ける。シェーピングの境界と装飾の境界は別物。** かつては
`font-family / font-weight / font-size / color / letter-spacing / line-height` を 1 つの
`RunStyle` にまとめ、その一致で **シェーピングの区間と `TextFragment` の両方** を決めていた。
そのため**色だけを変える `<span>` を挟むとそこで `shape()` が切れ、カーニングと合字が消えていた**
（issue #8。`AVTo` の「o」が font-size の 1 割ずれる）。平坦化したインライン列の文字は
[src/layout/inline_style.hpp](../src/layout/inline_style.hpp) の `CharStyleTable` に層ごとの
添字で属性を持つ:

| 層 | 中身 | 効き方 |
|---|---|---|
| シェーピング属性 | `text::TextStyle`（font-family / font-weight / font-size / direction） | **この層が等しい連続ごとに `shape()` を 1 回**。メトリクスもこの層で引く |
| 装飾・行高属性 | color / letter-spacing / line-height | シェーピングの結果を変えない。クラスタ境界で対応付ける |
| 行分割ポリシー | line-break / overflow-wrap | `linebreak::Item` に写す（A28）。見た目には一切効かない |

**どの層を見るかは用途ごとに違う。** 層を足しても関係のない処理が細切れにならないよう、
用途ごとに見る層を決めてある（`shape()` の区間 = シェーピングだけ、`TextFragment` = シェーピング +
装飾、行の高さ = シェーピング + 装飾、`linebreak::Item` のポリシー = 行分割ポリシーだけ）。
たとえば `TextFragment` を層の組（3 つ全部）で切ると、`overflow-wrap` を変えただけの `<span>` で
`DrawGlyphs` が無意味に割れる。表は
[src/layout/inline_style.hpp](../src/layout/inline_style.hpp) の冒頭にある。

- **グリフの位置は 1 回のシェーピング結果で決まり、装飾の有無で動かない。** `TextFragment` は
  従来どおり「同じ FontId・サイズ・色・sideways の連続」で切る（= 1 回の shape 結果を、装飾の
  境界とフォールバックの境界で複数の断片に切る）。描き分けは変わらず、位置だけが正しくなる
- **1 つのクラスタ（合字・結合文字）が装飾の境界をまたぐ場合は、クラスタ先頭の文字の装飾を使う。**
  クラスタは行分割の最小単位（A2）で内部では割らないので、2 色で塗り分ける置き場所がない。
  先頭側を採るのは「その位置から始まる文字の指定が効く」と説明しやすいため
- **行分割ポリシー（`line-break` / `overflow-wrap`）をシェーピング属性に入れてはいけない。**
  入れるとその境界で `shape()` が切れ、#8 と同じ不具合を作る。A23 のとおりアイテムごとに
  持たせる値なので、独立した層にして `linebreak::Item` に写す（A28）
- 層の索引は内容で引く（同じ内容には同じ添字）。登録のたびに既存のスタイルを線形探索していると、
  色違いの `<span>` が S 個ある段落で O(S²) になる（issue #10）ので、順序つきの索引で O(log S) に
  する。**索引の順序と反復順は出力に使わない**（DESIGN.md §3-5）

インライン整形文脈の実装は、この層分けに合わせて段の境界で 4 つのファイルに分かれている
（`inline_style` / `inline_collect` = (a) / `inline_paragraph` = (b)(c) / `inline_layout` = (d)(e)）。
(b)(c) までの結果は行の幅に依らないので `PreparedParagraph`（準備済み段落）として取り出してある。

**A28. 行分割ポリシーは「アイテムの代表の文字」の計算値で決める。** A23 で `linebreak::Item` に
開いた口に、layout は必ず値を入れる（nullopt は残さない）。どの要素の値を使うかは
アイテムの種類ごとに:

| アイテム | 使う値 |
|---|---|
| テキスト（1 クラスタ） | **クラスタ先頭の文字**が属する要素の計算値（A27 の装飾と同じ規則） |
| `<img>`（Atomic） | その `<img>` 自身の計算値 |
| ルビ組（Atomic） | 親文字の**先頭の文字**が属する要素の計算値 |
| `<br>`（ForcedBreak） | その `<br>` 自身の計算値（必ず改行するので結果には効かない） |

- `line-break: auto` はエンジンの既定に解決する（A17）。`overflow-wrap` は `anywhere` /
  `break-word` のどちらも `break_anywhere = true`（A23 の最後）
- **ルビ組の内部（親文字の途中・`<rt>`）の指定は効かない。** 組は Atomic 1 個で、その内部には
  分割可能位置が存在しないため（§3.8 のルビ）。「指定を読み落としている」のではなく
  「効かせる場所がない」。1 文字だけの要素に `line-break` を書いても何も起きないのと同じ
- **`linebreak::Config` は段落の既定値として残す。** いまは (c) がすべての `Item` に値を入れるので
  `strictness` / `break_anywhere` については使われないが、約物のアキ・あふれ処理（`overflow` /
  `trim_line_end` / `collapse_punctuation_spacing`）は `Config` にしかない
- 継承プロパティのうち、layout が「インライン要素の値」ではなく「段落のブロックの値」だけを
  読んでよいのは `text-align`（CSS ではブロックコンテナに適用。インライン要素に書いても
  効かないのが仕様どおり）と `writing-mode`（A1。文書で 1 つ。食い違いは ② がエラーにする）の
  2 つだけ。ほかの継承プロパティ（color / font-* / line-height / letter-spacing /
  line-break / overflow-wrap）はすべて文字ごとの属性の表（A27）を通す

**A29. flex の「測ってから置く」は、計測だけをメモする。配置は毎回ふつうに実行する。**
flex は主軸・交差軸のサイズを決めるために部分木を先に測る。column のアイテムは高さを知るために
**部分木を一度まるごと組んで捨て、配置の段でもう一度組んでいた**ので、深さ d の入れ子で 2^d に
なっていた（issue #5。実測: depth 22 で 14.7 秒。`layout_block` は depth 16 で 131,072 = 2^17）。
row でも、固有寸法の計測（`content_intrinsic()`）と実配置が同じ段落を別々に準備していた。

置き場は [src/layout/layout_cache.hpp](../src/layout/layout_cache.hpp) の `LayoutCache`。
`LayoutEngine` が 1 つ所有し、**レイアウト 1 回のあいだだけ生きる**（グローバル・static は持たない）。
覚えるのは 3 つ:

| メモ | キー | 値 |
|---|---|---|
| 計測 | 部分木 + `BoxSizing` 全部 + 内容の inline 開始位置 | border-box の block サイズ（float 1 つ） |
| 固有寸法 | 部分木 + `%` の基準 | `Intrinsic`（min-content / max-content） |
| 準備済み段落 | ノードの並び（`children.data()` + 個数）+ ブロックのスタイル | `PreparedParagraph` |

決めたこと:

- **出力を 1 ビットも変えないために、メモするのは「計測」だけにした。** 「原点で組んだボックスツリーを
  あとから平行移動して配置に使い回す」案もあったが、浮動小数点の加算は結合的でないので座標の
  最下位ビットが変わりうる。配置のための `layout_block()` は従来どおり毎回実行し、
  `prepare_base_column()` が捨てていた「高さを知るためだけの組み直し」を `measure_block_size()` に
  置き換えた（返すのは箱ではなく数値 1 つ）
- **キーには結果に効く入力をすべて入れる。** 足りないと「黙って違う高さを使う」バグになる。
  部分木の同一性は `BlockInput` のうち幾何に効くもの（`style` / `children.data()` / 個数 /
  `anonymous` / `replaced`）で表す。`tag` と `location` は箱の名前とエラーメッセージにしか
  使われないので入れない。float は**ビット列で**比べる（近い値をまとめない = 同じ入力から同じ出力。
  NaN が来ても全順序が壊れない）
- **ポインタ値は検索キーにしか使わない。** 値も `std::map` の順序も出力には出さず、反復もしない
  （DESIGN.md §3-5）。スタイル付きツリーはレイアウト中に不変なので、ノードのポインタが部分木を
  一意に表す。ポインタの全順序は `std::less` で作る（生の `<` は無関係なポインタ同士では未規定）
- **`<img>` を含む段落は共有しない。** `%` の幅とマージンが `content_inline_size` を基準にするので、
  準備済み段落が行の幅に依ってしまう（それ以外の段落は幅に依らない。A6 / A27）。
  shrink-to-fit だと計測時の幅と配置時の幅が食い違うので、共有すると画像の大きさが変わる
- **メモが効いた場合と効かない場合で出力が同じであることをテストで固定する。** `layout_without_memo()`
  （engine.hpp。テスト専用の口で、`layout()` と同じ道を通り、メモだけを切る）を性質テストの
  不変条件 (6) に足してある。ランダムな木 300 本で `dump_json()` を突き合わせる
  （ダンプの float は「元の値に戻せる最短表現」なので、この比較はビット比較と同じ）
- **計算量は 2^d から d² になった**（線形ではない。計測の段の中でも配置は 1 回ずつ走るため）。
  実測（`--dump-stage box`、幅 600px、release）: depth 22 が 14.7 秒 → 0.010 秒、
  入れ子の上限に近い depth 250 でも 0.020 秒。`layout_block` は column の depth 16 で
  131,072 → 154。`shape_calls` は深さによらず**テキストノードの数**になる
- メモの大きさは実際に行った計測の回数（最悪 d²）と段落の数に比例する。入力の上限は A25 が押さえる

**A30. `TextMeasurer::shape()` / `metrics()` も `Result` を返す。「正常に 0 グリフ」と失敗を区別する。**
A19 で `GlyphSource::rasterize()` を `Result` にしたが、計測側は値返しのままだったので、
**測れなかったことを「空の結果」としてしか表せなかった**（issue #3 の後半。DESIGN.md §3-6 違反）。
`shape()` が空を返すと行が 1 本も立たず、`metrics()` が `{}` を返すと行の高さが 0 になる。
どちらも「成功したがテキストが消えた PNG」になる。分類（`Shaper` の実装。契約は
[text_measurer.hpp](../src/text/text_measurer.hpp)）:

| 事象 | 返すもの |
|---|---|
| 空文字列（グリフが 0 個）、既定無視文字だけの run、`font_size` が 0 | 成功（空の `ShapedText` / 送り 0） |
| 豆腐（どのフォントにもグリフがない） | 成功 + `ShapedCluster::missing = true`（DESIGN.md §3-6 の唯一の例外） |
| `FontStore` にフォントが 1 つもない、不正な `FontId`、非有限の `font_size` | `Internal`（呼び出し側のバグ） |
| `hb_font_get_h_extents()` が偽（hhea / OS/2 が読めない） | `FontLoad` |
| `hb_font_create()` が空のフォントを返す、`hb_buffer_allocation_successful()` が偽 | `OutOfMemory`（A26 の種類） |
| グリフ数が 0 でないのに HarfBuzz がグリフ情報を返さない | `Internal` |

- **`hb_buffer_allocation_successful()` は `hb_buffer_create()` 自体の失敗も捕まえる**
  （確保に失敗すると `successful = false` の空のバッファが返るため）。だから
  コンストラクタが `Result` を返せなくても、確保失敗を黙って握りつぶさずに済む
- **フォントが 1 つもない `FontStore` はエラーにした。** それまでは「存在しない FontId 0 の
  `.notdef` を全文字ぶん返す」で通っていて、失敗するのはラスタライズの段（A19 の `Internal`）だった。
  api は空の `FontSet` を `NoFonts` で弾いているので、ここに来るのは呼び出し側のバグ
- `font_size` が 0 や負のときは従来どおり送り 0 で成功する（CSS の `font-size: 0` は正当な指定で、
  A19 の `pixel_size` と違ってラスタライザには渡らない）。非有限だけを `Internal` にする
- layout 側の呼び出しは `LayoutEngine::shape()` / `metrics()`（A21 の計測カウンタ）に集約済みなので、
  波及は機械的。**失敗した結果はメモに残さない**（A29 の準備済み段落は `Result` が成功した後でのみ
  `remember()` する）。偽の `TextMeasurer`（`tests/layout/test_support.hpp`）には失敗を注入する口
  （`fail_on` / `fail_metrics`）を足し、layout が伝播することをテストで固定した

**A31. 豆腐の警告は ③ レイアウトが組み立てる。`Shaper` は何も溜めない。**
DESIGN.md §6-6 は豆腐の警告を「コードポイント **＋位置**」と定めているのに、公開の `Warning` は
位置を持たず、`Shaper` が「見たコードポイントの集合」を副作用として溜めていた（issue #9）。
`Shaper` は素の `u32string_view` しか受け取らないので、原理的にどの要素の文字かを知らない。
向きを変えて、**位置を知っている側（layout）が、シェーピングの結果から拾う**ようにした:

- **`Shaper` は純粋になった。** `take_missing_glyphs()` と内部の集合を削除し、豆腐は
  `ShapedCluster::missing` で返すだけにした。同じ入力からは必ず同じ結果が返るので、
  固有寸法の計測で同じ段落を何度シェーピングしても結果が変わらない
- **位置は文字ごとの属性の表（A27）の 4 つめの層。** `shape()` の区間も `TextFragment` も
  この層では切らない。**位置をシェーピング属性や装飾属性に入れると、同じ見た目の `<span>` を
  2 つに割っただけでカーニングと合字が消える**（issue #8 の再発）
- **粒度は「その文字を含むテキストノードの先頭」**（= `StyledNode::location`）。文字単位の L:C は
  文字参照（`&#x1F600;` は 9 バイトで 1 文字）と空白の畳み込み（A14）を遡らないと出せず、
  html のテキストノードが「解決後の文字 → ソースのバイト位置」を持っていない。
  **不正確な位置を出すくらいなら出さない**方を選んだ（html / style には手を入れていない。
  必要になったときに足すものは下の「残した穴」）。`<rt>` のルビ文字だけは、子のテキストノードを
  連結して空白を畳み込んだ 1 本なので `<rt>` 要素自身の位置を使う
- **報告は (コードポイント, テキストノード) の組ごとに 1 件。** 同じノードに同じ絵文字が 5 個
  あっても 1 件、別のノードなら別件。`LayoutEngine` が `std::set<MissingGlyph>` で重複を除き、
  **入力位置の昇順 → コードポイントの昇順**で返す（`std::set` の順序がそのまま出力の順序。
  ポインタ値も unordered の反復順も出力に出さない）。同じ段落は計測と配置で何度も組まれる
  （配置は d² 回、`<img>` を含む段落は準備も複数回。A29）ので、「組むたびに積む」形では
  重複する。メモの有無で内容が変わらないことは性質テスト（`layout_without_memo()`）で固定した
- **運び方は `BoxTree::missing_glyphs`。** ダンプできない中間表現を作らない（DESIGN.md §3-3）ため、
  `--dump-stage box` に `"missing_glyphs": [{"codepoint": "U+1F600", "location": "1:24"}]` が出る
  （1 件も無ければキーごと省く）。同時に `TextFragment` にも `location` を足した（断片の先頭の
  グリフが属するノード）ので、「この行のこの断片は HTML のどこから来たか」をダンプで引ける
- **公開 API**: `Warning` に `std::optional<SourceLocation> location` を足し、`detail` の末尾に
  `to_string(RenderError)` と同じ書式で ` at L:C` を付ける。CLI は detail をそのまま stderr に出す

**残した穴**（直すなら html / style から）: 文字単位の桁を出すには、`html::Node` のテキストに
「文字参照を解決したあとの文字 → ソースのバイト位置」の対応（例: 文字ごとのオフセット表、または
「参照を含まない区間」の並び）が要る。平坦化の段では畳み込み前の添字まで追えている
（`inline_collect.cpp` の `Collapsed::source`）ので、html が上の対応を持てば layout 側は
`FlatChar` に「ノード内オフセット」を 1 本足すだけで桁まで出せる。

**A32. 決定性は 2 層に分けて固定する。zlib の deflate 出力は固定しない。**（issue #10-4）
DESIGN.md §3-5 の「同じ入力 → バイト単位で同じ PNG」は、**どの範囲で**成り立つかが書かれて
いなかった。同一プロセス内の一致と、コンパイラ・標準ライブラリ・CPU をまたいだ一致は別の主張。
PNG のバイト列を 2 つに割り、それぞれ別の方法で固定した:

- **ピクセル**（浮動小数点演算の結果）… `tests/golden/*.png` のピクセル完全一致。CI の 4 ジョブ
  （clang-18 + libc++ の dev / asan / release と gcc-14 + libstdc++）が同じ画像に通っているので、
  **この 2 つのツールチェーン（x86-64 Linux）ではピクセルまで一致する**と言い切れる
- **shashoku 自身が決めるバイト列**（IHDR / 行ごとのフィルタの選択 / フィルタ後の走査線 /
  チャンクの並びと CRC）… `tests/png/determinism_test.cpp` が「展開後の IDAT の CRC-32」を
  リポジトリに固定する。整数演算だけなので環境をまたいで 1 ビットも変わってはいけない
- **zlib の deflate 出力**… **固定しない。** 版（1.3.2 固定）と設定（`compress2` +
  `Z_BEST_COMPRESSION`）で決まる。圧縮レベルを変えると出力は必ず変わる（実測: IDAT が
  29 バイトの 16x16 単色画像でもレベル 6 と 9 で違った）ので、レベルの既定を変える作業
  （issue #13）と期待値が正面衝突する。だから「PNG 全体のバイト列のハッシュ」は持たず、
  レベルに依らない層だけを固定してある。**#13 が落ち着いたら全体のハッシュを 1 つ足せばよい**

確認した事実: `tests/golden/*.png` は shashoku 自身のエンコーダの出力そのもの（`SHASHOKU_UPDATE_GOLDEN`
の書き出しは `png::encode()` を通る）。外部の zlib で IDAT を展開 → レベル 9 で再圧縮すると、
16 枚すべてでファイルのバイト列と一致した。つまり「render() の出力 == ゴールデンのファイル」は
いま成り立っているが、#13 でレベルを変えると（ピクセルは同じまま）成り立たなくなる。

保証する範囲・保証しない範囲は [README の「決定性」](../README.md#決定性同じ入力から同じ-png)に
利用者向けに書いてある。

**A33. zlib の圧縮レベルは入力の一部。それ以外の設定は固定のまま。フィルタ選択は
「選ばれるフィルタを 1 つも変えない」形で速くする。**
`render()` の時間の約 90% が PNG エンコードだった（OG カード 1200x630 で 63.4 ms 中 56.9 ms。
issue #13）。原因は 2 つで、**`Z_BEST_COMPRESSION` 固定**と、**行ごとのフィルタ選択が
ベクトル化できていなかった**こと。

- **レベルは `RenderOptions::compression_level`（0〜9、既定 6）。** 決定性は「同じ入力 →
  同じバイト列」であって「レベルが 1 つしかないこと」ではないので、レベルが入力に加わるだけでは
  DESIGN.md §3-5 は崩れない（A25 の上限と同じ理屈）。範囲外は `InvalidOption`。
  `Z_DEFAULT_COMPRESSION`（-1）は受け付けない: 同じ値でも zlib の版によって実際のレベルが
  変わりうるため。**ストラテジ・windowBits・memLevel は従来どおり固定**で、増やすつもりもない
  （`compress2` の既定。設定が増えるほど「同じ絵なのに違うファイル」が増える）
- 既定を 9 から 6（zlib の既定）に下げた。ファイルは 1〜2% 大きくなるが、エンコードは 4 分の 1
  になる。最小サイズが要る使い方（画像を配布物に焼き込む等）では 9 を指定できる
- 既定値は `RenderOptions` が正で、`png::kDefaultCompressionLevel` は png を単体で使うときの
  既定。両者の一致は `src/api/render.cpp` の `static_assert` が検査する（A25 と同じ流儀）。
  api は既定に頼らず必ず明示的に渡す
- **フィルタ選択の最適化は出力バイト列を 1 ビットも変えない。** 速くなった理由は
  「フィルタ種別を実行時の引数からテンプレート引数に移した」ことだけ。1 バイトごとに
  `switch (filter)` していたせいで、5 種ともベクトル化されていなかった（1200x630 の行走査で
  12.9 ms）。種別ごとに関数を分けると 5.3 ms になる。境界（先頭 1 画素は左と左上が無い）も
  別の繰り返しに切り出して、残りから条件分岐を追い出した
- **候補バッファは 5 本から 1 本に。** 勝ったフィルタだけを最後にもう一度だけ出力の位置へ
  適用する。5 本のままでも速さは同じだったが、広い絵ではキャッシュに載らなくなるだけなので
  1 本にした
- **残差の書き出しと絶対値和は融合しない。** 1 パスにまとめると、合計のために 32bit へ広げる
  必要が出て残差の計算まで 32bit レーンに落ち、**2 倍遅くなった**（11.0 ms）。
  「パスを減らす」が常に速いとは限らない
- SIMD の組み込み関数は書かない（移植性と決定性のため。自動ベクトル化に任せる）。
  libm にも触らない（A9）

計測（release、i9-14900KF、21 回の中央値。`png::encode` のみ。load average 1.4〜1.6 の静かな状態で測った。
同じマシンで他のビルドが走っていると 1.5〜2 倍に膨らむので、絶対値ではなく比を見ること）:

| 入力 | 修正前（level 9） | 修正後 level 9 | 修正後 level 6（新しい既定） |
|---|---:|---:|---:|
| OG カード 1200x630 | 59.1 ms / 73,689 B | 51.7 ms / 73,689 B | **12.5 ms** / 74,733 B |
| OG カード @2x 2400x1260 | 154.7 ms / 158,939 B | 126.1 ms / 158,939 B | **45.4 ms** / 161,517 B |
| 和文の長いページ 800x1320 | 189.0 ms / 361,482 B | 179.3 ms / 361,482 B | **26.8 ms** / 366,026 B |

フィルタ選択の段だけ（level 0 で測ったもの。zlib の無圧縮ブロックの複写と adler32 のぶん約 6 ms を含む）:
OG カード 18.8 → 11.7 ms、@2x 73.6 → 45.7 ms、長いページ 26.4 → 16.5 ms。
行走査だけを切り出した細かい計測（1200x630）は上に書いた 12.9 → 5.3 ms。

`render()` 全体では、OG カードが 63.8 ms（修正前・level 9）→ 17.9 ms（修正後・既定の level 6）、
@2x が 173.5 → 66.3 ms、和文の長いページが 200.9 → 40.3 ms（41 回の中央値）。

**バイト一致の検証**: 修正前のエンコーダと、合成画像 11 種（1x1 / 単色 / 傾き / ノイズ /
写真風 800x600 / 4096 幅 / 4096 高 ほか）x レベル 0〜9 の 110 通りでバイト完全一致。
examples 4 種と和文の長いページも level 9 で `cmp` 一致。ゴールデン 16 枚は
デコードした画素で比べているのでもともと影響を受けない。

**A34. 資源は「読み取り専用で共有できるもの」と「1 スレッド専用の可変状態」に分ける。
共有する側だけを公開型（`LoadedFonts` / `LoadedImages`）にする。** それまでは `render()` が
呼ばれるたびに全フォントを解釈し直し、全画像をデコードし直していた（issue #7）。`FontStore` を
そのまま共有ハンドルとして公開するのは安全でない: `FT_Face` はグリフを読むたびに書き換わる。

**動機は計測で置き換えた。** issue #7 本文の「約 10MB の解釈が毎回」は起きていない
（FreeType も HarfBuzz も遅延解析で、フォント読み込みは 3 本 9.18 MiB で 0.78 ms =
`render()` の 1.2%）。実測で効くのは次の 3 つ:

| | 修正前 | 修正後 | 出典 |
|---|---:|---:|---|
| 8 並行で 96 本組んだときの RSS 増分 | 194 MiB | **110 MiB** | 共有資源 1 組 + 実行ごとの FreeType |
| 全面背景画像（1200x630）のデコード | 16.0 ms/回 | 0（用意は 1 回） | `render()` 1 回の 26% |
| 和文長文のラスタライズ（1632 グリフ / 異なり 44） | 6.72 ms | **0.33 ms** | 実行ごとのグリフキャッシュ |

時間そのものは 1 回あたり 66.2 → 62.3 ms（-6%）にしかならない。**`render()` の約 90% は
PNG エンコードで、そこは #7 の対象外**。

決めたこと:

- **共有してよいものの線引きは、各ライブラリの文書に従う。**
  - 共有（`text::FontStore`）: フォントのバイト列 / `hb_face_t` / cmap 引き用の `hb_font_t` /
    解析済みの family・family_folded・weight・italic・upem・face_index。HarfBuzz の face と font は
    `hb_face_make_immutable` / `hb_font_make_immutable` を掛けてから共有する（「immutable な
    オブジェクトは多スレッドでの利用を容易にする」。face のシェーププランの置き場はアトミック）
  - 実行ごと: `FT_Library` / `FT_Face` / グリフスロット（`FreeTypeGlyphSource`）、
    スケール付きの `hb_font_t` と `hb_buffer_t`（`Shaper`）。FreeType は「`FT_Face` は同時に
    1 スレッドからしか使えない。同じ `FT_Library` に対する face の生成・破棄も同時に行えない」と
    定めている。**`FT_Library` ごと実行ごとにする**（`FT_Init_FreeType` は 0.002 ms、
    face の作り直しは 3 本で 0.16 ms = render の 0.26%）。共有して mutex で直列化すると、
    並行度が上がるほどグリフのラスタライズが待ち行列になって損をする
- **`FontStore` は FreeType のハンドルを持たない。** ただし family / weight / italic / upem と
  「輪郭を持つか」（`FT_IS_SCALABLE`）は**今までどおり FreeType から読む**。HarfBuzz の name 表から
  読み直すと値が変わってフォールバック順（§3.5）が静かに変わりうるため、`load()` の中だけで
  一時的に `FT_Library` + `FT_Face` を作って読み、すぐ閉じる
- **不変にした `hb_font_t` の生ポインタは外に出さない**（`detail::ImmutableFont`）。HarfBuzz の
  setter は immutable なオブジェクトに対して**黙って失敗する**ので、`hb_font_set_scale()` を
  呼びたいコード（`Shaper`）がこのフォントを掴むと「スケールが変わらないまま組まれる」という
  見つけにくい壊れ方をする。読み取り専用の 2 つの用途（cmap 引き・縦組み用グリフの調査）だけを
  関数で出し、スケールを変えたい側は `hb_face_t` から自分の font を作る
- **グリフキャッシュは実行ごと**（`FreeTypeGlyphSource`）。実測で、実行をまたいで共有しても
  実行内キャッシュの効果の 6% しか増えない。実行ごとなら容量制限・追い出し方針・ロック・
  「前の実行の内容が残っても同じ結果」の証明をすべて避けられる。鍵は
  `(FontId, GlyphId, pixel_size のビット列, sideways)` の `std::map`。**容量は内部定数**（16 MiB）で、
  超えたら**新規登録をやめるだけで追い出さない**。容量を `RenderLimits` に足さないのは、A25 の
  上限がすべて「入力の一部」（同じ入力 + 同じ上限 → 同じ出力）なのに対し、キャッシュ容量は
  出力に一切影響しないから。混ぜると「上限」の意味が 2 種類になる
- **`LoadedImages::prepare()` は `RenderLimits` を引数で取り、`render()` 側で画素数を再検査する。**
  デコード前に弾かないと A25 の (c)「大きな確保の直前に判定する」が崩れる。両方で見るので、
  `prepare()` と `render()` に違う上限を渡しても「同じ HTML + 同じ `RenderLimits` → 同じ結果」は
  崩れない（厳しい方が効く）
- **ムーブ済みの共有資源を渡したら `InvalidOption` で落とす。** 「フォント 0 本」として黙って
  組むと、全部豆腐の PNG が「成功」で返る（DESIGN.md §3-6）
- **プロセス全体のグローバルキャッシュは作らない**（バイト列のハッシュ → 解釈済みフォント、など）。
  DESIGN.md §3-5 に正面から反する。出力自体が変わらなくても、いつ解放されるか・同時実行で誰が
  ロックを持つかが利用者から見えなくなり、`render()` が純粋関数だという説明が成り立たなくなる。
  共有資源は**利用者が持つ**
- **HarfBuzz のプロセス全体の遅延初期化**（既定の Unicode 関数群・言語タグの表）は
  `FontStore::load()` の先頭で 1 回触っておく。共有する前に単一スレッドで温めておかないと、
  最初の同時実行で競合しうる（HarfBuzz 自身のスレッドテストも同じ理由で先に呼んでいる）
- **検証**: `tests/integration/reuse_test.cpp`（N 回の一致 / 警告の一致 / A→B→A の混線なし /
  `dump()` の一致 / ムーブ済み / prepare と render で違う上限）と
  `tests/integration/concurrency_test.cpp`（8 スレッド x 4 回が単スレッドの結果とバイト一致）。
  後者は `tsan` プリセット（`SHASHOKU_SANITIZE_THREAD`）でも回す。**普段の完了条件には入れない**:
  依存ライブラリまで再ビルドになるので `dev` / `asan` と並べると重い

**A-new. Chrome との比較は「ページ内で測って DOM に書き出す」方式にする。判定は
欠落 / 重なり / 改行位置 / はみ出しの 4 つだけで、座標は論理座標にそろえて比べる。**（issue #15）

`scripts/compare/`。**ローカル専用で CI には入れない**（製品のビルドを Chrome に依存させない）。
使い方と差の分類は [chrome_compare.md](chrome_compare.md)。

- **依存を増やさない。** Playwright も Puppeteer も Node も使わない。python3 の標準ライブラリだけで
  動かす。CDP のクライアント（WebSocket）は標準ライブラリに無いので、**ページ内の JavaScript で
  `document.fonts.ready` を待ってから `Range.getClientRects()` で測り、結果を base64 にして
  DOM に書き出す**。`chrome.exe --headless --screenshot --dump-dom` の標準出力から取り出す
  （`--screenshot` は `--dump-dom` より**前**に置く。後ろだと PNG が書かれない）
- **Chrome 側を論理座標に直してから比べる。** #15 の本文は「縦書きは `display-list` を使うのが楽」
  としているが、Chrome の物理座標を論理（縦書きなら inline = y、block = −x）に直せば
  横書きと同じ 1 本のコードで済む。`box` ダンプに一本化した
- **比べない値を先に決める**: グリフ単位の x（A8 の丸めで必ず差が出る）、block 方向の絶対座標
  （`line-height: normal` の作り方が違う。Chrome は ascent / descent を整数 px に丸めるので、
  16px の行で 24 px、shashoku は 23.171875 px）、ピクセルの色
- **ルビの注記は「欠落」の判定から外す。** ルビは行の上（縦なら右）の帯に置かれ、その帯と注記の
  インラインボックスの関係は両者で作りが違う（実測: Chrome では注記が親ブロックの箱から 4 px 出る）。
  混ぜるとルビのケースが全部「差」になる。広がり自体は別項目で報告する
- **Chrome 側の「重なり」も判定に使わない。** ルビ組では親文字の `Range` の箱が注記の幅まで広がり、
  続きの文字と重なって見える（実測 8 px）。判定は (1) shashoku が字を重ねていないか、
  (2) 両者の字送りが合っているか、の 2 つで行う
- **はみ出しは祖先の箱との共通部分で測る。** flex アイテムは自分の箱が親より広くなることがあり
  （#18 の再現入力では行も箱も 82.2 px で、親の 32 px はどこにも出てこない）、自分の箱だけ見ると
  はみ出しが 0 に見える
- **紙面はウィンドウではなく CSS で決める。** WSL から Windows の `chrome.exe` を呼ぶと
  `--window-size` が当てにならない（幅は 500 px 未満にならず、`--dump-dom` のときは
  `window.innerWidth` が 0 になる）。`#shk-root` を `position: absolute; left: 0; top: 0` で
  物理的な左上に固定し、幅（縦書きは高さも）を指定する
- **同じフォントで描かれたことを確かめる手段を持つ。** `document.fonts` の `status` だけでは
  「ある文字だけシステムフォントに落ちた」を見逃すので、同じ文字列の幅を shashoku と突き合わせる
- **`--user-data-dir` は実行ごとの使い捨て**（`<out>/chrome-profile-<pid>`、終了時に消す）。
  ユーザーの普段のプロファイルには触らない

**A-new-2. ルビ組で短い方は「中央に置く」だけ。親文字を伸ばして長い方に合わせない。**（issue #15）

§3.8 の「幅は max(親文字, ルビ)、短い方を中央に置く」を判断として書き起こしたもの。
Chrome 比較で、ルビのあるケース（#15 のケース 4 / 5 / 7 / 21）がすべて「重なり（字送り）」の差に
なるので、**意図した差だと言うために A 番号が要る**（#15 の受け入れ条件）。

Chrome は CSS Ruby の既定（`ruby-align: space-around`）で**親文字の側を均等に広げて**注記の幅に
合わせる。実測（ケース 5、`<ruby>写植<rt>しゃしょく</rt></ruby>`、16px）: 親文字の字送りが
shashoku は 16.00 px（そのまま）、Chrome は 20.00 px（注記の 40 px に合わせて 2 文字に配分）。
ケース 21（親文字 1 文字 + 長い注記）では 57.73 px と 152.00 px になる。

中央に置くだけにしているのは、JLREQ 3.3.2 の 1:2:1 の配分と**ルビの掛け**（親文字の隣の文字への
はみ出し）が未対応だから（README の「既知の制限」）。この 2 つを入れるまでは、親文字の側を
広げても JLREQ に近づかない（掛けが無いと、広げたぶんが行全体を間延びさせるだけになる）。
**1:2:1 と掛けを入れるときに、この判断ごと見直す。**

---

## 2. モジュールと依存

```
core ─────────────┬──> png ────────────────────────────────┐
  │               ├──> raster ─────────────────────┐       │
  │               ├──> text ──(GlyphSource 実装)───┤       │
  │               ├──> html ──> style ──┐          │       │
linebreak（孤立）─┴─────────────────────┴> layout ─┴> paint ┴> api ──> tools/cli
```

| モジュール | 名前空間 | 依存してよいもの | 段 |
|---|---|---|---|
| core | `shashoku` | （なし） | — |
| linebreak | `shashoku::linebreak` | **（なし。core にも依存しない）** | ③ の中核 |
| png | `shashoku::png` | core, zlib | ⑥ |
| raster | `shashoku::raster` | core | ⑤b |
| text | `shashoku::text` | core, raster/glyph_source.hpp, FreeType, HarfBuzz | ④ |
| html | `shashoku::html` | core | ① |
| style | `shashoku::style` | core, html/dom.hpp | ② |
| layout | `shashoku::layout` | core, style/computed_style.hpp, text/text_measurer.hpp, linebreak | ③ |
| paint | `shashoku::paint` | core, layout, raster/display_list.hpp | ⑤a |
| api | `shashoku` | すべて | — |

- 各モジュールは `src/<module>/CMakeLists.txt` で `shashoku_add_module()`、テストは
  `tests/<module>/CMakeLists.txt` で `shashoku_add_test()` を呼ぶ（[cmake/Modules.cmake](../cmake/Modules.cmake)）。
  `src/CMakeLists.txt` と `tests/CMakeLists.txt` は存在するディレクトリを自動で拾うので触らない
- 失敗しうる関数は `Result<T>`（= `std::expected<T, Error>`）を返す。例外を投げない・捕まえない
- 各段の出力は `dump_json()` 系の関数で JSON にできること（`core/json_writer.hpp`）。キー順は固定

---

## 3. モジュール仕様

### 3.1 core

契約ヘッダの実装（`utf8.cpp`, `json_writer.cpp`, `error.cpp`）とそのテスト。
`to_string(RenderError)` の書式は error.hpp のコメントどおり。location がなければ ` at L:C` を省く。

### 3.2 png（⑥）

```cpp
namespace shashoku::png {
Result<std::vector<std::uint8_t>> encode(const Bitmap& bitmap,   // RGBA8 → PNG
                                         int compression_level = kDefaultCompressionLevel);
Result<Bitmap> decode(std::span<const std::uint8_t> bytes,       // PNG → RGBA8
                      std::uint64_t max_pixels = kMaxPixels);
}
```

- **encode**: シグネチャ + IHDR + IDAT + IEND。color type 6（RGBA）/ 8bit / 非インターレース。
  フィルタは行ごとに 5 種（None / Sub / Up / Average / Paeth）を試し、「符号つきバイトとみなした
  絶対値和」が最小のものを選ぶ（PNG 仕様 §12.8 のヒューリスティック。同点なら番号の小さい方）。
  **zlib はレベルだけが入力で（0〜9、既定 6。A33）、ストラテジ・windowBits・memLevel は固定**。
  レベルはフィルタの選択には影響しない。CRC32 は自前で実装する
  （zlib の `crc32()` は使わない: PNG エンコーダの自作範囲）。幅か高さが 0、
  `rgba.size() != width*height*4`、`compression_level` が 0〜9 の外: いずれも `InvalidOption` エラー。
  api は `RenderOptions::compression_level` を必ず明示的に渡し、既定値の一致は `static_assert`
  で検査する（A25 と同じ流儀）
- **decode**: `<img>` とゴールデンテストの比較用。対応: 8bit の gray / gray+alpha / RGB / RGBA /
  パレット（tRNS 対応）、および 16bit（上位 8bit に落とす）、非インターレースのみ。
  対応外（インターレース、1/2/4bit）とシグネチャ・CRC・チャンク構造・zlib の破損は
  `ImageDecode` エラー。補助チャンクは読み飛ばす。ガンマや ICC は無視する。
  **`max_pixels`（幅 x 高さ）の判定は IHDR を読んだ時点で行う**（画素を確保する前。A25 の (c)）。
  超過は `LimitExceeded`。api は `RenderLimits::image_pixels` と `total_image_pixels` から
  「いま効いている方」の値を渡す
- 受け入れ: `pngcheck` が**圧縮レベル 0〜9 のすべてで**通る。encode → decode がどのレベルでも
  元の Bitmap に戻る。同じ入力・同じレベルなら 2 回目もバイト一致。壊れた入力で落ちない（ASan）

### 3.3 raster（⑤b）

```cpp
namespace shashoku::raster {
struct Target {
  float width = 0, height = 0;  // CSS px
  float scale = 1;              // デバイスピクセル = ceil(CSS px * scale)
  Color background = kTransparent;
  std::uint64_t max_device_pixels = kMaxDevicePixels;  // 幅 x 高さの上限（A25）
};
Result<Bitmap> rasterize(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                         std::span<const Bitmap> images);
}
```

- 合成は source-over、ストレートアルファ、sRGB 空間のまま。8bit 整数演算で、丸めは
  `(x * 255 + 127) / 255` 系の「四捨五入」に統一する。半透明同士を重ねたときのアルファも正しく
  （`out_a = src_a + dst_a * (1 - src_a)`、色はアルファで重みづけ）
- 矩形は辺が小数座標でもよい（端のピクセルは面積比で被覆率を出す。scale = 2 や 0.5px 境界で必要）
- 角丸・枠線・クリップの縁は被覆率によるアンチエイリアス。方式は実装者が選んでよいが、
  決定的であること（A9）と、半径 0 のとき FillRect と 1 ビットも違わないこと
- DrawGlyphs: 原点をデバイスピクセルの整数に丸めてから（A8）`GlyphSource::rasterize()` の
  被覆率に色を掛けて合成する。**`rasterize()` のエラーはそのまま伝播する**（A19）。
  読み飛ばしてよいのは「成功して空のビットマップ」= 空白グリフだけで、
  `coverage.size() == width * height` を破ったビットマップは `Internal`。
  `size * scale` が非有限・非正になるコマンドは（他の非有限な寸法と同じく）無視する
- DrawImage: 縮小は面積平均、拡大はバイリニア。等倍で整数位置ならピクセルをそのまま合成
- PushClip / PopClip: 入れ子は積集合。対応が取れていない列は `Internal` エラー
- 描画対象外（ビットマップの外、クリップの外）へのアクセスで落ちない
- **デバイス画素数は、ピクセルバッファを確保する前に `target.max_device_pixels` と比べる**
  （A25 の (c)）。超過は `LimitExceeded`。api は `RenderLimits::device_pixels` を渡す。
  1 辺が `Bitmap::width` の型（`std::uint32_t`）に収まること、画素数 x 4 が `std::size_t` に
  収まることは、`max_device_pixels` では緩められない絶対上限

### 3.4 linebreak（③ の中核・製品のコア）

契約は [line_breaker.hpp](../src/linebreak/line_breaker.hpp)。ここでは規則を定める。
**このモジュールのテスト群が禁則処理の実質的な仕様書になる**（DESIGN.md §10-2）ので、
テストケースには出典（UAX #14 の規則番号 / JLREQ の節 / CSS Text の節）をコメントで添える。

**(1) 分割クラス**: UAX #14 の分割クラスのうち、日本語と基本ラテンに必要なものをテーブルで持つ:
BK CR LF NL SP ZW WJ GL CM ZWJ OP CL CP QU EX IS SY NS CJ IN B2 BA BB HY PR PO NU AL ID
（+ 絵文字用に EB EM RI を ID 相当で扱う。ハングル音節は ID）。未知は AL、CJK の統合漢字・
かな・全角記号の範囲は ID。East Asian Width が必要な規則（LB30 の OP/CP）は全角括弧の表で代用する。
`Strictness` による CJ の解決と Loose の追加規則は CSS Text 3 §5.3 に従う。

**(2) 分割可能位置**: UAX #14 の規則 LB2〜LB31 のうち、上のクラスに関係するものをペア表 +
文脈規則（LB8 の空白、LB9/10 の CM、LB14〜17 の空白越し、LB25 の数値、LB30a の RI）で実装する。
日本語の禁則はこの上に自然に乗る: 行頭禁則 = CL / CP / NS / EX / IS / (strict の) CJ の前で割らない、
行末禁則 = OP の後で割らない、分離禁則 = `B2 B2`（——）と `IN IN`（……）と数値 + 単位。
`Config::extra_*` は該当文字を NS / OP 相当に格上げする。`Item::no_break_before` と
`ItemKind::Atomic`（ID 扱い）を尊重する。

**(3) 行の決定**: 貪欲法。幅に収まる最後の分割可能位置で割る。行末の空白（SP）は幅に数えず
`content_end` から除く。`ForcedBreak` の直後で必ず改行する。
**仕事の量は N に線形**（A24）。行を 1 本組むのに段落の残り全体を舐めない: 前方に探すもの
（次の強制改行・次の分割可能位置）は `break_lines()` が持ち回り、後方に遡るもの（LB30a の RI の並び）は
1 回の走査で表にする。`Counters` で回数を数え、`complexity_test.cpp` が N = 20,000 で検査する。

**(4) 約物の空き**（JLREQ 3.1.2〜3.1.5）。対象は全角の約物だけ:
始め括弧 `「『（〔［｛〈《【〖〘〝`、終わり括弧 `」』）〕］｝〉》】〗〙〟`、読点 `、，`、句点 `。．`、中点 `・：；`。
これらは 1em の送りのうち半分（中点は両側 1/4 ずつ）が空き。空き量は `Item::em` から計算する。
- `collapse_punctuation_spacing`: 終わり括弧類・句読点の直後に始め括弧が続くとき、および
  終わり括弧類・句読点が連続するとき、始め括弧が連続するときに、間の空きを半角ぶん詰める
  （JLREQ 3.1.4 の表に従う）。行をまたいだペアには適用しない
- `trim_line_end`: 行末の終わり括弧・句読点が収まらないとき、後ろの半角空きを捨てて収める
- `trim_line_start`: 行頭の始め括弧の前の半角空きを捨てる

**(5) あふれ処理**:
- `Oidashi`: (3) のまま。禁則文字は手前の文字を道連れにして次の行へ行く
- `Burasage`: 行末に来た句読点（`、。，．`）1 文字が収まらないとき、それを行の外に出す
  （`Line::hang`）。収まるならぶら下げない。句読点以外には効かない（追い出しになる）
- `Oikomi`: 貪欲法で決めた位置の次の分割可能位置までを、行内の約物の空き（(4) の空き量が上限）を
  詰めれば収められるなら、詰めて収める。詰め量は各約物の詰め可能量に比例配分する。
  収められなければ追い出し
- どのポリシーでも (A4) 禁則 > 幅。`break_anywhere` は分割可能位置が 1 つもない行でだけ発動する

**(6) 不変条件**（ファジングで検査する）: 全アイテムがちょうど 1 行に属する / 行は空でない /
`overflows` でない行は `width <= available_width`（+ 許容誤差）/ 分割位置は必ず
`break_opportunities()` が true の位置か ForcedBreak の直後か `break_anywhere` の発動
（= 両側のアイテムがともに anywhere の位置。(7)）/ 同じ入力には同じ出力。
ファジングにはアイテムごとのポリシーをランダムな範囲で混ぜた入力も含める。

**(7) アイテムごとのポリシー**（A23）: `Item::strictness` / `Item::break_anywhere` は
インライン要素（`<span>`）での上書き。nullopt なら `Config` の値を使い、すべて nullopt なら
出力は `Config` だけを使っていたころと完全に同じ。境界の規則は:
- (1) の分割クラスの解決は**そのアイテム自身の** `strictness` で行う。(2) のペア表・文脈規則は
  解決済みのクラスに対して従来どおり働く。例外は loose の「直前が ID ならハイフン ‐ – の前で
  割ってよい」だけで、これは行頭に来る側（後ろのアイテム）の `strictness` で決める
- (5) の `break_anywhere` の緊急分割は、**両側のアイテムがともに** `break_anywhere` の位置でだけ
  起こす。発動条件と位置選びの優先順は変わらない。割れる位置が 1 つもなければ A4 ではみ出す
- `break_opportunities()` は `break_anywhere` の影響を受けない（緊急分割は「分割可能位置」ではない）。
  `min_content_width()` は `strictness` の影響を受け、`break_anywhere` の影響は受けない

### 3.5 text（④）

```cpp
namespace shashoku::text {
class FontStore {                       // フォント実体の唯一の所有者（DESIGN.md §3-2）。**共有資源**（A34）
 public:
  Result<FontId> load(std::span<const std::uint8_t> bytes);  // バイト列をコピーして保持。追加順 = フォールバック順
  // family 名・weight の照会、(cp → どのフォントのどのグリフか) の解決 など
};
class Shaper final : public TextMeasurer { /* FontStore を参照。HarfBuzz。実行ごと・副作用を持たない */ };
class FreeTypeGlyphSource final : public raster::GlyphSource { /* FontStore を参照。実行ごと */ };
}
```

- **共有資源と実行コンテキストを分ける**（A34）。`FontStore` は `load()` を終えたあと完全に
  読み取り専用で、何本の `render()` から何スレッドで同時に読んでもよい。持つのはバイト列・
  `hb_face_t`（immutable）・cmap 引き用の `hb_font_t`（immutable。生ポインタは
  `detail::ImmutableFont` の外に出さない）・解析済みの family / weight / italic / upem / face_index だけ。
  **FreeType のハンドルは持たない**: `FT_Library` と `FT_Face` は `FreeTypeGlyphSource` が
  実行ごとに作る（`FT_Face` は 1 スレッド専用で、同じ `FT_Library` に対する生成・破棄も
  直列化が要るため）。スケール付きの `hb_font_t` と `hb_buffer_t` は `Shaper` が実行ごとに持つ

- 依存: FreeType と HarfBuzz を FetchContent で版・ハッシュ固定（A7）。システムのライブラリ
  （zlib, png, bzip2, brotli）を拾わせない。HarfBuzz は公式 CMake でも `harfbuzz.cc`（アマルガム）
  でもよいが、`shashoku_mark_system()` でヘッダを SYSTEM 扱いにすること
- フォント選択（A15）: **`font-family` の指定は family の優先順を変えるだけで、太さの照合は
  常に全 family に対して働く**（`font-family` を書かない普通の HTML でも `font-weight: 700` の
  見出しが Bold で描かれる）。フォールバック列は次の順で作る:
  1. FontStore の全フォントを family 名（大文字小文字を無視）でグループ化する。グループの順序は
     「その family の最初のフォントが追加された順」
  2. `font_family` を順に見て、一致するグループがあれば列に入れる（総称ファミリや FontStore に
     ない名前は読み飛ばす）
  3. 残りのグループを、グループの順序どおりに列に足す
  4. 各グループの中では、`font_weight` に最も近い face（CSS Fonts の規則）を先頭に、残りの face を
     その後ろに置く（同じ family の face はふつう同じ文字を持つので後ろが使われることは稀だが、
     片方の face にしか無い文字の保険として残す）

  `metrics()` が返すのもこの列の先頭のフォント。豆腐の `□` を探す順もこの列
- run 分割: コードポイントごとにフォールバック列を cmap 引きし、最初にグリフを持つフォントを採用。
  同じフォントが続く区間をまとめて HarfBuzz に渡す。結合文字・異体字セレクタ・ZWJ は直前の
  文字と同じ run に入れる（別フォントに割らない）
- 豆腐: どのフォントにもないコードポイントは `ShapedCluster::missing` で返し（**Shaper は
  溜めない**。警告を組み立てるのは ③ レイアウト。A31）、`□`（U+25A1）を
  **フォールバック列の順に全フォントから探して**、最初に見つかったフォントのグリフを
  1em の送りで出す（第一フォントだけを見ると、欧文フォントが先頭のときに幅の狭い `.notdef` が
  1em の枠の左端に出て不揃いになる）。どのフォントにも `□` が無ければ第一フォントの `.notdef`。
  縦書きでも同じグリフを立てる。`ShapedCluster::missing = true`
- 縦書き（`Direction::Vertical`）: UAX #50 の Vertical_Orientation が U / Tu の文字は
  `HB_DIRECTION_TTB` でシェーピング（HarfBuzz が `vert` を自動適用）、R の文字（欧文・数字）は
  横組みでシェーピングして `sideways = true`。**Tr（「」（）ー：； など）は UAX #50 の定義どおり、
  そのフォントに縦組み用グリフ（`vert`）があれば立てて差し替え、なければ横倒し**。
  offset は text_measurer.hpp の座標の約束に合わせる
- run は「フォールバックで決まるフォント」と「スクリプト」の両方の境界で分ける。HarfBuzz には
  script / language（`ja` 固定）を明示的に渡す（`hb_buffer_guess_segment_properties` は
  ロケールを読むので使わない。決定性のため）
- ラスタライズ: `FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP`、`FT_RENDER_MODE_NORMAL`。
  sideways は輪郭を 90° 回してから描く（ビットマップを回すのではなく）。
  失敗は必ず `Result` のエラーで返す（A19 の表）。空のビットマップを返すのは空白グリフだけ。
  `(FontId, glyph_id, pixel_size, sideways)` → ビットマップは**この実行の中だけ**メモする（A34）。
  純粋な写像のメモ化なので、キャッシュの有無で出力は 1 ビットも変わらない（和文の長文で
  ラスタライズ段が 6.72 → 0.33 ms）。容量は内部定数の 16 MiB で、超えたら新規登録をやめる
  （追い出さない）。失敗はキャッシュしない
- シェーピングと計測の失敗も必ず `Result` のエラーで返す（A30 の表）。成功して空の
  `ShapedText` を返してよいのは入力が空文字列のときだけで、「測れなかった」を空で表さない
- `FontStore::load()`: 輪郭を持たないフォント（`FT_IS_SCALABLE` が偽。埋め込みビットマップ専用の
  カラー絵文字フォントなど）は `FontLoad` で拒否する。ラスタライザは輪郭しか扱えないので、
  「全部の字が消えた PNG」になる前に一番早い段で落とす（A19）
- テスト用フォント: リポジトリに置かず、CMake の configure 時に版（コミット SHA）とハッシュを
  固定してダウンロードする（`cmake/TestAssets.cmake`）。Noto Sans JP（OFL）+ 欧文フォント 1 つ
  （フォールバックのテスト用）。パスはコンパイル定義でテストに渡す
- 受け入れ（DESIGN.md Phase 2）: 「こんにちは、世界のみんな。ABC😀」を 1 行でシェーピングでき、
  😀 だけが豆腐として報告される

### 3.6 html（①）

```cpp
namespace shashoku::html {
// 合成ルート "#root" を返す
Result<Node> parse(std::string_view source, std::size_t max_nesting_depth = kMaxNestingDepth);
std::string dump_json(const Node& root);
}
```

- 入力は断片（`<html>` / `<body>` なしで `<div>…` から始まる）。トップレベルに複数ノード可
- 対応タグ: `div span p h1-h6 img ruby rt rp br style`。それ以外は `UnsupportedTag`
  （`html head body script …` も含めてエラー）。コメントと `<!DOCTYPE>` は読み飛ばす
- 対応属性: 共通 `style class id`、`img` は加えて `src width height alt`。それ以外は
  `UnsupportedAttribute`。属性の重複は `HtmlParse`。引用符は `"` `'` なし の 3 形式
- 空要素 `br img` は閉じタグなし（`<br/>` も可）。それ以外の要素の閉じ忘れ・対応しない終了タグ・
  入れ子の誤りは `HtmlParse`（WHATWG の暗黙の閉じ規則は実装しない。fail loudly）
- 文字参照: `&amp; &lt; &gt; &quot; &apos; &nbsp;` と数値参照（10 進・16 進）。未知の名前、
  範囲外・サロゲートの数値は `HtmlParse`。`<style>` の中身は生テキスト（文字参照を解決しない）
- 入力が不正な UTF-8 なら `InvalidUtf8`。すべてのエラーに `SourceLocation` を付ける
- `max_nesting_depth` を超える入れ子は `LimitExceeded`（位置つき。api は
  `RenderLimits::nesting_depth` を渡す）。入力が 4 GiB を超える場合も `LimitExceeded` だが、
  こちらは `SourceLocation::offset` が 32 bit であることによる絶対上限（A25）

### 3.7 style（②）

```cpp
namespace shashoku::style {
// ルートの ComputedStyle は初期値
Result<StyledNode> resolve(const html::Node& root, std::size_t max_style_rules = kMaxStyleRules);
std::string dump_json(const StyledNode& root);
}
```

- カスケード: UA スタイルシート < `<style>` の規則（詳細度 → 出現順）< `style` 属性。
  セレクタは `tag` `.class` `#id` とその結合（`p.note`）、およびカンマ区切りのみ。
  結合子（子孫・`>`）、擬似クラス、`@` 規則、`!important` は `CssParse` / `UnsupportedProperty`
- UA スタイル: `div p h1-h6` は block。`h1`〜`h6` は font-size `2 / 1.5 / 1.17 / 1 / 0.83 / 0.67 em`・
  bold・上下 margin（ブラウザの既定値）。`p` は上下 margin 1em。`rt` は font-size 50%。
  `rp` と `style` は display: none
- 対応プロパティは DESIGN.md §4 の一覧 + 次のショートハンド / 別名:
  `margin` `padding`（1〜4 値）、`border`（`<幅> solid <色>` / `none`）、`border-width`
  `border-style`（solid / none）`border-color`、`flex`（`none` / `auto` / 1〜3 値）、
  `gap` `row-gap` `column-gap`、`background`（色のみ。`background-color` の別名）。
  一覧にないプロパティは `UnsupportedProperty`、値が対応外なら `UnsupportedValue`
- 単位: `px` `em`、`0`（単位なし）。`%` は `width` と `flex-basis` のみ。`line-height` は
  `normal` / 数値 / px / em。色: `#rgb #rgba #rrggbb #rrggbbaa`、`rgb()` `rgba()`、
  CSS の色名、`transparent`、`currentColor`（border-color のみ）
- 継承するのは computed_style.hpp で「継承する」とした群。`inherit` キーワードは全プロパティで可。
  `em` は親の（`font-size` 自身は親の、それ以外は自分の）font-size で解決する
- 合成ルート `#root` は `display: block`、それ以外のプロパティは初期値（`writing-mode` だけ A1 の規則で決まる）。
  `img` の `width` / `height` 属性は px の数値として `attr_width` / `attr_height` に入れる（不正なら `UnsupportedValue`）
- `display: inline` の要素への `width height margin padding border` 指定は `UnsupportedLayout`
  （`img` を除く）。`writing-mode` の途中変更も `UnsupportedLayout`（A1）
- `<style>` から読んだ規則が `max_style_rules` を超えたら `LimitExceeded`（位置つき）。
  セレクタの照合は「規則数 x 要素数」なので、規則の数そのものに上限が要る（A25）

### 3.8 layout（③）

```cpp
namespace shashoku::layout {
struct Options {
  float viewport_width;                  // 物理 px
  std::optional<float> viewport_height;  // 縦書きでは必須（InvalidOption）
  linebreak::Config line_break;          // strictness / break_anywhere は CSS が上書きする
};
struct ImageSize { float width, height; };  // <img> の固有寸法。名前 → 寸法は api が解決して渡す
Result<BoxTree> layout(const style::StyledNode& root, const Options&, text::TextMeasurer&,
                       /* 画像の固有寸法を引く口 */, Counters* = nullptr);
std::string dump_json(const BoxTree&);
}
```

- ボックスツリーは論理座標・ルート原点からの絶対位置（A1）。型は `layout/box_tree.hpp` に
  layout の実装者が定義する。最低限: ブロックの border-box と塗り情報、行ボックス、
  行内のテキスト断片（FontId・サイズ・色・sideways・グリフごとの inline 位置と offset・
  ベースライン / 中心軸の block 位置・**元ノードの位置**）、画像断片、インライン背景。
  加えて豆腐の記録（`BoxTree::missing_glyphs`。A31）を持つ。**絵には影響しない**
  （paint は読まない）が、api が `Warning` にし、`dump_json()` が出す
- **block**: 幅は親から降り、高さは子から戻る。`width: auto` は利用可能幅いっぱい。
  `margin: 0 auto` の中央寄せ。兄弟間のマージン相殺（A10）。子が inline と block の混在なら
  inline の連続を無名ブロックで包む
- **inline**: インライン整形文脈ごとに、(a) 空白の畳み込み（A14）→ (b) **シェーピング属性**が
  同じ区間ごとに `TextMeasurer::shape()`（色・letter-spacing・line-height の境界では切らない。A27）
  → (c) クラスタを `linebreak::Item` に変換（装飾・行高と**行分割ポリシー**はクラスタ先頭の
  文字のものを対応付け（A27 / A28）、letter-spacing を advance に加算、`<br>` は ForcedBreak、
  `<img>` とルビのまとまりは Atomic）
  → (d) `LineBreaker::break_lines()`（`Config` は段落の既定値。`line-break` / `overflow-wrap` は
  アイテムごとの値が勝つ。A23 / A28）→
  (e) 行ボックスを積み、`Spacing` と `text-align`（justify を含む。A13）を反映してグリフを配置。
  `TextFragment` は「同じ FontId・サイズ・色・sideways の連続」で切る（1 回の shape 結果を、
  装飾の境界とフォールバックの境界で複数の断片に切る。位置は動かない）。
  行の高さは行内の各断片の `line-height` の最大、ベースラインは半行間（half-leading）で決める。
  (b) のあと、豆腐のクラスタ（`ShapedCluster::missing`）を文字ごとの属性の表の位置の層と
  突き合わせて `LayoutEngine` に記録する（A31。段落が何度組まれても重複しない）。
  ファイルは段の境界で分けてある（A27 の末尾）。(a)〜(c) の結果 `PreparedParagraph` は
  行の幅に依らないので、固有寸法の計測と実際の配置で同じものを使える
  （`inline_intrinsic()` の min-content / max-content にもアイテムごとのポリシーが効く）
- **flex**（Phase 6）: 単一行のみ（`flex-wrap` は対応外）。CSS Flexbox §9 のアルゴリズムのうち、
  flex-basis の解決 → grow / shrink の配分（min-content を下限に）→ 交差軸の整列 → justify-content → gap。
  アイテムの max-content / min-content は `kUnbounded` と `min_content_width()` で測る。
  column のアイテムの主軸サイズ（高さ）は「交差軸の幅を決めたうえで部分木を組んでみる」で出すが、
  その**計測**は `LayoutEngine::measure_block_size()` を通してメモする（A29。組み直していたのが
  issue #5 の 2^depth）。配置の `layout_block()` は従来どおり毎回 1 回ずつ実行するので、
  座標は 1 ビットも変わらない。固有寸法（`content_intrinsic()`）と準備済み段落も同じメモに乗る
- **ルビ**（Phase 7）: `<ruby>` 内の「親文字の並び + `<rt>`」を 1 組とし、組ごとに 1 つの Atomic。
  幅は max(親文字, ルビ)、短い方を中央に置く。行ボックスはルビのぶん block-start 側に広がる
- **縦書き**（Phase 8）: 論理座標のまま。`TextStyle::direction = Vertical` で測るだけ
- **計算量**: 1 つの IFC を組む仕事は、アイテム数 N に対して線形。行ごとに段落全体を舐めたり、
  段落全体ぶんの作業バッファを確保したりしない（A22）。flex の入れ子は深さ d に対して d²
  （A29。メモがないと 2^d）。守れているかは計測カウンタ（A21）で
  検査する（`tests/layout/complexity_test.cpp`）。時間ではなく回数で見る
- テストは偽の `TextMeasurer`（全角 1em / 半角 0.5em、ascent 0.88em / descent 0.12em）で
  フォントなしに書き、`dump_json()` の座標を検証する（DESIGN.md §10-3）

### 3.9 paint（⑤a）

`raster::DisplayList build_display_list(const layout::BoxTree&)`。木を前順に辿り、
背景 → 枠線 → 子 の順で命令を出す。論理 → 物理の変換（A1）はここだけで行う:
横書きは `x = inline, y = block`、縦書き（vertical-rl）は `x = viewport_width − block − block_size, y = inline`。
同じフォント・サイズ・色・sideways が連続するグリフは 1 つの `DrawGlyphs` にまとめる
（`DrawGlyphs` の切れ目は**描き分けの都合だけ**で決まる。グリフの位置は layout が 1 回の
シェーピングから決めていて、色でいくつに分かれても動かない。A27）。
完全に透明な塗りは命令を出さない。`dump_json(const DisplayList&)` と、デバッグ用の
`dump_svg()`（DESIGN.md §2「SVG はデバッグダンプに格下げ」。グリフは矩形で代用してよい）を持つ。

### 3.10 api / CLI

公開ヘッダは `include/shashoku/`（`shashoku.hpp` が全部を include する）。DESIGN.md §8 の
シグネチャに、A12 の `ImageSet` を取るオーバーロードと、`--dump-stage` 用の
`dump(html, fonts, images, opts, Stage)` を加える。公開ヘッダに内部の型（FreeType、`src/` の型）を
漏らさない。`FontSet` / `ImageSet` はバイト列を保持するだけで、解釈は `render()` の中で行う。

**共有資源の経路**（A34）: `LoadedFonts::prepare(FontSet)` / `LoadedImages::prepare(ImageSet, limits)`
で解釈・デコードを 1 回だけ済ませ、`render(html, LoadedFonts[, LoadedImages], opts)` と
`dump(html, LoadedFonts, LoadedImages, opts, stage)` に渡す。どちらも pimpl で、FreeType /
HarfBuzz / `src/` の型は名前も出さない（`tests/api/public_header_check.cpp` が
「include パスを `include/` だけに絞ったターゲット」で機械的に検査する）。

資源の用意のしかたの違いは `src/api/render.cpp` の `ResourceSource` に閉じ込め、パイプライン本体は
1 本のまま。**検査の順序はどちらの経路でも同じ**なので、同じ入力からは同じエラーが同じ順で出る。
従来の `render(html, FontSet, ImageSet, opts)` は、パイプラインの同じ位置で `prepare()` を呼ぶ
薄い包みになった（`ImageSet` の名前重複の検査も `LoadedImages::prepare()` に移った）。
用意済みの画像には `opts.limits` を掛け直す（枚数は (a) の位置、画素数は (c) の位置）。
ムーブ済みの `LoadedFonts` / `LoadedImages` を渡したら `InvalidOption`。

出力サイズ: 幅 = `viewport_width`、高さ = `viewport_height`、未指定ならルートの内容の高さの切り上げ
（0 なら `InvalidOption`）。どちらも `scale` を掛けて切り上げる。豆腐は `Warning` として返す:
`BoxTree::missing_glyphs`（③ が入力位置の昇順 → コードポイントの昇順に並べたもの。A31）を
そのまま写し、`detail` の末尾に `to_string(RenderError)` と同じ書式で ` at L:C` を付ける。
api は並べ替えない（順序を決めるのは ③ の仕事）。CLI は `warning[missing-glyph]: <detail>` を
stderr に出す。

`validate(options)` は寸法・`scale`・`compression_level`（0〜9。A33）を見る。**オプションの誤りは
HTML を読む前に返す**（壊れた HTML でも `InvalidOption` が先に出る）。`png::encode` も同じ範囲を
自分で検査するが、api はそこに頼らない。

入力の上限（A25）は api が一手に引き受ける。順序は
`validate(options)` → (a) `check_input_limits` → `html::parse` → (b) `check_dom_limits` →
`style::resolve` → (b) `check_computed_limits` → `load_resources`（(c) 画像）→ … → `rasterize`（(c) 出力）。
`check_dom_limits` は DOM を、`check_computed_limits` はスタイル付きツリーを、それぞれ明示スタックで
1 回だけ前順に辿る（layout には手を入れない）。`png` / `raster` / `html` / `style` へは引数で渡す。
各モジュールの既定値と `RenderLimits` の既定値が一致することは `static_assert` で検査する。
公開関数の境界には `std::bad_alloc` / `std::length_error` の `catch` を置く（A26。`catch` があってよいのは
`src/api/out_of_memory.hpp` だけで、`render()` / `dump()` / `prepare()` の全オーバーロードが使う）。
CLI に上限を変えるフラグは足していない（既定値のまま使う）。

CLI は `tools/shashoku/`: `shashoku input.html --font A.otf [--font B.ttf …] [--image name=path …]
-o out.png [--width N] [--height N] [--scale S] [--compression 0-9]
[--overflow oidashi|oikomi|burasage] [--dump-stage dom|style|box|display-list|svg]`。
エラーは `to_string(RenderError)` を stderr に出して終了コード 1。
値の範囲の検査は `render()` に任せる（オプションの正は 1 か所。CLI は形だけを見る）。

---

## 4. テスト

- 単体テストは `tests/<module>/`。テスト実行ファイル名は `<module>_test`
- ゴールデンテストは `tests/integration/`（`render()` を通した end-to-end）。期待画像は
  `tests/golden/*.png` にコミットし、デコードしたピクセルの完全一致で比較する。不一致なら
  `build/<preset>/test_output/` に actual / expected / diff の 3 枚を書き出す（DESIGN.md §10-1）
- 期待画像の追加・更新は、必ず画像を目で見て正しいと確認してから行う
- メモリを触るモジュール（png, raster, text, html, style）は `asan` プリセットでもテストを通す
- ファジング（DESIGN.md §10-4）は、乱数の種を固定した「ランダム入力の性質テスト」として
  通常のテストに含める（html: 落ちない / linebreak: §3.4 (6) の不変条件）

**性質テスト**（issue #10-2）。ゴールデンは「以前と同じ絵が出る」ことしか見ないので、
「書き方を変えたら結果が変わってしまう」たぐいの壊れ方は捕まえられない（#2 / #8 がそれだった）。
次の 2 つの入力を組んで結果を突き合わせる形で、設計判断そのものをテストにする:

| ファイル | 固定する性質 | 根拠 |
|---|---|---|
| `tests/layout/writing_mode_property_test.cpp` | 横書き（幅 W）と縦書き（高さ W）で論理座標のボックスツリーが一致する | A1 |
| `tests/integration/scale_property_test.cpp` | ボックスツリーとディスプレイリストが scale に依らない／整数座標の矩形は @2x で厳密に 2x2 ピクセルになる | A8 |
| `tests/layout/invariance_test.cpp` | 素の `<span>` で包む・テキストノードを切る・既定値を明示的に書く・空白の畳み込みを 2 度かける、が組版を変えない | A27 / A14 |
| `tests/layout/property_test.cpp` | 計測のメモの有無で結果が変わらない | A29 |
| `tests/integration/shaping_test.cpp` / `line_policy_test.cpp` / `tests/layout/inline_test.cpp` | 装飾だけの `<span>` でグリフ位置が動かない／ブロックに書いた継承プロパティと span に書いたものが一致する | A27 |
| `tests/integration/determinism_test.cpp` / `tests/png/determinism_test.cpp` | 決定性（A32） | A32 |

**書字方向で一致しないもの**（上の 1 つめのファイル冒頭に全部書いてある。Chrome 比較の
「意図した差」の候補にもなる）: ベースライン（縦書きでは行の中心軸）／`sideways` と、それによる
断片の切れ目／`<img>` の行内での揃え方（横書きはベースライン揃え、縦書きは中心軸に中央揃え）と
固有寸法が物理であること／行に font-size の大小が混ざるときのインライン背景の block 方向／
ルビの行高の float 1 ulp。
