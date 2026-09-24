# shashoku 実装設計書

[DESIGN.md](DESIGN.md) が「何を・なぜ作るか」、この文書が「どう作るか」。
DESIGN.md のスケッチを実装可能な粒度まで具体化し、その過程で下した設計判断を記録する。
**両者が食い違ったら、§1 の判断記録に理由が書いてある限りこの文書が優先**（書いていなければバグなので直す）。

モジュール間の契約は文章ではなくヘッダで固定してある。まずそれを読むこと:

| 契約ヘッダ | 結ぶもの |
|---|---|
| [include/shashoku/error.hpp](../include/shashoku/error.hpp), [src/core/result.hpp](../src/core/result.hpp) | 全モジュール共通のエラー型 |
| [include/shashoku/source_location.hpp](../include/shashoku/source_location.hpp), [include/shashoku/warning.hpp](../include/shashoku/warning.hpp) | 入力位置と警告（エラーと共有。A46） |
| [src/core/diagnostics.hpp](../src/core/diagnostics.hpp) | ① html / ② style → api: 集めた診断（A46） |
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
空なら「空白」として通ってしまう。**これは `FontStore` が load のときに見つけて `Shaper` が豆腐に
回す**（A43。issue #27）。ラスタライザの契約はこの表のまま変えていない: 「輪郭が 0 本のグリフ」は
ラスタライザから見れば空白グリフと区別がつかないので、**描く前の段（④ の入口）で判定する**。
`SVG `（OT-SVG）だけを持つカラーフォントも同じ穴だが、**同じ扱い（警告 + 豆腐）にする方針だけを
決めて実装は別の作業に回した**（A43 の最後）。

**A20. Unicode の表は UCD から生成し、生成物をコミットする。** 行分割クラス（UAX #14）・
縦書きの字の向き（UAX #50）・東アジア幅（UAX #11）の 3 つの表は
[scripts/gen_unicode_tables.py](../scripts/gen_unicode_tables.py) が、**版と SHA256 を固定した**
UCD から生成する。生成物（`src/*/[a-z_]*_table.inc`）はコミットするので、ビルドに Python も
ネットワークも要らない（製品コードとテストは実行時に UCD を読まない。DESIGN.md §3-5）。
生成物の先頭に生成元の版・ハッシュ・スクリプト名が入るので、3 つが同じ版から作られていることは
ファイルを見れば分かる。`--check` で「再生成した結果がコミット済みの表と一致するか」を検査できる。
手で足した例外（クラスの寄せ先、既定値のブロック）は結果の範囲ではなく**規則**としてスクリプトに書く。
更新手順は [docs/UNICODE_TABLES.md](UNICODE_TABLES.md)。

**2026-09-22 追記（issue #24）: 全角表の「据え置き」をやめた。** 表を生成に切り替えた issue #11 では、
それまで手で起こしてあった `kWideRanges`（Unicode 15.1 相当）と振る舞いを変えないために、生成の最後に
`LEGACY_WIDE_DEVIATIONS` 32 件を当てて結果を 15.1 に戻していた。これは「結果の範囲での上書き」そのもので
この判断記録に反しており、しかも `--check`（生成元との機械的な一致）が永久に落ちたままになる。
32 件を調べると「この文字は据え置くべきだ」という判断は 1 件もなく、25 件は 16.0〜18.0 の版上げの
取りこぼし、7 件は手起こしの誤り（写し漏れ 3・未割り当ての穴をまたいだ範囲 4）だった。実害として
U+1B155（小書きカタカナ「コ」）が半角扱いになり、A14 の畳み込みで和文に空白が入っていた。
**例外リストを削除し、UCD 18.0.0 そのままの表にした**（ゴールデン 16 枚と `examples/` の出力は不変。
32 区間のコードポイントがどこにも出てこないため）。

版上げ手順への影響: 全角表も他の 2 つと同じく「再生成して差分を読むだけ」になり、`--check` が
**終了 0 の状態を保つべき検査**になった。**CI の lint ジョブがこれを走らせる**（UCD は
`build/ucd/<版>/` にキャッシュし、検査は `--offline`。キャッシュが外れたときだけ `--fetch` で取りに行く）。
以後、表を手で編集したり、`UCD_VERSION` を上げて再生成を忘れたりすると CI が落ちる。

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
`std::optional<Wrap> wrap`（当初は `std::optional<bool> break_anywhere`。A35 で 3 値にした）を
持たせ、nullopt なら `Config` の値を使う（すべて nullopt なら出力は従来と完全に同じ）。
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
- **`wrap` の緊急分割は、位置の両側のアイテムがともに `Normal` 以外のときだけ許す。**
  つまり anywhere / break-word を指定した要素の内部でだけ割れ、要素の境界では割れない。
  指定していない語が隣接のせいで割れるより、指定した範囲だけが割れる方が説明しやすい。
  発動条件（分割可能位置が 1 つもない行でだけ。§3.4 (5)）と優先順（分離禁則 >
  行頭・行末禁則、クラスタ内部では割らない）は `Config` のときと同じ。両側が `Normal` 以外の
  位置が 1 つもなければ、A4「禁則 > 幅」で割らずにはみ出す
- ~~`break_anywhere` は `min_content_width()` に影響しない（`Config` のときからの挙動。CSS の
  `break-word` 相当）。CSS Text 3 §5.4 は `anywhere` を min-content に効かせると定めているが、
  現行の `Config::break_anywhere` は `anywhere` と `break-word` を 1 つのフラグにまとめているので
  区別しない。区別が要るようになったら `Config` の意味論の問題として別に決める~~
  → **A35 で撤回**（issue #18）。この割り切りのせいで `overflow-wrap: anywhere` を指定した
  flex アイテムが親からはみ出していた。いまは `Wrap` の 3 値を持ち、`Anywhere` だけが
  `min_content_width()` に効く

layout 側がこの口に何を入れるかは A28。`Config` は**段落の既定値**として残っていて、値を持たない
`Item` にだけ効く（約物のアキ・あふれ処理など、`strictness` / `wrap` 以外の設定は
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
| (b) | パース・計算値化のあと（api が DOM とスタイル付きツリーを 1 回ずつ辿る） | `nesting_depth` / `dom_nodes` / `text_code_points` / `style_rules` / `font_size_device_px` / `length_px`（A36） / `scale` |
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
| `length_px` | 2^24 = 16,777,216 | 長さ・座標の絶対値（A36）。出力の絶対上限（1 辺 2^32-1 px）より十分小さく、`dom_nodes` = 20,000 段ぶん足しても 3.4x10^11 で float の上限 3.4x10^38 に遠く届かない。float が整数を 1 刻みで表せる上限でもある |
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
| ルビ組（Atomic） | 親文字の**先頭の文字**が属する要素の計算値（幾何には使わない。A37） |
| `<br>`（ForcedBreak） | その `<br>` 自身の計算値（必ず改行するので結果には効かない） |

- `line-break: auto` はエンジンの既定に解決する（A17）。`overflow-wrap` は 3 値をそのまま
  `linebreak::Wrap`（`Normal` / `BreakWord` / `Anywhere`）に写す。どちらも緊急分割を許すが、
  min-content に効くのは `anywhere` だけ（A35。CSS Text 3 §5.4）
- **ルビ組の内部（親文字の途中・`<rt>`）の指定は効かない。** 組は Atomic 1 個で、その内部には
  分割可能位置が存在しないため（§3.8 のルビ）。「指定を読み落としている」のではなく
  「効かせる場所がない」。1 文字だけの要素に `line-break` を書いても何も起きないのと同じ。
  **これは行分割ポリシーの話に限る。** 組の代表の文字から幾何の寸法（ascent / font-size /
  letter-spacing）を取ってはいけない（#16 / #17 で実際に壊れていた。A37）
- **`Item` に入れるものはポリシーだけではない**（A44 で 1 つ増えた）: ルビ組には
  「前後の文字にはみ出してよい量」（`overhang_before` / `overhang_after`）も入れる。
  こちらは代表の文字ではなく**隣のアイテムの文字**と組の寸法から決まるので、
  アイテムを全部作ったあとの後処理で埋める（`resolve_ruby_overhang()`）
- **`linebreak::Config` は段落の既定値として残す。** いまは (c) がすべての `Item` に値を入れるので
  `strictness` / `wrap` については使われないが、約物のアキ・あふれ処理（`overflow` /
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
| 豆腐（どのフォントでも描けない） | 成功 + `ShapedCluster::missing = true` と `missing_reason`（A43。DESIGN.md §3-6 の唯一の例外） |
| `FontStore` にフォントが 1 つもない、不正な `FontId`、**負**または非有限の `font_size` | `Internal`（呼び出し側のバグ） |
| `hb_font_get_h_extents()` が偽（hhea / OS/2 が読めない） | `FontLoad` |
| `hb_font_create()` が空のフォントを返す、`hb_buffer_allocation_successful()` が偽 | `OutOfMemory`（A26 の種類） |
| グリフ数が 0 でないのに HarfBuzz がグリフ情報を返さない | `Internal` |

- **`hb_buffer_allocation_successful()` は `hb_buffer_create()` 自体の失敗も捕まえる**
  （確保に失敗すると `successful = false` の空のバッファが返るため）。だから
  コンストラクタが `Result` を返せなくても、確保失敗を黙って握りつぶさずに済む
- **フォントが 1 つもない `FontStore` はエラーにした。** それまでは「存在しない FontId 0 の
  `.notdef` を全文字ぶん返す」で通っていて、失敗するのはラスタライズの段（A19 の `Internal`）だった。
  api は空の `FontSet` を `NoFonts` で弾いているので、ここに来るのは呼び出し側のバグ
- `font_size` が **0** のときは送り 0 で成功する（CSS Fonts 4 §2.5 の `font-size: 0` は正当な指定で、
  A19 の `pixel_size` と違ってラスタライザには渡らない）。**負**と非有限を `Internal` にする
  （負は issue #26 で足した。style が `font-size: -16px` を宣言の位置つきで止めているので
  入力からは到達しないが、注入点の契約としては穴だった。A36 の最後を見よ）
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

**A35. `overflow-wrap` は `linebreak` でも 3 値で持つ。`anywhere` だけが `min_content_width()` に
効く。緊急分割の条件と禁則の優先順は変えない。**（issue #18。A23 の最後の割り切りを撤回する）

CSS Text 3 §5.4 は 2 値を **min-content に効くかどうか**で区別している:

> **anywhere**: An otherwise unbreakable sequence of characters may be broken at an arbitrary point
> if there are no otherwise-acceptable break points in the line. …
> **break-word**: As for `anywhere` except that soft wrap opportunities introduced by `break-word`
> are **not** considered when calculating min-content intrinsic sizes.

min-content は flex アイテムの自動最小サイズ（Flexbox §4.5。`min-width: auto` の content size
suggestion は主軸の min-content サイズ）に使われるので、1 つの bool に潰すと
`overflow-wrap: anywhere` を指定した flex の子が「1 行ぶんの幅」より縮まず、親からはみ出す
（実測: 親 32px に子 82.21875px。同じ内容をブロックに置けば正しく 3 行になるので、
**flex を通したときだけ指定が効かない**）。DESIGN.md §3-6「fail loudly」にも触れる:
指定は受理され、エラーも警告も出ないまま黙って無視されていた。

決めたこと:

- **`linebreak` に CSS の 3 値に対応する `enum class Wrap { Normal, BreakWord, Anywhere }` を置き、
  `Config::wrap` / `Item::wrap` にする。** `style::OverflowWrap` は持ち込まない
  （`linebreak` は何にも依存しない。DESIGN.md §3-4）。値は**弱い順**に並べ、位置の両側の
  アイテムの弱い方がその位置で何ができるかを決める（A23 の境界の規則を 3 値に読み替えただけ）
- **`break_lines()` は 1 ビットも変えない。** 緊急分割の発動条件（分割可能位置が 1 つもない行
  でだけ）、候補の判定（クラスタ境界。`anywhere_candidate()`）、位置選びの優先順
  （分離禁則 > 行頭・行末禁則 > 最後の逃げ場）はそのまま。変えたのは `min_content_width()` の
  区間の切り方だけで、`BreakWord` と `Anywhere` は実配置では完全に同じ振る舞いをする
- **`min_content_width()` は、分割可能位置に加えて「両側がともに `Anywhere` のクラスタ境界」でも
  区間を切る。** 切ってよい位置の判定は緊急分割と同じ `anywhere_candidate()` を使う
  （クラスタ内部・ZWJ の吸収では割らない）
- **禁則は min-content では守らない。** §5.4 は "broken at an arbitrary point" としか言わず、
  禁則を守れとは書いていない。緊急分割には「守れる位置が 1 つもなければ破る」という最後の
  逃げ場があるので、こう定義しても **`min_content_width()` の不変条件「この幅なら必ず収まる」は
  保てる**（`tests/linebreak/property_test.cpp` の `MinContentWidthNeverOverflows` と
  `min_content_test.cpp` の `TableWidthsAreAchievable` で検査する）。禁則の優先順そのものは
  緊急分割のまま変えない
- **`max_content` は変わらない。** `kUnbounded` では緊急分割の経路を通らない
- **メモ化（A29）の鍵は変えない。** `IntrinsicKey` は `SubtreeId`（`ComputedStyle` のポインタを
  含む）+ `%` の基準なので `overflow-wrap` の違いは既に鍵に入っている。`PreparedParagraph` も
  ポリシーを `Item` に持つ。計算量も O(N) のまま（区間が細かくなるだけ。`anywhere_scan` に計上する）
- **採らなかった案**: `bool` を残して `anywhere_in_min_content` をもう 1 本足す形。差分は小さいが、
  bool 2 本の組み合わせに意味のない状態（緊急分割は不可・min-content には効く）ができて
  契約が読みにくくなる
- **範囲外**（この判断では直さない）: `min-width` の対応、
  `anywhere_candidate()` が `Item::no_break_before` を見ないこと（rank 3/4 の候補にはなるので
  実害はないが、緊急分割の候補判定としては見るのが筋）

**A35 への追記（issue #25。legacy name alias）**: 範囲外に置いていた `word-wrap` を入れた。
CSS Text 3 §5.4 は *"For legacy reasons, UAs must treat `word-wrap` as a legacy name alias of the
`overflow-wrap` property."* としており、**alias は「未対応の機能」ではない**。写し先の 3 値は
実装済みなので、止めても利用者には代替の組版が手に入らず、「黙って違う絵を出さない」という
fail loudly（DESIGN.md §3-6）の目的を果たしていない。旧来の日本語ページはほぼ必ず
`word-wrap: break-word` を書くので、試用版（#20）で最初の 1 枚が通らない典型例になる。

- **やり方は「名前の表に 1 行足す」だけ**（`value_parser.cpp` の `kPropertyNames` に
  `{"word-wrap", PropertyName::OverflowWrap}`）。`Declaration::property` が
  `PropertyId::OverflowWrap` になるので、カスケード（同一ブロックでは後勝ち）・継承・
  `inherit` / `initial`・計算値・`--dump-stage style` の出力名は、すべて `overflow-wrap` と
  **完全に同一の経路**を通る。CSS Cascade の「パース時に新しいプロパティへ変換する」を満たす
- **入口で名前を正規化する案（別表 `kPropertyAliases`）は採らない。** エラーの文面まで
  `overflow-wrap` に寄り、著者が書いていない名前をエラーに出すことになる。CSSOM を持たない
  shashoku では旧名が見える場所は**値のエラーの文面だけ**なので、そこは著者の綴りを残す
  （`` `word-wrap: foo` is not supported (…) ``。種類と位置は従来どおり）
- **`grid-row-gap` / `grid-column-gap` / `grid-gap` の別名は入れない（決定）。** CSS Box
  Alignment 3 §8.4 が同じ "legacy name alias" を課しており写し先も対応済みだが、CLAUDE.md の
  一問「日本語の文章を正しく組むことに寄与するか」に対し `word-wrap` は Yes（旧来の日本語
  ページの標準的な書き方）、`grid-gap` は No（shashoku に grid は無く、flex に `grid-gap` と
  書く動機がない）。代わりに `kPropertyHints` に 3 行足して写し先を案内する
  （`` `grid-gap` is not a supported property `` + hint `` legacy name: use `gap` ``。
  A46 より前は hint を message の末尾に括弧で足していた。A48 で `RenderError::hint` に移した）
- **出力は 1 ビットも変わらない。** `examples/` とゴールデン 16 枚の入力に `word-wrap` は無く、
  `overflow-wrap` の経路自体は触っていない（`release` の CLI で修正前後の
  `--dump-stage style` / `box` と PNG がバイト一致することを確かめた）

**A36. 各段は「自分が出す数値が有限で上限以内であること」を保証する。A25 の「入力の
個数・サイズ」とは別の保証として並べる。** `padding: 1e38em` を渡すと `em x font-size` が
float をあふれて `inf` になり、style も layout も paint も何も言わないまま、raster が
「非有限な寸法のコマンドは無視する」（§3.3）で捨てていた。結果、**その要素だけが絵から
消えた PNG が終了コード 0 で返る**（警告 0 件）。DESIGN.md §3-6「fail loudly」に反する。
`em` の乗算を通る 12 プロパティのうち、止まっていたのは `font-size` だけだった（issue #19）。

単独の条件分岐の不足ではなく**段の契約の抜け**である。A25 の `RenderLimits` が保証して
いたのは「入力の個数・サイズ」で、「計算結果が有効であること」はどの段も保証していなかった。
A25 を書き換えるのではなく、別の保証として並べる。

**「有限」だけでは足りない**ことは実測が示している: `padding: 3e38px` は style では有限で、
layout の加算で `inf` になる。そこで `RenderLimits` に**長さ・座標の絶対値の上限**
`length_px`（既定 2^24 = 16,777,216 px。根拠は A25 の表）を新設し、「有限かつ上限以内」を
要求する。上限は入力の一部なので純粋関数の性質は壊れない（A25 と同じ理屈）。
検査に使うのは比較と `isfinite` だけなので A9 の許可リスト内。

決めたこと:

- **② style の出口**（実装済み）: `resolve_length()` / `resolve_dimension()` /
  `resolve_line_height()` が `Result` を返し、宣言の位置つきで `LimitExceeded` にする。
  カスケードのあとに計算値をもう一度まとめて検査する（`check_computed_lengths()`）ので、
  継承で入ってきた値と、プロパティを足したときの掛け忘れもここで捕まる。
  `line-height` の倍率は倍率のまま継承するため、「倍率 x **その要素の** font-size」は
  この段でしか見られない（親で収まっていても子の font-size で超えうる）。
  `<img>` の `width` / `height` 属性も layout に渡る長さなので同じ上限で見る
- **`font-size` は `length_px` の対象外。** A25 の `font_size_device_px` が scale 込みで
  より厳しく見ており、要素の位置で報告している。二重に検査すると、同じ入力のエラーの位置が
  宣言の側に移るだけで得るものがない。ただし**非有限な font-size は style が止める**
  （`em` の基準が壊れたまま残りのプロパティを解決すると、原因ではないプロパティを指す
  エラーが出るため）。種類と位置は従来どおり `LimitExceeded` + 要素の位置
- **③ layout の出口**（実装済み。`check_geometry()`）: `%` と flex の比は style では判定できない
  （A5: 包含ブロックが要る）。`width: 1e38%` は同じ HTML でもビューポート幅で結果が変わっていた
  （200 px なら `2e38` で有限、1000 px なら `inf`）。そこで `layout()` の最後に BoxTree を
  前順に 1 回辿り、**座標・寸法・行・断片・グリフ位置がすべて有限かつ上限以内**であることを
  確かめる。違反は `LimitExceeded` + **その箱の入力位置**（断片なら断片の位置）。
  費用は O(N) で paint の走査 1 回ぶん
- **layout の上限は `length_px x dom_nodes`（既定 2^24 x 20,000 = 3.36x10^11）にする。**
  座標は長さの足し算なので、`length_px` をそのまま使うと**常識的な文書が落ちる**
  （高さ 1000 px のブロックを 20,000 個積むと 2x10^7 で 2^24 を超える）。一方で「有限であること」
  だけでは足りない（`width: 1e38%` @200 の `2e38` が通ってしまい、**幅によってエラーになったり
  ならなかったりする**）。そこで**新しい制約を足すのではなく、既存の 2 つの上限から導く**:
  1 要素あたりの長さが `length_px` 以内で要素が `dom_nodes` 個以下なら、どれだけ足し込んでも
  この値を超えない。つまり**②の検査を通った文書がこの上限で落ちることはありえない**。
  落ちたということは、`%` の解決や flex の比のように「入力の長さに比例しない計算」が
  壊れたということで、それこそが報告すべき事故。`RenderLimits` に新しいフィールドは足さない
  （api が同じ式で計算して `layout::Options::max_geometry_px` に渡す。導出が 2 か所で
  別々に動かないよう `static_assert` で固定）。float の上限 3.4x10^38 からも 27 桁離れている
- **`BlockBox` に入力位置を足した**（`TextFragment::location`（A31）と同じ考え方）。
  出口の検査が「どの要素の座標が壊れたか」を言うために要る。`--dump-stage box` にも出す
  （ダンプできない中間表現を作らない。DESIGN.md §3-3）。**paint は読まないので絵は 1 ビットも
  変わらない**（`examples/*.html` の PNG と display-list ダンプがバイト一致することで確かめた）
- **flex の比の計算そのものは直さない。** `factor / factors.scaled` が `inf/inf` = NaN になる式は
  `distribute_once()` の中にあるが、そこを `Result` にしても保証は強くならない。NaN が入る先は
  そのアイテムの箱なので、出口の検査でも位置は同じ精度で付く。検査を個々の計算に散らさず
  段の出口に 1 か所置いたのは、**あとから計算を足したときに検査を書き忘れても
  不変条件が破れない**ようにするため
- **`ErrorKind` は `LimitExceeded` に一本化する。** パーサは以前 `1e39px`（float にできない数値）を
  `UnsupportedValue`（the number is out of range）で返していたが、`1e38em` が `LimitExceeded` に
  なると**ほぼ同じ入力が別の種類**になる。利用者から見てこの区別は説明しづらい。まだリリース前で
  互換性のコストが小さいので、「数値が範囲外」は `LimitExceeded` に寄せた
  （**これは既存のエラーの種類を変える互換性の変更**）。`UnsupportedValue` は今までどおり
  「単位・キーワードが対応外」の意味だけに使う。`font-weight: 1e39` は「100..900 の値でない」
  なので `UnsupportedValue` のまま
- メッセージは A25 の流儀（どの上限を・いくつに対して・いくつだったか、`RenderLimits` の
  どのフィールドで緩められるか）。float で表せない値はその旨と掛け算の内訳を添える
  （`` `padding-left` computes to inf (1e+38em x font-size 16 px) … ``）
- **§3.3 の「非有限な寸法を持つコマンドは無視する」はそのまま残す。** ラスタライザの防御としては
  正しい（落ちない・UB を踏まない）。前段で止まるので到達しなくなるだけ。
  **却下した案**: raster で非有限を見つけたときに警告を出す。段としては最後で「どの入力が
  原因か」の情報がもう無く、fail loudly の「原因の入力位置つき」を満たせない
- 同じ理由で、**JSON / SVG ダンプが非有限を `null` / `0` に潰す**のも直していない。
  ②③ の出口で止まるので、ダンプに非有限が現れることがなくなった（`--dump-stage svg` が
  「PNG に描かれないグリフを原点に描く」食い違いも、ダンプ自体が出なくなることで消える）
- **自動高さ（`--height` なし）のときの「the content height is 0」**は、内容高さが `NaN` のときにも
  出ていた（`!(height > 0)` が NaN でも真になる）。③ の出口が先に止めるので到達しなくなったが、
  万一届いたら layout の不変条件が破れている = shashoku 側のバグなので、`Internal` で
  「有限でない」と報告する。本当に高さ 0 のときのメッセージは従来どおり
- **負の値はパース時（②の入口）に止める。有限性・上限とは別の検査**で、`margin` と
  `letter-spacing` だけが例外（`font-size: 0` は CSS Fonts 4 §2.5 で有効なので通す）。
  種類は `UnsupportedValue` + 宣言（属性）の位置で、「範囲外」の `LimitExceeded` とは別物
  （#19 でこの 2 つを分けた）。計算値の段で負になる経路は無い（`em` の乗算は「非負 x 非負」
  だけで `calc()` は未対応）ので、ここで弾けば後段は負を見ない。総当たりの表は
  `tests/style/error_test.cpp` の `NegativeValuesAreRejectedExceptForMarginAndLetterSpacing`。
  あわせて**注入点の契約**（`text::TextMeasurer`）にも「`font_size` は非負」を明記し、
  シェーパの入口（`check_contract()`）で `< 0` を `Internal` にした（issue #26）。
  style が止めているので入力からは到達しないが、偽の `TextMeasurer` やレイアウトのテストは
  この入口を直接叩くので、契約の文面と実装を合わせておく

**A37. ルビ組の「代表の文字」は行分割ポリシー専用。組の内部は通常のインライン内容として
組み、幾何は親文字の全クラスタから出す。**（issue #16 / #17）A28 は「ルビ組の
`linebreak::Item` の `line-break` / `overflow-wrap` に、親文字の先頭の文字の計算値を使う」と
決めただけなのに、`RubyPiece::base_style` が**計測・配置・行の高さの代表**まで兼ねていた。
そのため 2 つの壊れ方が同時に起きていた:

| 症状 | 原因 |
|---|---|
| `letter-spacing` が計測（行分割器に渡す送り）には入るのに、親文字の配置には入らない（#16） | 親文字だけが「装飾ごとの区間をグリフの送りで並べ、最後に字間をまとめて足す」別実装だった |
| 親文字の 2 文字目以降の `font-size` / `line-height` が行の高さに効かず、大きい文字が画像の外で切れる（#17） | 行の高さもルビの張り出しも、代表の文字 1 つから出していた |

決めたこと:

- **`RubyPiece::base` はクラスタの列**（`RubyCluster` = `ItemSource` + letter-spacing 込みの送り）。
  中身は通常テキストのアイテムと同じで、配置も同じ関数（`place_cluster()`）を通る。
  装飾ごとの「区間」という概念は捨てた（断片を切るのは `FragmentWriter` の仕事。A27）。
  これで**計測と配置が同じ数値を使う**ようになり、一方向パイプライン（DESIGN.md §3-1）の
  「前段の出した数値を後段が別計算しない」が組の内部でも成り立つ
- **幾何に使う値は名前で「最大」だと分かるようにする**（`max_base_ascent` /
  `max_base_font_size`）。行の高さは親文字の**全クラスタ**のスタイルで `extend_line_height()` を
  回して求める（CSS 2.1 §10.8。注釈側は行の高さに参加しない — CSS Ruby 1 §3.4）
- **`base_style` は行分割ポリシーの代表としてだけ残す**（A28 のまま）。`linebreak::Item::em`
  （約物のアキに使う値）も代表のまま: 幾何ではなく行分割器への入力だから
- **`<rt>` に `letter-spacing` は適用しない。** 和文のルビは親文字に対する配分で決まる
  （JLREQ 3.3「ルビ文字列の配置」）ので、親文字の字間がルビ文字の間にも入ると、ルビが
  親文字より広がって別の語のように見える。継承した値も明示した値も効かない。効かないことを
  テストで固定してある（`tests/layout/ruby_test.cpp` の `LetterSpacingInsideRtIsNotApplied`）
- **親文字の一部を覆う `background-color` は、通常テキストと同じ矩形を出す。** 組は
  `linebreak::Item` 1 個なので、アイテム単位の二分探索では組の内部で始まる / 終わるスコープを
  取りこぼしていた（黙って消えていた）。クラスタまで降りて解決する。組**ぜんぶ**を覆う
  スコープは従来どおり組の箱の矩形（中央寄せのずらしを含む）
- ルビ文字は「組の送り」の中央に置く（従来どおり）。親文字側の送りは末尾の字間を含むので、
  `letter-spacing` があるとルビの中心は親文字の**グリフの**中心より字間の半分だけ後ろに来る。
  これは親文字を 1 つのインラインボックスとして中央に揃えた結果で、通常テキストの送りの
  扱いと一貫している（Chrome との突き合わせは #15）

**A38. 既定フォントは CLI 層だけの機能。ライブラリはバイト列しか受け取らない。**（issue #20）
試用版の目的は「準備なしで 1 枚出せる」ことなので、`--font` を省けるようにする必要がある。
ただし `render()` / `dump()` にフォントの探索経路を持たせると、**同じ HTML から機種ごとに違う PNG**
が出うる（DESIGN.md §3-5 の純粋関数、§4「外部リソースを取りに行かない」に正面から反する）。
そこで既定フォントは `tools/shashoku/` に閉じ込め、公開 API（`include/shashoku/`）も
`render()` / `dump()` の署名も変えない（A12: 画像とフォントはバイト列で渡す）。

- **持ち方はバイナリへの埋め込み**（Noto Sans JP Regular + Bold、約 11.2 MiB）。
  `.incbin` で `.rodata` に置く（C の配列初期化子に展開すると 50 MB 超のソースになる）。
  実行ファイルの隣に置く方式は「バイナリだけコピーされる」と分かりにくく失敗する。
  **システムフォントの探索も実行時のダウンロードもしない**（上と同じ理由）
- **サブセット化はしない**。「任意の日本語文字列を流し込んでも組版が壊れない」が製品の約束
  （README 冒頭）なので、字種を削るとその約束を破る
- **`--font` を書けばそちらが優先**。既定フォントは黙って足されない（欧文フォントだけを
  渡したら和文は豆腐になり、警告が出る = 利用者が見ている集合と実際の集合が食い違わない）
- **順序は Regular → Bold**。`--font <Regular> --font <Bold>` と書いたときと
  **バイト単位で同じ PNG** が出ることを `Cli.default_font` が検査する（og_card で
  `font-weight: 700` の選び方まで見る）。この 2 本は同じ family で太さが違うだけなので
  順序を入れ替えても結果は変わらないが、約束として順序を固定しておく
- **版の固定**: 埋め込むフォントは `cmake/TestAssets.cmake` がコミット SHA と SHA256 で
  固定して取得する（テスト用フォントと同じ実体。`SHASHOKU_BUILD_TESTS=OFF` でも和文 2 本は取る）。
  **フォントの版が変われば同じ HTML から違う PNG が出る**ので、`shashoku --version` が
  shashoku・zlib・FreeType・HarfBuzz・既定フォントの版を出し、README の「保証しないもの」にも書いた（A32）
- 埋め込みは `SHASHOKU_EMBED_DEFAULT_FONT`（既定 ON）で切れる。OFF のときは `--font` が必須に戻る

**A39. 配布物は「C++ ランタイムだけ静的、glibc は動的」。完全静的リンクは却下した。
リンク方法は出力を変えない。**（issue #20。2026-09-21 にユーザーが決定）
release ビルドの動的依存は `libc++.so.1` / `libc++abi.so.1` / `libunwind.so.1` / `libm` /
`libgcc_s` / `libc` で、**素の Ubuntu に無いのは前の 3 つ（と `libgcc_s` の版）だけ**。
`-static-libstdc++ -static-libgcc` で C++ ランタイムを取り込めば、残る動的依存は glibc だけになる。
実測で NEEDED は `libm.so.6` と `libc.so.6` の 2 つになり、libc++abi と libunwind は
追加の指定なしで静的に入った（clang-18 + libc++ 18）。

- **完全静的（`-static`）は却下した。** NEEDED が消えてどの Linux でも動くのは魅力だが、
  glibc（LGPL-2.1+）を実行ファイルに取り込むことになり、LGPL が再配布に求める
  「受け取った人が別版の glibc と再リンクできる手段の提供」に触れうる。配布物のための
  法務上の負担を、試用版の段階で背負う価値は無いと判断した
- 代わりに**前提が 1 つ増えた**: 動的に要る glibc の版は「ビルド機の版以上」なので、
  **ubuntu:22.04 のコンテナでビルドする**（= glibc 2.35 以降が前提。Ubuntu 22.04 /
  Debian 12 以降）。**musl の環境（Alpine など）では動かない**。「どの Linux でも動く」とは
  言えなくなったので、README と配布物の README に前提を明記する
- **この前提は静かに壊れる**（新しいランナーでビルドすると、要求する glibc のシンボル版だけが
  上がって「古いディストリで GLIBC_2.39 not found」になる。絵もテストも変わらないので気づけない）。
  だから 2 つを機械で押さえる:
  1. **ビルドは必ず ubuntu:22.04 のコンテナの中**（`scripts/dist_container_build.sh` を
     `docker run ... ubuntu:22.04` で呼ぶ。CI の `dist` ジョブと release.yml の両方）。
     22.04 には clang-18 / libc++-18 が無いので apt.llvm.org の jammy-18 を足す。
     CMake は 22.04 の 3.22.1 で足りる（`cmake_minimum_required` と同じ）
  2. **`scripts/check_dist_binary.sh`** が NEEDED と**要求する glibc のシンボル版の最大**
     （既定の上限 2.35）を検査する。`readelf --dyn-syms` の `@GLIBC_x.y` を集めて最大を取る
     （`2.4 < 2.35` を正しく比べるため major / minor は整数で見る）。
     検査に入る前に「ELF の実行ファイルであること」を確かめ、`readelf` の失敗はそこで
     落とす（A45。以前は解析の失敗が「依存なし」に化けていた）
- workflow のシェルは `scripts/` に切り出してある（workflow に埋め込むと**ローカルで一度も
  動かせない**）。`check_dist_binary.sh` / `pack_dist.sh` / `release_notes.sh` は docker 無しで
  回せる。`dist_container_build.sh` は `SHASHOKU_SKIP_TOOLCHAIN=1` で apt の部分だけ飛ばせる
- **`cmake/CompilerOptions.cmake`（`-ffp-contract=off` など決定性のフラグ）は変えない。**
  変えるのは CLI のリンク方法だけで、浮動小数点の丸めには触れない。実測でも、`dist` と
  通常の release の CLI で examples 5 本 + 禁則 3 方式の PNG がバイト単位で一致した
- 保証範囲（A32）は「同じ版・同じ依存」で語っているので、**配布するのと同じ設定でビルドした
  バイナリでゴールデン 16 枚を通す**。`dist` プリセット（Release + `SHASHOKU_STATIC_RUNTIME`）を
  用意し、CI の `dist` ジョブと release.yml の両方で `ctest --preset dist` を回す。
  どちらのジョブも `readelf -d` の NEEDED が libc / libm / ld-linux 以外なら失敗する
- 対応環境は **linux-x86_64 だけ**。決定性を CI で検査しているのが x86-64 Linux の 2 つの
  ツールチェーンだけで、aarch64 では**ゴールデン 16 枚を検査していない**（= 絵を保証しない
  成果物になる）。macOS / Windows / aarch64 は Phase 9c
- **ライセンス表示**: 静的に取り込む libc++ / libc++abi / libunwind は
  Apache-2.0 WITH LLVM-exception なので `THIRD_PARTY_LICENSES` の 5 節に全文を転載した。
  glibc は取り込まないので転載しない
- サイズ（strip 前）: 通常の release（動的・フォント無し）3.1 MiB /
  `dist`（C++ ランタイム静的・フォント埋め込み）13.0 MiB（strip 後 12.3 MiB）。
  参考: 完全静的だと 14.0 MiB だった

**A40. Chrome との比較は「ページ内で測って DOM に書き出す」方式にする。判定は
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

**A41. ルビ組の中は JLREQ 3.3.6 の 1:2:…:2:1 で配る。組の送り `max(親文字, ルビ)` は
変えない。配分（3.3.6）とルビの掛け（3.3.8）は別々に入れられる。**
（issue #15 で起票 → issue #28 で書き換え）

§3.8 のルビの規則 1〜5 の根拠と、そこに至った経緯。**この番号は 2 回書き換わっている**ので、
古い版を読んだ記憶があるなら下の「取り消した理由づけ」を読むこと。

JLREQ 3.3.6: 「親文字の文字列の字間の空き量の大きさ 2 に対して、ルビ文字の文字列の先頭から
親文字の文字列の先頭までの空き量…を 1 の比率で空けると体裁がよい」。つまり n 個のクラスタに
余り E を配るとき **端 = E/(2n)、字間 = E/n**。同じ節の注として、**端の空きはルビ文字サイズの
全角を上限**とする（極端に短いルビで端だけが大きく開くのを避ける）。

- **組の送りは `max(B, R)` のまま**（規則 1）。配分は余りを「組の外側（前後の空き）」から
  「組の内側（字間）」へ移すだけなので、**行分割器に渡す `Item::advance` は 1 ビットも変わらない**。
  行分割位置・固有寸法・`Spacing`・行の矩形が動かないことをテストで固定してある
  （`LayoutRuby.DistributionDoesNotChangeTheLineBreaking` と
  `tests/integration/ruby_distribution_test.cpp`）
- **クラスタが 1 つなら中央**（規則 5 の但し書き）。配る字間が無いので 1:2:…:2:1 が定義できず、
  端の上限も掛けない（掛けると 1 文字のルビが組の頭に寄ってしまう）
- **配分で入れた空きは、手前のクラスタの背景が覆う。** `letter-spacing` の字間と同じ扱い
  （A37: 親文字の送りは末尾の字間を含む）。そうしないと親文字を分けて包んだ `<span>` の
  背景の間に隙間が開く。組の端に接するスコープが組の箱の端まで伸びるのは従来どおり
- **`text-align: justify` でも組の内部は広げない。** JLREQ 3.3.6 は「親文字群は、行の調整処理の
  際に字間を空ける処理をしてはならない」と定めている。Chrome は justify の行で組の内部にも
  均等割りの空きを入れる（実測: `<ruby>写植<rt>しゃしょく</rt></ruby>` で写 0・植 20）が、真似しない

**取り消した理由づけ（#15 で書き、#28 で誤りと分かったもの）。**

1. 「Chrome は CSS Ruby の既定（`ruby-align: space-around`）で短い方を長い方の幅まで**均等に
   広げる**」——**読み違い**。根拠にしていた数値（ケース 5 の親文字の字送り 20.00 px、
   ケース 3 の注記の字送り 30.92 px）は `Range.getClientRects()` が返す矩形、つまり
   **「割り当てられた枡」であって字送りではない**（同じことを chrome_compare.md §4-5 が
   書いているのに、§4-2 がその値を送りとして使っていた）。スクリーンショットのインクで
   測り直すと、Chrome 153 も**親文字・ルビとも ベタのまま中央**に置いていた。
   訂正は chrome_compare.md §4-2 に入れてある
2. 「掛けが無いまま親文字の側を広げても JLREQ には近づかない（広げたぶんが行全体を
   間延びさせる）ので、1:2:1 と掛けはまとめて入れる」——**誤り**。組の送りはもともと
   `max(B, R)` なので、配分しても行は 1 px も伸びない。まとめる理由が無かったので、
   **(a) 配分（この番号）→ (b) 掛け（A44）** の 2 段に分けた。予告どおり (b) は
   `linebreak::Item` に掛けてよい量を足し、行頭・行末で落とす形で入った

いまの shashoku と Chrome 153 の差は、**(1) 配分そのもの**（shashoku は JLREQ どおりに配り、
Chrome は中央に置く）と **(2) 掛ける相手**（shashoku は仮名だけ、Chrome は漢字にも掛ける。A44）
の 2 つ。どちらも shashoku が意図して違えている（JLREQ に従っている）。

なお、**注記を親文字の「送り」の中央に置く**（末尾の字間を含む。A37）点は Chrome と同じだった。
実測（ケース 1、`<ruby>ABC<rt>x</rt></ruby>`、32px・字間 16px）: 親文字の送りは両者とも
`[0, 108.906]`（末尾の字間 16px を含む）で、注記の中心も両者 54.45 px。
インクの中心（`C` の開始 72.48 + グリフの送り ≒ 92.9 の中点 ≒ 46.4 px）ではない。
**「送りの中央か、インクの中央か」は両エンジンとも送りの中央**。配分を入れたあとも、
配った結果は組の中で対称（前後の空きが同じ）なので、この性質は保たれている。

**A42. 文字を 1 つも持たないインラインボックスも、行の高さに参加する「文字のない支柱」
として (a) で記録する。**（issue #23）

CSS 2.1 §10.8 は「空のインライン要素も空のインラインボックスを作る。そのボックスは
マージン・パディング・ボーダーと line-height を持つので、**内容のある要素と同じように**
この計算に参加する」と定める。ところが (a) の `collect_element()` は、文字を 1 つも持たない
インラインボックスを**どこにも記録していなかった**。`CharStyleTable` は「文字ごとの属性の表」
なので、文字が 0 個なら載る口が無く、行の高さを決める `measure_line()` が見るものが無い。
指定は読めていて、エラーも警告も出さずに効かない（DESIGN.md §3-6 の fail loudly にも反する）。

- **A37 / #17 と同じ形の壊れ方だが段が違う。** A37 は「代表の文字 1 つでは親文字の 2 文字目
  以降の寸法が落ちる」で直したのは (c)、こちらは「クラスタが 0 個でも寸法を持つ」で直すのは
  (a) の収集。どちらも #21 の「1 つの値を 2 つの用途に兼用するのをやめる」の一例
- **仕組みは新しく作らない。** ブロックの支柱（strut）が「文字を持たない寸法が行の高さに
  参加する」仕組みをすでに持っているので、それにそろえる: `Collected` / `PreparedParagraph` に
  `EmptyInlineBox{style, char_pos, item}` の列を足し、`measure_line()` の先頭で支柱と並べて
  `extend_line_height()` を通す
- **どの行に参加するかの規則**: `char_pos` を**範囲に含むアイテムの行**（issue #30 で足した。
  下の段落）→ 無ければ `char_pos` 以降の最初のアイテムの行 → 無ければ
  直前（= 最後）のアイテムの行 → ただし最後のアイテムが**強制改行ならどの行にも参加しない**
  （`A<br><span></span>` は次の行を作らないので、参加する行が無い）。Chrome 153 の実測 8 ケース
  （issue #23 の表）をすべて満たす規則はこれ。**行の幅に依らないので (c) で決めておける**
  （= メモした準備済み段落で使い回せる。A29）。行ごとの仕事はカーソル 1 本で O(1) 償却（A22）
- **範囲で探す規則は issue #30 で足した。** 最初の版は「`char_pos` 以降の最初のアイテム」しか
  見ておらず、アイテムが 1 文字 = 1 クラスタである通常テキストでは正しかったが、
  **ルビ組は複数の文字を 1 アイテムにまとめる**（Atomic 1 個。A28）ので、組の内部の位置は
  組を飛び越えて次のアイテムに割り当てられ、**ルビの行ではなく後続の行**が高くなっていた
  （`<ruby>A<span style="font-size:80px"></span>B<rt>ab</rt></ruby>C` で、組の行 34.75 /
  `C` の行 115.84375。期待は 115.84375 / 23.171875）。範囲は `[char_begin, char_end)` で、
  1 文字のアイテムでは #23 の規則と同じ行になる（= 既存の実測 8 ケースは 1 件も動かない）。
  `char_begin` は狭義単調増加なので二分探索のままで、`char_pos` を含みうるのは
  「`char_begin` が `char_pos` 以下の最後の 2 つ」だけ（下の注）なので後退は定数回
- **ルビ組だけは範囲の両端を含む。** `<ruby>AB<span></span><rt>ab</rt></ruby>`（組の内部の末尾）と
  `<ruby>AB<rt>ab</rt></ruby><span></span>`（組の直後）は、(a) の出力では**どちらも
  `char_pos == 組の char_end`** になり区別できない（(a) は「どのルビ組の内部か」を記録していない）。
  CSS では前者は組の行、後者は改行位置なら次の行だが、区別する材料が無いので**組の側に寄せた**:
  `<ruby>` の中に書いた指定が黙って別の行に効く方が壊れ方として悪い（#30 そのもの）。
  区別が要るようになったら (a) に「囲んでいるルビ組」を持たせるのが筋（今は要らない）
- **行ボックスを作らないことは変えない**（アイテムが 0 個なら段落は行を持たない）。
  CSS 2.1 §10.8.1 の zero-height line box とも Chrome の実測とも一致している
- **`linebreak::Item` は増えない**（幅は 0）ので、改行位置・固有寸法（min-content / max-content）・
  グリフの位置は 1 ビットも変わらない。`examples/` 5 本とゴールデン 16 枚が無変更なことは実測した
- インライン要素の `padding` / `margin` / `border` は `unsupported-layout` で止まるので、
  §10.8.1 の「margin / padding / border が 0 でない空のインライン要素」の条件は考えなくてよい
- 背景は変わらない: 空のインラインボックスの `BackgroundScope` は `begin == end` なので
  (e) が矩形を出さない（CSS でも幅 0 なので何も塗られない）

**A43. 「グリフがある」と「単色で描ける」を分ける。色データだけのグリフ（COLR のベースが空）は
豆腐に回す。**（issue #27。A19 が書き残した穴）

`resolve_char()` は `has_glyph()`（cmap にあるか）だけで豆腐を決めていた。COLR/CPAL の
カラー絵文字は「ベースグリフ + 色レイヤーの列」で字形を表すので、**ベースの輪郭が空**のフォントでは
「グリフはあるが単色では描くものが無い」状態になる。単色の輪郭しか描かない shashoku では、
その文字が**警告も豆腐も出ないまま消える**（実測: 自作の最小 COLR フォントで `AAA` が
終了コード 0・警告 0 件・全ピクセル透明の PNG）。豆腐より静かに壊れるので DESIGN.md §3-6 違反。

- **判定は `FontStore::load()` のとき**に済ませる。`FontStore` は load のあと読み取り専用の
  共有資源（A34）で `FT_Face` を持たないので、**引くときに FreeType に聞くことはできない**。
  `FT_Face` がまだ生きている `make_entry()` の中で「色データを持ち、かつ輪郭が空」のグリフ番号を
  集めて `FontEntry::color_only_glyphs`（昇順・重複なし）に覚え、以後は二分探索で引く
- **条件**: 色データは HarfBuzz（`hb_ot_color_glyph_get_layers() > 0 || hb_ot_color_glyph_has_paint()`。
  COLR v0 と COLRv1 の両方）、輪郭は FreeType（`FT_Load_Glyph(FT_LOAD_NO_SCALE)` のあと
  `outline.n_contours == 0`）に聞く。A7（互いを知らない）はそのまま:
  両者に別々に聞いて `FontStore` の中で突き合わせる。**`hb_font_get_glyph_extents()` では判定できない**
  （HarfBuzz は COLR のレイヤーから extents を計算するので、空のベースでも 800×700 を返す）
- **フォント単位で色データが無ければ 1 グリフも調べない**（`hb_ot_color_has_layers()` /
  `has_paint()` が両方 false なら即やめる）。COLR を持たないフォント（Noto Sans JP / Noto Sans）は
  読み込み時間も出力も 1 ビットも変わらない。`FontStore::color_probe_count()` は**テスト用の統計**で、
  「調べていない」ことを 0 で固定するためだけにある（出力には影響しない）
- **描けないグリフは「そのフォントには無い」のと同じ扱い**にする。`resolve_char()` の
  フォールバック列は `has_drawable_glyph()`（cmap にあり、かつ色データだけでない）で辿るので、
  後ろのフォントがその文字を単色で持っていれば**そちらで描かれる**（豆腐にしない）。
  どのフォントも描けないときだけ `plan.missing = true` で、あとは A31 の仕組みに乗る:
  □ が描かれ、`ShapedCluster::missing` → `LayoutEngine` → `Warning{MissingGlyph, 位置}`
- **`WarningKind` は増やさない。理由は列挙を 1 つ通して detail の文面だけ分ける。**
  利用者にとっては「その字が出せなかった」であり、原因が cmap に無いのか色データだけなのかで
  対処は変わらないので、公開 API の種類を増やす価値はない。一方で原因が分からないと直せないので、
  **`text::MissingReason { NotInAnyFont, ColorOnly }`**（`text_measurer.hpp`）を
  `ShapedCluster::missing_reason` → `layout::MissingGlyph::reason` → `api::to_warnings()` と
  1 本通し、文面だけを分ける:
  `the glyph for U+0041 has only color layers (COLR); drawn as tofu at 1:28`。
  **既定は `NotInAnyFont` で、従来の豆腐の文面は 1 文字も変わらない**（`tests/integration` の
  既存の期待値がそのまま通ることで固定）。理由は**組版には一切効かない**: 送りも豆腐のグリフも
  同じで、run の分け方（`CharPlan::same_run_as`）にも重複除去のキー（`MissingGlyph::operator<`）にも
  入れない。`--dump-stage box` には既定以外のときだけ `"reason": "color-only"` が増える
  （既定値のキーは出さない = 従来のダンプはバイト単位で不変）
- **豆腐の `□` を探すときも「描けるか」で見る**（`emit_missing_run()`）。`□`（U+25A1）自体が
  色データだけのグリフであるフォントを選ぶと、豆腐が空白になって同じ壊れ方をするため
- **却下した案**: (B) エラーで止める → 絵文字 1 文字で文章全体が組めなくなる。豆腐を警告に
  している唯一の例外の趣旨に反する。(C) COLRv0 のレイヤーを単色で重ねて描く →
  「日本語の文章を正しく組むことに寄与するか」に No。重ねた結果は黒い塊で □ より情報が多くない
- **OT-SVG（`SVG ` テーブルだけを持つフォント）も同じ扱いにする**: `hb_ot_color_has_svg()` で
  同じように検出できるが、検証用のフォントが無いので**この作業では実装しない**。
  実装するときは `collect_color_only_glyphs()` に条件を足すだけで済む（判定の置き場は同じ）

**A44. ルビの掛け（JLREQ 3.3.8）は「アイテムがはみ出してよい量」として `linebreak::Item` に
持たせ、行頭・行末で落とすのは行分割器の仕事にする。文字クラスの判定は layout に残す。**
（issue #28 の (b)。A41 が予告した形）

掛けは 2 つのことを同時に要求する: **(1) 掛けてよい相手かは隣の文字の分割クラスで決まる**
（layout が知っている）、**(2) 行頭・行末では掛けてはいけない**（行が決まるまで分からない）。
(2) は行分割の**あと**にしか判定できないので、`trim_line_start` / `trim_line_end` と同じ形にした。

- **契約**: `Item::overhang_before` / `overhang_after`（px、非負）。行の幅は
  `advance − overhang_before − overhang_after` で測り、行頭・行末では該当する側を落とす。
  効いた量は `Spacing` に負の値で入るので、**描画側の手順（`pen += before` …）は変わらない**。
  §3.4 (4') が規則、§3.8 の規則 6 が layout 側の決め方
- **`linebreak` は「掛けてよい相手か」を知らない。** 量だけを受け取る。禁則テーブルと同じ
  分類を linebreak 側で公開して layout から呼ぶ案もあったが、**cl-15 / cl-16（仮名）と
  cl-19（漢字等）の区別は UAX #14 のクラスには無い**（どちらも ID）ので、結局 layout に
  仮名の表が要る。依存の向き（A2 の「linebreak は何にも依存しない」）を保つ方を採った
- **量の決め方**（§3.8 の規則 6）は「前後に掛けられるなら 1:1、片側だけならその側に寄せ、
  片側あたり `<rt>` の 1em を上限」。**1 文字に掛けてよいのは 1em まで**という JLREQ の上限を
  「片側あたり」と読んだ（隣の 1 文字を超えて掛けると、その先の文字まで覆ってしまう）
- **掛けた側の文字は動かない。** 動くのは組（親文字とルビ）と、組より後ろの文字の位置だけ。
  ルビが隣の仮名の上に**重なる**のが掛けなので、隣の文字を避けさせては意味がない
- **背景の矩形は掛けを含まない**（= 組が行の中で占める送りの範囲）。掛けは「ルビの帯が
  隣にはみ出す」だけで、親文字の箱が広がるわけではない。A37 / (a) の「組の箱の矩形」と
  同じ考え方で、`Spacing` に入った掛けを戻して求める（Atomic のアイテムに付く `Spacing` は
  掛けだけなので一意に戻せる。§3.4 (4')）
- **改行位置と固有寸法は変わりうる。** 掛けは行の幅を縮めるので、(a) と違って
  `Item::advance` の実効値が変わる。`min_content_width()` にも同じ規則で効き、区間の端では
  落ちるので「返した幅で必ず収まる」性質は保たれる（性質テストに掛けを混ぜて検査）
- **Chrome との差**: Chrome 153 は**漢字にも掛ける**（実測: `名<ruby>桜<rt>さくら</rt></ruby>木` で
  桜 16 / 木 32）。JLREQ 3.3.8 は漢字等（cl-19）への掛けを認めていないので真似しない。
  片側だけ掛けられるときに Chrome は片側 1/2 だけ掛ける（`私は東京に住む` の `に` が 68 = 4px）が、
  shashoku は余りを掛けられる側に寄せる（= 8px）。JLREQ に「片側だけのときは半分」という
  規定は無く、掛けられる側に寄せた方が組の前後の不自然な空きが消えるため

**A45. 配布バイナリの検査は「ELF の実行ファイルであること」から始める。`readelf` の失敗は
終了コードと stderr の両方で見る。検査そのものにテストを付ける。**（issue #31）
`scripts/check_dist_binary.sh`（A39）は NEEDED と要求 glibc を取る 2 つのパイプラインを
`|| true` で終えていたので、**解析できなかった結果が「動的依存も glibc のシンボル版も無い
正常なバイナリ」に化け**、ELF ですらないテキストファイルが合格していた（main 58e61df で再現）。
#22 の SIGPIPE 対応（読み手が先に終わる問題）とは別の穴。

- **順番**: (1) ファイルがある → (2) `readelf -h` が通る（= ELF）→ (3) 実行権がある →
  (4) NEEDED → (5) 要求 glibc。「NEEDED が空 = 完全静的リンク」と読んでよいのは、
  解析が**成功した**ときだけ
- **終了コードだけでは足りない**: ELF ヘッダだけで切れたファイルでは、readelf は
  **終了コード 0 のまま** stderr に `Error:` を書く（実測）。readelf の呼び出しは
  「終了コード 0 かつ stderr が空」を成功とし、違えば readelf の出力を添えて落とす
- **実行権も見る**（#31 の受け入れ条件の外。最小限の追加）。`.o` / `.a` / `.so` は ELF だが
  動的依存を持たないので、(2) だけでは「完全静的リンク」として合格してしまう
- **`LC_ALL=C` を固定する。** readelf は見出しもエラーも locale で日本語になる（このマシンで実測）。
  出力を解析する以上、言語に依存させない（DESIGN.md §3-5 の「ロケール禁止」と同じ理由）
- **検査を検査する**: `scripts/check_dist_binary_test.sh` が 9 通り（存在しないパス／テキスト／
  実行権のあるシェルスクリプト／空ファイル／ELF ヘッダだけで切れたファイル／ELF だが実行権なし／
  libc++ に動的依存するバイナリ／配布バイナリ／上限を下げた配布バイナリ）に検査を掛け、
  終了コードとメッセージを見る。**CI の `dist` ジョブと release.yml の両方で走る**
  （配布物のビルドと同じく 2 か所で押さえる。A39 の「2 つを機械で押さえる」と同じ考え方）。
  docker は要らない。
  「落ちてほしいバイナリ」には同じ dist ビルドのテスト実行ファイルを渡す:
  静的リンクは CLI にだけ効くので、これは release プリセットの CLI と同じく libc++ を動的に引く
- **合格するときの出力は変えていない**（配布バイナリと release の CLI の両方で、修正前の
  スクリプトと 1 バイトも違わないことを確かめた）。`SHASHOKU_MAX_GLIBC` の上限判定も変えていない

**A46. fail loudly を「一度に全部・直し方つき・結果にも」に広げる。エラーと警告は AI が読んで
直すためのフィードバックであり、製品の主機能として扱う。**（2026-09-23、ユーザーの決定。
DESIGN.md §2「個性を支える態度」と §3-6 を書き換えた。実装は未着手で、別に起票する）

根拠は 2026-09-23 の適合検証（依頼 10 件 × 4 経路。記録はセッションのスクラッチ `exp/RESULTS_2026-09-23.md`）:

- **対応範囲の文書（README 抜粋 111 行）を渡した AI** が書いた HTML は 10/10 が 1 回で通り、エラー 0・警告 0
- **普段どおりに AI が書いた HTML** は 10/10 が `<html>` で止まり、止めた原因 180 件（property 112 / selector 20 /
  value 20 / tag 13 / wrapper 10 / unit 4 / image 1）を 1 件ずつ直して通した。**170 件はエラー文から直し方が
  決まったが、1 回に 1 件しか出さないため CLI が 190 回走った**（反復の中央値 16.5）。hint のある
  `box-sizing` / `min-height` / `position` は 1 回で直り、hint の無いものは宣言を削るしかなく見た目が落ちた。
  `background-clip: text` を削ると `color: transparent` が残って**文字が消えた**（警告なし）
- **Satori**（比較対象）は同じ依頼で exit 0 が 10/10 なのに 1 回目に正しい絵は 1/10。`<ruby>` の連結、
  `writing-mode` の消失、`<title>` の文字の混入がすべて無警告。サーバーでは人が絵を見ないので、
  終了コードが配信判断の材料になる。**ただし約束の範囲は限定して言う**: strict の成功が意味するのは
  「対応範囲の中で、検出対象の検査（対応外・欠落・はみ出し）を満たした」ことであって、絵が正しいことの
  保証ではない。未検出のレイアウトの誤り、意図しない重なり、文字色と背景色の同化は検出しない
  （過去の縦書きルビの左右逆やルビの配置不良は、テストが通ったまま起きた）
- **shashoku 自身にも穴がある**: `viewport_height` を固定して中身がはみ出しても、警告なし・exit 0 で
  切れた PNG が出る（`examples/og_card.html --height 200` で確認。1200×200 の PNG、stderr 空）。
  対応外の記法には厳しいのに、組み上がった絵の欠落には黙っている

いまの契約（変える前の状態）: `render()` は `std::expected<RenderResult, RenderError>` で **最初の 1 件**で
止まる。hint は `style/value_parser.cpp` の `kPropertyHints`（13 件。「同じ結果が出せると確かめた代替だけ」の
規則は維持する）。警告は `WarningKind::MissingGlyph` の 1 種類（A31 / A43）。

決めたこと（**契約に触れるので、実装の前にこの節と §3.x を更新してから起票する**）:

1. **一度に全部**: ① html と ② style の段は、見つけた問題を**集めてから**失敗する（同じ規則の中の連続した
   対応外プロパティ、`-webkit-*` の接頭辞、複数のセレクタ、など）。③ layout 以降は 1 件目で止めてよい
   （後段は前段の成功が前提）。公開 API は `RenderError` 1 件のままにするか `errors` の配列を持つ失敗型に
   するかを **v0.1.0 の前に決める**（`include/shashoku/` は互換性を背負う。DESIGN.md §8）。
   CLI は全件を標準エラーに出し、終了コードは今までどおり非 0
2. **直し方つき**: hint の規則（確かめた代替だけ）は変えない。加えて、**削ると危険な組**
   （`background-clip: text` + `color: transparent` のように、削った結果が「文字が消える」になるもの）は
   エラー文に危険を書く。`unsupported-layout` の hint は「`display: block` を足す」と「宣言を削る」の
   どちらを勧めるかを明記する（前者は文の流れを壊す。検証 C の観察）
3. **結果にも**: `WarningKind` に **`ContentOverflow`**（固定した紙面から中身がはみ出した量、CSS px）を足す。
   固定した箱からテキストがはみ出す場合も同じ種類で返すかは、実装時に §3.8 で決める。画像の不在は
   すでに `ImageNotFound`（エラー）
4. **格上げ**: `RenderOptions::warnings_as_errors`（既定 `false`）を足し、`true` なら警告 1 件以上で
   `RenderError`（新しい `ErrorKind`。文面は警告と同じ）にする。CLI は `--strict`。オプションは入力の一部
   なので純粋関数の性質（§3-5）は変わらない。豆腐を警告のまま続行する A31 / A43 の決定は変えない
5. **入れないもの**: 「未対応を黙って無視するモード」。検証 C の試算では 5/10 が通らず、通った 5 件中
   4 件で文字が背景と同色になって消えた（exit 0）。Satori の欠点を輸入することになる
6. **機械が読める契約**: 安定させるのは `ErrorKind` / `WarningKind` のケバブケース識別子（`to_string(kind)`）と
   `SourceLocation`。`message` / `detail` の文面と hint は人向けで、**変えてよい**。CLI は全診断を JSON で出す
   モード（名前は実装時に決める）を持ち、機械側はそれを読む。テストは識別子と位置で検査し、文面の検査は
   hint の内容確認に限る（いまの `tests/style/error_test.cpp` は文面を見ているので、実装時に整理する）
7. **効果は実装後に測る**: 「一括にすれば往復が減る」は期待値であって実測ではない。相互依存するエラーや、
   直したあとで初めて現れる問題がある。実装後に同じ依頼 10 件（`exp/requests.md`）で往復回数を測り、
   ここに追記する

（この時点で未決だった関門の扱いと §1 の Satori の記述の訂正は、同日の A47 で決着した）

**契約の確定（2026-09-23 夜。外部レビュアーの推奨を採用し、細部をオーケストレーターが決めた。実装前の仕様）**

公開 API（`include/shashoku/`）の変更。v0.1.0 前なので互換性は壊してよいが、ここに書いた形で固定する:

```cpp
// source_location.hpp（新設）: SourceLocation を error.hpp から移す（warning.hpp が error.hpp に依存しないため）
// warning.hpp
enum class WarningKind : std::uint8_t { MissingGlyph, ContentOverflow };
struct Warning {
  WarningKind kind; std::string detail; char32_t codepoint = 0;   // 既存
  std::optional<SourceLocation> location;                          // 既存
  float overflow_px = 0.0F;   // ContentOverflow: 紙面の外に出た量の最大（CSS px）。他の種類では 0
  OverflowEdge overflow_edge = OverflowEdge::None;   // ContentOverflow: 最大の超過量を出した物理の辺（W3 の実装で追加）
};
// error.hpp
enum class ErrorKind : std::uint8_t { /* 既存 16 種 + */ WarningAsError /* strict で警告を格上げしたもの */ };
struct RenderError {
  ErrorKind kind; std::string message; std::optional<SourceLocation> location;   // 既存
  std::string hint;                     // 「代わりにどう書くか」。無ければ空。人向けで文面は変えてよい
  std::optional<WarningKind> warning;   // kind == WarningAsError のとき、元の警告の種類
};
struct RenderFailure {
  std::vector<RenderError> errors;   // 1 件以上。入力位置の昇順（位置なしは末尾）→ kind → message で安定
  std::vector<Warning> warnings;     // 失敗までに集まった警告（strict で格上げしたものは errors 側に移し、ここには残さない）
  bool truncated = false;            // limits.max_diagnostics に達して記録を打ち切った
};
std::string to_string(const RenderFailure&);   // 1 行 1 件。既存の to_string(RenderError) の形を並べ、hint は "  hint: …" の行
// render.hpp
struct RenderResult { /* 既存 */ bool diagnostics_truncated = false; };   // 警告が上限で打ち切られた
std::expected<RenderResult, RenderFailure> render(...);                    // 3 つのオーバーロードとも
// options.hpp
struct RenderOptions { /* 既存 */ bool warnings_as_errors = false; };
// limits.hpp
struct RenderLimits { /* 既存 */ std::size_t max_diagnostics = 100; };    // errors + warnings の合計の上限
```

決めたこと（番号は上の 1〜7 の細部）:

- **集める範囲（「一度に全部」）**: ① html は `UnsupportedTag` / `UnsupportedAttribute` を集めて**その要素を読み飛ばして続行**する。
  `InvalidUtf8` / `HtmlParse`（閉じ忘れなど構造の破損）/ `LimitExceeded` は**その場で止める**（安全に解析を続けられない）。
  ② style は `CssParse`（宣言・規則の単位で読み飛ばし）/ `UnsupportedProperty` / `UnsupportedValue` /
  計算値の検査で分かる `UnsupportedLayout`（inline への padding、直交する writing-mode）/ `ImageNotFound` を集める。
  `NoFonts` / `FontLoad` / `ImageDecode` / `InvalidOption` / `LimitExceeded` は止める。①② で 1 件以上あれば layout に進まず
  `RenderFailure` を返す。③ layout 以降は今までどおり 1 件目で止める（後段は前段の成功が前提。`Internal` / `OutOfMemory`）
- **上限**: `max_diagnostics`（既定 100）は errors と warnings の合計。達したら**記録をやめて解析は続け**、
  `truncated = true` を立てる。解析の時間と入力は既存の `RenderLimits` が抑える。診断そのものがメモリの消費源に
  ならないための上限（サーバー安全性との接点）。JSON にも `"truncated": true` を出す
- **順序と決定性**: 診断は (offset, kind, message) で安定に整列する。同じ入力からは同じ並びで出る（§3-5）
- **strict**: `warnings_as_errors = true` のとき、描画が終わって警告が 1 件以上あれば `RenderFailure` を返す。
  errors は警告 1 件につき `RenderError{kind = WarningAsError, message = warning.detail, location = warning.location,
  warning = warning.kind}`。**識別子・位置・詳細を失わない。** PNG は返さない。豆腐を警告のまま続行する既定（A31 / A43）は変えない
- **CLI**: `--strict` でオプションを立てる。**失敗時は出力ファイルを作らない・上書きしない**（成功してから書く。
  既存の `-o` の扱いを見直す）。`--diagnostics json` で**標準出力に JSON を 1 オブジェクト**:
  `{"ok": bool, "width": W, "height": H, "errors": [{"kind": "unsupported-property", "message": "…", "hint": "…",
  "line": 3, "column": 14, "offset": 120, "warning": null}], "warnings": [{"kind": "missing-glyph", "detail": "…",
  "codepoint": 128512, "line": …, "column": …, "offset": …, "overflow_px": 0}], "truncated": false}`。
  `line` / `column` / `offset` は位置が無ければ `null`。JSON のときは `-o` 必須で、`--dump-stage` と併用不可。
  人向けの標準エラー出力（今の形式 + hint の行）は JSON を選ばないときそのまま。終了コードは今までどおり失敗で非 0
- **hint**: `kPropertyHints` の規則「shashoku で同じ結果が出せると確かめた代替だけ」は変えない。足すのは
  (a) ベンダー接頭辞（`-webkit-*` / `-moz-*` / `-ms-*`）: 「接頭辞を外す（対応表にあれば）」
  (b) **削ると危険な組**: `background-clip` / `-webkit-background-clip`: 「`color: transparent` も外さないと文字が消える」
  (c) `unsupported-layout`（inline への箱プロパティ）: 「宣言を削る（`display: block` にすると文の流れが切れる）」と、
  どちらを勧めるかを明記。hint は `RenderError::hint` に入れ、`message` には混ぜない（機械側が分けて読める）
- **はみ出し（`ContentOverflow`）**: 対象は「紙面で実際に切れる内容」だけ。**箱（行ボックス・置換要素・ブロックの
  border box）が出力の矩形（viewport_width × 高さ。高さは固定のときだけ）の外に 0.5 px を超えて出ている**とき、
  最も外側の該当要素ごとに 1 件、`location` はその要素の入力位置、`overflow_px` は最大の超過量。
  グリフのインク（イタリックの張り出しなど）は見ない。**箱からのはみ出し（固定幅の箱から文字が出る）は別種で、
  この段では入れない**（`BoxOverflow` として将来）。
  **誤検出への配慮（受け入れ例を先にテストに書く）**: ぶら下げ（burasage）で行末の約物が行ボックスの外に出ても
  紙面の中なら警告しない／ルビの注記は行ボックスの中なので警告しない／`--height` 省略（内容追従）では縦方向に
  はみ出せないので横方向だけ判定／負のマージンで左・上に出た場合も対象。1 px 未満の丸め差は 0.5 px の許容で吸収
- **テスト**: 識別子（`to_string(kind)`）と位置で検査する。文面は hint の有無と内容の検査に限る。
  いまの `tests/style/error_test.cpp`（message を 34 か所で見ている）は、識別子・位置・hint の検査に書き換える
- **効果の測定**: 実装後に、依頼集の日本語 10 件と英語 5 件（第 3 節）の両方にガイドを渡し、初回成功率・修正の往復回数・
  所要時間・誤警告・内容の保持を測る。**見た目の評価は「元画像への忠実度」と「用途を満たすか」を分けて記録する**
  （グラデーションが消えても使える場合があり、文字が全部出ても情報の強弱が失われれば使えない）。可能なら別モデルでも
  （既知の依頼への最適化になっていないか）。境界ケース（第 4 節）は別に試し、需要の測定と混ぜない
- **言い過ぎない**: strict の成功が意味するのは「対応範囲の中で、検出対象（対応外・豆腐・紙面のはみ出し・画像の不在）の
  検査を満たした」こと。未検出のレイアウトの誤り・重なり・文字色と背景の同化は対象外

**実装の分割**（各作業は worktree の opus エージェント。契約ヘッダと §3.x の更新はオーケストレーターが先に行う）:
W0 契約（ヘッダ 5 本と §3.6 / §3.7 / §3.8 / §3.10、受け入れ例の HTML）→ W1 html（集める）/ W2 style（集める・整列・hint）/
W3 layout（はみ出しの検出と一覧）を並行 → W4 api + CLI（`RenderFailure`、strict、JSON、上限、失敗時に書かない、統合テスト）→
W5 対応範囲と書き方のガイド（`docs/guide/`。どの LLM にもテキストで渡せる本文と、それを包む skill。B と C とグラレコで分かった
不足を反映。W1〜W4 と並行に下書き、最後に JSON の形を合わせ、エンジンと同じ版番号を持つ）→ W6 サーバー連携の**小さな**実例
（CLI 呼び出し・診断 JSON・上限つき再試行を示すスクリプト 1 本。HTTP サービスや汎用のエージェント基盤には広げない）

**A47. 製品の目的を「HTML から共有・資料利用のための PNG をブラウザなしで生成する」に置き、
日本語組版は「強み・最初の得意分野」とする。機能追加の関門は「対象用途で成功率・修正の手間・
運用上の信頼性を改善するか。保守コストに見合う実例が依頼集にあるか」に置き換える。**
（2026-09-23、ユーザーの決定。外部レビュアーの整理を採用。DESIGN.md §0 §1 §2、CLAUDE.md、README 冒頭を書き換えた）

- **経緯**: 適合検証（A46 の根拠と同じ。記録は `docs/benchmark/results_2026-09-23.md`）で、強みの大半
  （1 回で使える絵、高さの追従、診断、単一バイナリ、決定性）が言語と無関係だと分かった。実行コストの差
  （cold で起動 10 倍・メモリ 1/4）はネイティブ実行と SVG を経由しない経路によるもので、常駐では 1 枚あたりは同じ桁（16.8 ms 対 15.4 ms。優劣は言えない）で、残る差はメモリ（29 MB 対 283 MB）と
  同時生成（フォントを全スレッドで共有できる shashoku は 16 スレッドで 558 枚/秒・295 MB。Node は 1 プロセスでは 59 枚/秒、
  worker 16 本で 395 枚/秒・2.6 GB）。`docs/benchmark/results_2026-09-23.md` §5。一方で box-sizing・
  画像・診断の改善を「日本語に寄与しないが例外で入れる」と扱い続けるなら、主関門のほうが目的とずれている
- **以前の関門が果たしていた役割は範囲の統制**で、日本語はその代理だった。新しい関門は統制を測定（依頼集）と
  保守コストで直接かける。依頼集に無い需要は実例に数えない
- **依頼集**: `docs/benchmark/requests.md`。2026-09-23 の 10 件（資料向け 5 + サーバー生成 5。製品に合わせて
  選んでいない）から始める。用途を足すときは依頼を足し、判定は「依頼集の中で何件が困ったか、代替が無かったか」。
  **既存 10 件は回帰検証用に固定**し、実利用で見つかった依頼は別枠（同じファイルの第 2 節）に追加する。製品の成長を
  10 件に閉じ込めない。判定は「通る／通らない」の二値ではなく「現時点の優先度」で言う（例: グラデーションは
  単色で目的を満たせたため現時点では優先度が低い。辺ごとの border は削ると代替が無いため高い）
  検証のやり方（4 経路、Chrome の参照画像、fidelity の目視）も同じ文書に残す
- **変えないもの**: 「削る」の一覧（JS、外部取得、grid / float / table、ブラウザとのピクセル一致）、fail loudly（A46）、
  決定性（A32）、一方向パイプライン。エンジンは日本語専用にしない（英語の文章でも普通に使える設計を保つ）が、
  英語圏向けの文書と実例は日本での手応えを見てから。CJK への拡大も自動の次の一手にせず、言語ごとに需要と
  組版規則を別に検証する
- **順序**: まず日本の開発者に向けて出し、価値を確かめる。絞るのは届ける相手・紹介する用途・検証する品質で、
  エンジンではない
- **§1 の訂正**: 「Satori は禁則処理がない」は誤り（Satori 0.33.5 は UAX #14 で基本の禁則を行う。実測）。
  差は両端揃え・約物の詰め・和欧文アキ・ルビ・縦書き・高さ追従・黙殺しないこと
- **言い過ぎを避ける**: 「絶対に破綻させない」「信じられる PNG」のような検証しきれない約束は書かない（A46 の補正と同じ）
- **追記（同日夜。対象を言語で絞らない）**: 「エンジンとガイドをセットで、生成・診断・修正まで動く」価値は言語を問わないため、
  一文ミッションを「AI とアプリのための、ブラウザ不要の HTML→PNG エンジン。文章主体のカード・図解・資料画像を生成し、問題は
  修正できる形で伝える。日本語組版にも強い」に改め、4 層の「最初の得意分野」を「対象と検証: 日本語・英語を対象に検証する
  （全言語の組版保証ではない）」と「積み上げた強み: 日本語組版。既存機能として維持し、追加開発は需要で判断」に分けた。
  「最初の試用は日本の開発者から」は連絡とフィードバックの得やすさで決めた順序で、対象の限定ではない。縦書きの需要の多寡を
  製品全体の需要の根拠にしない。依頼集に英語の自然な依頼 5 件（第 3 節。日本語固有の機能を使わない）を固定で足し、
  組版の限界を調べる境界ケース（長い URL、ハイフネーション、スモールキャップなど）は需要の測定と混ぜず第 4 節の別枠にした。
  「AI 向けガイドは他に無い」は前提にしない（Vercel は AI 向け skill を提供している）。差別化はガイドとエンジンを一緒に設計して
  一連の成功率を検証すること。**設計上の目標（A46）と現在の機能は文書で区別する**

**A48. style の「集めて続行」は、宣言と規則を読み飛ばす単位で決める。hint は表を引く前に
ベンダー接頭辞を外す。**（2026-09-23、A46 の実装 W2。仮番号）

A46 の「① html と ② style は見つけた問題を集めてから失敗する」を style に入れるにあたって決めた細部。
仕様の本体は §3.7 に書いた。ここには**なぜそう決めたか**だけを残す。

- **読み飛ばしは宣言の単位が基本で、セレクタが読めないときだけ規則の単位。** CSS Syntax 3 §5.4 の
  エラー回復と同じ粒度。宣言 1 つが読めなくても残りの宣言は著者の意図どおりなので効かせる
  （検証 C で「1 件直すたびに CLI を走らせ直す」原因になっていたのは、1 つの規則の中に対応外が
  並んでいる場合だった）。セレクタが読めない規則は、宣言をどの要素に当てるか決められないので丸ごと捨てる
- **捨てた宣言は「書かれなかった」扱いにする**（途中まで展開した longhand も巻き戻す）。
  「半分だけ効いた宣言」は、あとで診断を読んで直すときにいちばん説明しづらい状態になる。
  errors が 1 件でもあれば描画しないので、この木が絵になることはない
- **未終了のコメントだけは回復しない。** どこまでがコメントかを決める手がかりが無く、推測して
  読み進めると「著者が書いていない宣言」を報告しかねない。記録して残りを捨てる
- **文書の `writing-mode` を決める先読みでは診断を出さない。** 先読みはトップレベル要素を
  もう一度カスケードするので、そのまま記録すると同じ診断が 2 件になる。捨てる `Diagnostics` に流し、
  致命エラーも握り潰して `build_element` の 1 回に任せる（**先に集めた診断を失わないため**でもある。
  先読みで止めると、文書の前方にあった対応外の報告が消える）
- **トップレベルの `writing-mode` の食い違いは、最初に出た値を採って続行する。** 続きの要素の診断を
  出すには文書の値が 1 つ要る。「最初に出た値」は入力だけで決まるので決定的（DESIGN.md §3-5）
- **hint は表を引く前にベンダー接頭辞を外して引き直す。** `-webkit-background-clip` に
  `background-clip` と同じ助言を出すために別表を持つと、2 つの表が食い違う。外した名前が対応表に
  あれば「接頭辞を外す」、対応外でも hint 表にあればその hint、どちらでもなければ**何も言わない**
  （`-webkit-line-clamp` に `line-clamp` の助言を捏造しない）
- **`kPropertyHints` は「確かめた代替」に加えて「削ると危険な組」も載せる**（A46 の 2）。
  代替ではないが、`background-clip: text` を黙って消させると `color: transparent` が残って
  **文字が消える**（検証 C で実際に起きた）。表のコメントに 2 種類あることを明記した
- **hint 付きのエラーは `style/style_error.hpp` の `error_with_hint()` で作る。** `core/result.hpp` の
  `fail()` に hint 引数を足すと全モジュールの共有物が A46 のために太るので、style の中に置いた
  （`is_recoverable(kind)` も同じ理由でここ）

**追記（2026-09-24、A53 と同じ回）**: hint の書き方と、計算値の診断の重複について決めた。

- **hint は無条件の置き換えにしない。** A46 の「確かめた代替だけ」は維持したうえで、**見た目が変わる代替**
  （gradient → 単色、片側の `border` → 1px の div）は「変わる」と明記し、**成立条件**があるなら添える。
  `min-height` を「`height` を使え」で置き換えると、親が flex で `align-items: stretch`（既定）のときに
  不要な固定高さが入る。条件は宣言だけからは判定できないので、条件を書いたうえで
  「判定できないならガイドの節番号を見よ」に寄せる。等価な書き方が無い側（箱の枠の一辺、不等幅の grid）は
  「no equivalent」と書いて、代替を捏造しない
- **値レベルの hint を足した**（`bad_value_with_hint()`）。`display: grid` / `display: inline-block` /
  `background: linear-gradient(…)` は、プロパティ名まででは対応外だと分からない。プロパティの表
  （`kPropertyHints`）を値にも広げるのではなく、値パーサの中で**値を見てから**添える。
  message は `bad_value()` と同じまま（変えたのは hint だけ）で、`core/result.hpp` の `fail()` と
  `Error` は触っていない（A46 のときと同じ理由）
- **名前の引き方に接頭辞の一致を足した。** 片側だけの `border-*` は 16 通り、grid は `grid-template*` /
  `grid-auto-*` があるので、表に 20 行以上並べるより接頭辞で 1 規則にするほうが食い違わない。
  ただし**完全一致を先に引く**（legacy の `grid-gap` / `grid-row-gap` / `grid-column-gap` は A35 の
  案内のまま）。`border-top-left-radius` のような**角**の指定には罫の助言を当てない
  （助言が別物になる。間違った助言をするくらいなら何も言わない、という A48 の規則）
- **計算値の診断の重複は style の中で落とす。** 1 つの規則（`.tag { border: 1px solid #000 }`）に
  複数の要素が一致すると、計算値の検査は要素ごとに同じ診断を出す（V2-fix の case03 / 06 / 09 で
  `unsupported-layout` が同一位置・同一文面で 2 件）。**同じ (kind, location, message) は 1 回だけ**にする。
  core の `Diagnostics` を変えないのは、①html と③以降の診断は「同じ文面が複数回出るのが正しい」ことが
  あるため（要素ごとの豆腐・はみ出し）。落とすのは**計算値の検査だけ**で、宣言の単位の診断は
  もともと規則を 1 回しか読まないので対象外。覚えるのは順序つきの `std::set`（`unordered_map` の
  反復順を出力に影響させない。DESIGN.md §3-5）で、`Diagnostics` が上限で捨てたものは覚えない
  （大きさが `max_diagnostics` で抑えられる）

**A49. ① html の「透過」の細部: 対応外の要素は開始タグ・終了タグを無いものとして読み、
HTML の空要素はスタックに積まず、入れ子の上限は透過を含むスタックの深さで見る。
捨てるもの（透過した要素の属性・対応外の属性の値・対応外の生テキスト要素の中身）は
読み飛ばすだけで、報告も検証もしない。**
（2026-09-23、W1 と W1b の実装で決めた。A46「集めて続行する」の具体化。仕様は §3.6）

- **なぜ透過か**: `<html><head>…</head><body>…</body></html>` で包まれた入力（AI が普段どおりに書く HTML の
  10/10 がこの形。A46 の検証）で、外側の対応外のタグで解析を止めると中身の問題が 1 件も出ない。
  透過にすると 1 回で全部出る
- **HTML の空要素（`area base col embed hr input link meta param source track wbr`）は積まない**:
  これらには終了タグが無いので、`<meta charset="utf-8">` を積むと直後の `</head>` が「入れ子の誤り」になって
  致命エラーになる。**一覧は WHATWG の void element から対応済みの `br` `img` を除いたもの**で、
  「対応していないタグの構造を知っている」唯一の場所。ここに無い対応外のタグ（`<section>` など）は
  終了タグを要求する（閉じ忘れは今までどおり `HtmlParse`）
- **`/>` で閉じた対応外の要素はその場で終わる**（`<section/>`）。対応済みのタグに対する `<div/>` は
  今までどおり `HtmlParse`。対応外のタグはすでに `UnsupportedTag` で報告しているので、
  同じタグについて 2 件目のエラーを出さない
- **対応する開始タグの無い終了タグ**（`</table>` 単独）は `UnsupportedTag` を足して読み飛ばす。
  ただし**その名前の要素が開いているのに一致しない**ときは入れ子が壊れているので `HtmlParse`（致命）
- **入れ子の上限は透過を含むスタックの深さ**で見る（`max_nesting_depth`）。木の深さより厳しくなるが、
  `<section>` を 1 万個並べただけの入力で解析器のメモリが伸びないようにするため（上限の数値は同じ）
- **透過した要素の境界でテキストノードが分かれる**（`<div>a<section>b</section>c</div>` は
  テキスト 3 個）。errors が 1 件でもある木は描画されないので結果には出ない

**追記（2026-09-23、W1b）。「捨てるものは読み飛ばすだけ」を 3 点そろえた。** 動機は同じ検証入力
（`docs/benchmark/2026-09-23/inputs/a_plain/`）で、W1 の透過だけでは 10/10 が `<head>` の
`<link href="…&display=swap">` の `&display` で `HtmlParse`（致命）になり、15〜18 件集めたところで
止まっていた（`<style>` の中身にも本体にも届かない）。

- **透過した要素の属性は報告しない**（`UnsupportedAttribute` を足さない）。`<meta charset="utf-8">` に
  「`class` / `id` / `style` なら使える」と読める報告が並ぶのは雑音で、AI が直すときの妨げになる。
  「`<meta>` が対応外」の 1 件で必要な情報は足りている。**対応済みの要素の対応外の属性**
  （`<p onclick>`）は今までどおり報告する: その要素は残るので、その属性だけが効かないと知らせないと直せない。
  ついでに**重複の検査も残す属性だけ**にした（捨てる属性が重なっても結果に影響しないのに、
  そこで致命にすると後ろの問題が 1 件も出なくなる）
- **捨てる属性の値は文字参照を検証しない**（生のまま読み飛ばす）。対象は透過した要素の全属性と、
  対応済みの要素の対応外の属性の両方。Google Fonts の URL（`?family=A&display=swap`）が典型で、
  値を捨てると決めたあとに中身の綴りで致命にするのは筋が通らない。**残す属性は今までどおり検証する**。
  値の**終わり**の判定（引用符・空白・`>`・引用符なしの値に書けない `"` `'` `=` `<` `` ` `` `/`）は
  捨てる値でも同じ: そこは値の中身ではなく「タグをどこまで読むか」の構文なので、曖昧なまま進めない
- **対応外の生テキスト要素**（`script textarea title xmp iframe noembed noframes`）は、対応する
  終了タグまでを生テキストとして読み飛ばす（`<style>` と同じ扱い。`UnsupportedTag` は 1 件、子は作らない）。
  一覧は WHATWG の raw text / escapable raw text から対応済みの `style` を除いたもの。終了タグが無ければ
  `HtmlParse`（どこまでが中身か決められない）。`/>` で閉じたものはその場で終わる（透過と同じ規則。
  ブラウザは `<script/>` を閉じないが、ここは「厳格なサブセットのパーサ」なので A49 の規則をそろえる）
- **確かめたこと**: 上の 3 点で `a_plain/case01.html` 〜 `case10.html` の 10 件すべてが ① で致命にならず、
  ① の診断を全部集めて ② へ進むようになった。絵は変わらない（ゴールデン不変）。
  release の CLI（`--font NotoSansJP-Regular.otf`）で数えた 1 回あたりの診断件数:

  | 入力 | 変更前（合計 / 致命） | 変更後 ①（tag） | 変更後 ②（property / value / selector / layout） | 変更後 合計 / 致命 |
  |---|---|---|---|---|
  | case01 | 19 / `html-parse` | 42 | 19 | 61 / なし |
  | case02 | 16 / `html-parse` | 9 | 13 | 22 / なし |
  | case03 | 16 / `html-parse` | 8 | 17 | 25 / なし |
  | case04 | 16 / `html-parse` | 19 | 21 | 40 / なし |
  | case05 | 16 / `html-parse` | 8 | 12 | 20 / なし |
  | case06 | 16 / `html-parse` | 8 | 17 | 25 / なし |
  | case07 | 16 / `html-parse` | 8 | 24 | 32 / なし |
  | case08 | 16 / `html-parse` | 8 | 17 | 25 / なし |
  | case09 | 16 / `html-parse` | 12 | 10 | 22 / なし |
  | case10 | 16 / `html-parse` | 8 | 19 | 27 / なし |

  変更前は 10/10 が `<link href="…&display=swap">` の `&display` で止まり、そこから先（`<style>` の中身も
  本体も）を一切見られなかった。終了コードは前後とも 1（診断があるので描画しない）で、PNG は作らない。
  `max_diagnostics` の既定では 10 件とも打ち切られていない

**A50. 紙面からのはみ出し（A46 の「結果にも」③）の
細部: 単位は CSS px のまま、合成ルートは候補にしない、断片は含むブロックの位置で報告する。**
（2026-09-23、実装時に決めた。実装は `src/layout/check_overflow.cpp`、仕様は §3.8）

- **辺（`OverflowEdge`）を `ContentOverflow` に足した**: §3.8 が決めていた構造体は
  `{ SourceLocation location; float overflow_px; }` の 2 つだけだったが、それだと
  `warning.hpp` が例示している detail（`"... by 42.5px (bottom) ..."`）を api が作れない。
  「下に出た（背が高すぎる）」と「右に出た（幅が広すぎる）」は直し方が別なので、
  最大の超過量を出した辺を一緒に運ぶ。物理の 4 辺で、縦書きでも「下」は物理の下
- **単位は CSS px**: レイアウトの座標は scale を掛ける**前**の CSS px（api は `RenderOptions::viewport_width`
  をそのまま `layout::Options::viewport_width` に渡し、scale は ⑤b raster が `raster::Target::scale` で掛ける）。
  だから `ContentOverflow::overflow_px` はそのまま `Warning::overflow_px`（CSS px）になる。**api は割らない**
- **合成ルート `#root` は候補にしない**: ルートの border box は高さが `auto` で常に中身に追随するので、
  中身が縦にはみ出せば必ず一緒にはみ出す。候補にすると「最も外側の 1 件」が毎回ルートになり、位置が
  入力の先頭（1:1）に化けて、実際に突き出した要素を隠してしまう。ルートは入力の要素でもない。
  除いても取りこぼしはない: ルートがはみ出すときは必ずその子孫のどれかが同じ辺からはみ出している
  （負のマージンでルートの外に出た子も、子として判定される）
- **行ボックスと画像断片は、含むブロック要素の位置で報告する**: `LineBox` と `ImageFragment` は
  `SourceLocation` を持たない（`BlockBox` と `TextFragment` だけが持つ。A31 / A36）。位置のためだけに
  型を増やすより、含むブロックを指すほうが直せる形になる（「この段落が紙面から出ている」）。
  §3.8 が行について決めていた規則を、断片にもそのまま広げた
- **同じ位置は 1 件にまとめ、超過量は最大を採る**: 1 つの要素から複数の箱（複数行・複数の断片）が出ることが
  あり、位置が同じ警告を何件も出しても直し方は変わらない。並べ替えは (offset, line, column) の昇順で
  `std::stable_sort`（`MissingGlyph::operator<` と同じ比べ方。決定的）
- **`check_geometry()` のあとに見る**: 座標が有限で上限以内であることを確かめてから引き算するので、
  はみ出し量が非有限になることはない。失敗はしない（警告のための記録なので、木を返せなくしない）
- **確かめたこと**: A46 の動機になった `examples/og_card.html --height 200` で、`.card`（19 行目）の
  はみ出し 430 px が 1 件だけ出る。`--height 630`（正しい高さ）では 0 件。
  縦書き（`examples/vertical.html`）も、`--width 200` で行送り方向の 320 px、`--height 120` で
  字送り方向のはみ出しを段落ごとに出す

**A51. strict の判定は ⑥ の直後に置き、警告は ③ の直後に診断へ通す。診断 JSON のライタは
CLI の中に持つ。**（2026-09-23、A46 の実装 W4。仮番号）

A46 の仕上げ（api + CLI）で決めた細部。仕様は §3.10。

- **strict（`warnings_as_errors`）の判定は PNG を作り終えてから**。警告は ③ で出そろっているので
  もっと早く返せるが、早く返すと `warnings_as_errors` を立てたときだけ ⑤b / ⑥ の失敗
  （`device_pixels` の `LimitExceeded` など）が消える。**strict は診断を増やすだけで減らさない**という
  性質を選んだ。代償は「strict で失敗する入力の PNG を 1 回無駄に符号化する」こと（`render()` の費用の
  9 割は PNG 符号化。A-perf）だが、失敗する入力にしか掛からないので受け入れる
- **警告は ③ の直後に `Diagnostics` へ通す**（返す直前ではなく）。こうすると (1) `max_diagnostics` が
  エラーと警告の合計に掛かるという契約が 1 か所で満たされ (2) ④以降で失敗したときも
  `RenderFailure::warnings` に集まっていた警告が載る
- **格上げしたエラーの並べ替えは `into_failure(extra)` に任せる**。`RenderFailure::errors` の契約は
  (位置, kind, message)、警告の並びは (位置, kind, コードポイント, detail) で規則が違う。
  すべて `WarningAsError` になると kind で差が付かないので、並べ直さないと同じ位置の 2 件の順が
  errors の契約からずれる
- **`layout::OverflowEdge` と公開の `OverflowEdge` は別の型のまま**にし、api で写す。③ は
  「はみ出していない」状態を持たないので `None` が無く、公開 API は `Warning` の既定値のために
  `None` が要る。型を片方に寄せると layout が公開ヘッダの都合を背負う（`linebreak` と同じ理由で、
  段は公開 API を知らないでいられるほうがよい）
- **診断 JSON は CLI が自前で書く**。`src/core/json_writer.hpp` を使うには CLI に `src/` の include パスを
  通すことになり、「CLI は公開ヘッダだけを見る」（`tests/api/public_header_check.cpp` が守っている境界）が
  崩れる。要るのは `"` `\` と制御文字のエスケープだけなので、20 行ほどで済む
- **`--diagnostics` は `human` | `json`** の 2 値にした。`json` だけだと誤った値のときの文面が
  「json のいずれかです」になる
- **確かめたこと**: `docs/benchmark/2026-09-23/inputs/a_plain/case01.html`（普段どおりに AI が書いた HTML）
  を 1 回通すと **61 件**（① `unsupported-tag` 42 件 / ② `css-parse` 11 件・`unsupported-property` 8 件）が
  入力位置の昇順で出る。A46 より前はこれが 1 件ずつで、同じ 1 枚に CLI が何十回も要った。
  `examples/*.html` 5 本の PNG は main（c157766）の配布バイナリとバイト単位で一致する
  （増えたのは成功時の `wrote` の行だけ）

**A52. flex アイテムの自動最小サイズは主軸のサイズプロパティ（width / height）で決め、
flex-basis は使わない（CSS Flexbox §4.5）。**（2026-09-24）

CSS Flexbox Level 1 §4.5 "Automatic Minimum Size of Flex Items" は、`min-width: auto` /
`min-height: auto` の flex アイテムの content-based minimum size を
**min(specified size suggestion, content size suggestion)** と定義している。
specified size suggestion は「**主軸のサイズプロパティ**（row なら `width`、column なら `height`）が
definite ならその値」で、**`flex-basis` は含まれない**。

`src/layout/flex_layout.cpp` はここを `main_dimension()`（flex-basis 優先）で計算していたため、
`flex: 1 1 0`（`flex: 1` の展開。`style` の `parse_flex` は `1 1 0px` にする）のアイテムの
最小サイズが **0** になっていた。結果:

- **column**: `flex: 1 1 0` の子が高さ 0 に潰れ、文字が紙面の下に押し出されて消える。
  再現（幅 480、`width: 400px` + `padding: 8px` の column に `flex: 1 1 0` の子と `flex: none` の子）は
  修正前 `rect` が `[8, 8, 400, 0]`（PNG 480x40、1 つ目の子の文字が欠ける）、
  修正後 `[8, 8, 400, 46.34]`（PNG 480x86、2 行とも見える）
- **row**: `flex: 1 1 0` の子が min-content より縮み、文字が箱の外に描かれる。
  再現（幅 300 の row に `flex: 1 1 0` の子 2 つ、2 つ目は分割できない長い欧文）は
  修正前が 150 / 150（文字が箱からあふれる）、修正後が 16 / 400.33（= それぞれの min-content。
  合計 416.33 がコンテナの 300 を超えるので、紙面から出れば `content-overflow` の警告が出る）

これは A46 後の再検証で「exit 0・警告 0 なのに内容が PNG から消える」と記録した現象の原因
（`docs/benchmark/results_a46_2026-09-23.md` §3 の a_plain case02 / case04）。ユーザーの方針
「検出器を作る前に原因を分け、エンジンの不具合なら直す」に従い、エンジン側を直した。

決めたこと:

- **`min_main`（自動最小サイズ）は主軸のサイズプロパティだけを見る**（`main_size_property()`）。
  row なら `LogicalMap::inline_size`、column なら `block_size`。auto なら内容の min-content
  （row）/ 内容の高さ（column）そのもの、auto でなければその値と内容サイズの小さい方
- **`base`（flex base size）は今までどおり `main_dimension()`**（flex-basis 優先）。
  §9.2 の flex base size と §4.5 の specified size suggestion は別物で、混ぜたのが不具合だった
- **column で主軸が不定（高さ auto）のとき、`%` の `height` は definite ではない**ので、
  下限は内容の高さになる。`base` も今までどおり内容の高さ（`%` は解けない）
- **column で主軸が定まっているとき、伸縮の下限は内容の高さ**。内容がコンテナより高ければ
  はみ出す（§9.7-4d の最小サイズ違反。Chrome と同じ）
- **置換要素（`<img>`）は今までどおり内容サイズより縮めない**。column では `resolve_image()` が
  返す高さがそのまま内容の高さなので、新しい規則でも値は変わらない
- **`flex_intrinsic()` と `LayoutCache` の鍵は変えない**。`flex_intrinsic()` はアイテムの
  `width` と内容の固有寸法しか見ておらず、もともと flex-basis を参照していない。`MeasureKey` は
  `BoxSizing`（= 解決後の `target`）を持つので、下限が変わればそのまま鍵が変わる
- **確かめたこと**: `dev` / `asan` とも 1253 件すべて通り、**ゴールデン画像は 1 枚も変わらない**。
  `examples/*.html` 5 本と `docs/guide/examples/*.html` 12 本を README / ガイドに書かれた引数で
  修正前後のバイナリで描き、**17 本すべてバイト単位で一致**（`flex: 1 1 0` を使う og-card の見出し・
  list・table・vcenter・row を含む。和文の min-content は 1 文字なので下限に当たらない）。
  変わったのは `tests/integration/overflow_wrap_test.cpp` の表のうち
  **`flex: 1` + `overflow-wrap: break-word` / `normal`** の 4 行（× 横書き・縦書き）で、
  32px（3 行に割れる）から 82.21875px（1 行のままはみ出す）になった。CSS Text 3 §5.4 は
  `break-word` の分割位置を min-content に数えないと定めており（A35 に引用）、`flex: 1` の
  `width` は auto なので specified size suggestion が無い。**Chrome も同じで**、
  「`flex: 1` の子で `break-word` は効かない（`anywhere` を使う）」という周知の挙動に揃った。
  表の `Sizing::None`（指定なし）の行が前から 82.21875px だったので、**同じ `width: auto` の
  アイテムが `flex: 1` を書いたときだけ違う、という不整合が消えた**


**A53. flex コンテナの直接の子要素は block 化する（CSS Display 3 §2.7）。例外は img / ruby / br。**
（2026-09-24）

`display: flex` の親の中の `<span>` に `padding` を書くと `unsupported-layout` になっていた。
仕様（CSS Display 3 §2.7 の blockification、CSS Flexbox 1 §4）では flex コンテナの子はブロック化され、
`display: inline` と明示されていても block になる。**shashoku の layout はもともと仕様どおりで**
（`flex_layout.cpp` の `build_items()` は flex コンテナの子をすべて flex アイテム = ブロック級として組む）、
止めていたのは ② style の計算値の検査だけだった。

- **経緯**: A46 後の再検証（`docs/benchmark/results_a46_2026-09-23.md` §2）で、inline への箱プロパティの
  hint「宣言を削る」が当たった 8 件が**すべて pill / タグ / バッジ**で、削ると絵が壊れるため誤解を招いた。
  ガイドは「flex の子はかならず div」という回避規則を持っていた。エンジンを仕様に合わせれば、
  この回避規則も hint の言い換えも要らなくなる（ユーザーの方針: ガイドの回避規則を増やすより
  エンジンの小さな修正）
- **決めたこと**: 親の計算値の `display` が `Flex` である**直接の子要素**で、自分の計算値の `display` が
  `Inline` なら `Block` にする。作者が明示的に `display: inline` と書いていても block 化する（仕様どおり）。
  **孫は変えない**（文中の span は文の流れに残る）。テキストノードも変えない。
  **例外は 3 つ**で、どれも layout が inline のまま別扱いしているもの:
  `img`（置換要素。inline でも箱プロパティを取れ、flex では置換アイテムになる）、
  `ruby`（`flex_layout.cpp` が意図的に無名アイテムの中へ inline のまま入れる。単独のアイテムにすると
  親文字とルビの組が壊れる）、`br`（強制改行そのもの）。`display: none` は none のまま
- **カスケードのあとに直す**（`cascade()` の末尾）。計算値の検査（`validate()`）も、ダンプ
  （`--dump-stage style`）も、layout も、同じ値を見るようにするため。段の順序（DESIGN.md §3-1）は
  変わらない: ② の中で計算値を決め切ってから ③ に渡す
- **確かめたこと**: `<div style="display: flex; gap: 8px">` の中の `<span style="padding: 4px 8px; …">`
  2 つは、修正前は `unsupported-layout` が 2 件、修正後は**診断 0** で、`span` を `div` に書き換えた
  入力の PNG と**バイト一致**する。`--dump-stage style` で span の `display` は `block`。
  flex の中の `<img>` / `<ruby>` / `<br>` を含む入力の PNG は、修正前のバイナリの出力と**バイト一致**。
  `<p>` の中の span への `padding` は今までどおり `unsupported-layout`。
  `examples/*.html` 5 本の PNG は main（2c7066c）のバイナリとバイト一致
- **残った不整合**: `docs/guide/writing-html-for-shashoku.md` §3-(3)「flex 項目の `span` は block 化されない」は
  この判断で失効する（ガイドは別の作業で直す）

**A55. 診断の取りこぼしを 3 つ塞ぐ: inline の箱プロパティは全部、捨てる規則の宣言は構文の診断だけ、
対応外タグのセレクタは決して一致しないので報告する。**（2026-09-24、A52 / A53 の次の回）

A52 / A53 後の再測定（`docs/benchmark/results_a53_2026-09-24.md` §2 / §3）で、普段の AI の HTML を
「診断が指した箇所だけ直す」と往復が増える原因が 3 つ分かった。いずれも A46 の「一度に全部」の取りこぼしで、
**対応している指定が黙って効かない / 診断が次の往復まで隠れる**という信頼性の問題なので最優先で塞いだ。

- **経緯（数字）**: (a) inline への箱プロパティが 1 つずつしか出ず、case04 は border → border-radius →
  padding で 3 往復（case08 も同型）。(b) セレクタが読めない規則を丸ごと捨てるので、中の対応外プロパティは
  セレクタを直した次の往復で初めて出る（case01 / 03 / 06 / 09）。(c) `<body>` を外すと `body { … }` が
  誰にも当たらず、背景・余白・本文色が**黙って消える**（a_plain 10 件中 7 件に同型の HTML、case01 で実害）
- **(a) 作者が書いた箱プロパティは全部覚えて全部報告する。** 数える単位は**作者が書いた宣言 1 つ**で、
  ショートハンドを展開した longhand 列は先頭（`border` なら border-width、`padding: 2px 6px` なら
  padding-top）だけを数える。`style` 属性の中では宣言の位置がすべて属性を指すので**位置では宣言を
  区別できない**。そこで `Declaration` に `source_head`（展開列の先頭か）を足し、css パーサが印を付ける。
  message の形・位置・hint は今までどおりで、並びは api の `Diagnostics::sort()`（位置順）に任せる。
  A48 の重複除去（同じ (kind, location, message) は 1 回）はそのまま効くので、1 つの規則が複数の要素に
  当たっても増えない。UA 由来は今までどおり数えない
- **(b) セレクタを使えない規則の宣言は、「報告だけ」して捨てる。** 宣言ブロックの括弧が対応していて
  安全に読めるなら、通常の宣言パーサに通して宣言レベルの診断（`unsupported-property` /
  `unsupported-value` / 宣言単位の `css-parse`。hint も通常どおり）を出し、**結果はカスケードに入れない**。
  適用しないので「その規則を適用した前提のレイアウト診断」（計算値の検査）は出ない ——
  ここが A48 の「どの要素に当たるか決められないものは推測しない」との両立点で、**構文は要素を知らなくても
  読めるが、計算値は要素を知らないと言えない**。ブロックが安全に読めないとき（閉じていない、
  入れ子の `{`、未終了のコメント）は今までどおり規則ごと捨てる（推測して「著者が書いていない宣言」を
  報告しない。A48）。セレクタの `css-parse` は今までどおり 1 件出す
- **(c) 対応外のタグを名指しするタイプセレクタは `UnsupportedTag` で報告する。** ① html は対応外の要素を
  透過するので、`body` `table` `ul` `li` `section` … を名指しした規則は**決して一致しない**。
  これは「未使用の CSS」ではなく「shashoku に存在しないタグへの指定」なので黙らない。
  **「正常な選択」と「黙った無視」の線引き**: クラス / ID / `*` が一致しないのは正常（報告しない。
  未使用の CSS を毎回警告すると `--strict` が使えなくなる）、対応外タグの名指しは無視（報告する）。
  種類は **`UnsupportedTag` を再利用**した（`ErrorKind` を増やさない。要素側の `<body>` と同じ識別子なので
  AI が同じ往復で両方を直せる）。位置はセレクタ、カンマ区切りは**その部分だけ**落として残りは適用する
  （`body, div { … }` は body の 1 件を報告して div には当てる）。宣言は (b) と同じく報告だけする。
  hint は「外側の `div` にクラスを付けて移す or 規則を削る」で、**移して同じ絵になることを CLI で確かめた**
  （背景・padding・本文色が外側の div で出る）
- **対応タグの表を style にも持った。** 正は ① html の `kSupportedTags` だが、style は html に依存しない
  （§2 の依存の向き）ので `css_parser.cpp` に同じ表を写した。食い違いが出たら style が誤報するので、
  タグを増やすときは両方を直す（テストは `SupportedTagSelectorsAreNotReported` が 15 タグを通す）
- **確かめたこと**: `docs/benchmark/2026-09-23/inputs/a_plain/case01.html` は 1 回の実行で出る診断が
  **61 → 71 件**（`unsupported-tag` +2 = `body` / `table` のセレクタ、`unsupported-property` +8 =
  捨てていた規則の中の `border-right` / `vertical-align`）、case04 は **40 → 55 件**（タグのセレクタ +6、
  宣言 +9）。消えた診断は 0 件。case04 の r1（`<code>` を `span` にした版）は **1 → 3 件**で、
  前は 3 往復かかった border-width → border-radius → padding-top が 1 回で出る。
  `examples/*.html` 5 本の PNG は main（2c7066c）の release バイナリの出力と**バイト一致**（絵は変えていない）

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

**(4') はみ出してよい量**（`Item::overhang_before` / `overhang_after`。ルビの掛け = JLREQ 3.3.8 を
行分割器の側から見たもの。A44）: アイテムが前 / 後ろの隣のアイテムに**掛けてよい量**（px、非負）。
行の幅は `advance − overhang_before − overhang_after` で測り、**行頭に来たアイテムの
`overhang_before` と行末に来たアイテムの `overhang_after` は落とす**（`trim_line_*` と同じ形。
版面の外に出さないため）。効いた量は (4) のアキ詰めと合わせて `Spacing` に負の値で返るので、
**描画側の手順は変わらない**。`min_content_width()` にも同じ規則で効く（区間の端では落ちる）。
**掛けてよい相手か**（JLREQ 3.3.8 の文字クラス）は呼び出し側が判定し、linebreak は量だけを
受け取る（何にも依存しない原則を保つ）。契約外の値（負・`advance` より大きい）は丸める:
行の幅が負になると「アイテムを足すと行の幅は増える」という前提が壊れ、行の決定が成り立たない。

**(5) あふれ処理**:
- `Oidashi`: (3) のまま。禁則文字は手前の文字を道連れにして次の行へ行く
- `Burasage`: 行末に来た句読点（`、。，．`）1 文字が収まらないとき、それを行の外に出す
  （`Line::hang`）。収まるならぶら下げない。句読点以外には効かない（追い出しになる）
- `Oikomi`: 貪欲法で決めた位置の次の分割可能位置までを、行内の約物の空き（(4) の空き量が上限）を
  詰めれば収められるなら、詰めて収める。詰め量は各約物の詰め可能量に比例配分する。
  収められなければ追い出し
- どのポリシーでも (A4) 禁則 > 幅。緊急分割（`Wrap` が `Normal` 以外）は分割可能位置が
  1 つもない行でだけ発動する

**(6) 不変条件**（ファジングで検査する）: 全アイテムがちょうど 1 行に属する / 行は空でない /
`overflows` でない行は `width <= available_width`（+ 許容誤差）/ 分割位置は必ず
`break_opportunities()` が true の位置か ForcedBreak の直後か緊急分割の発動
（= 両側のアイテムがともに `Normal` 以外の位置。(7)）/ `min_content_width()` の幅で組んだ行は
どれも `overflows` にならない / 同じ入力には同じ出力。
ファジングにはアイテムごとのポリシーをランダムな範囲で混ぜた入力と、**(4') の掛けを
ランダムに（契約を破る値も含めて）混ぜた入力**も含める。

**(7) アイテムごとのポリシー**（A23）: `Item::strictness` / `Item::wrap` は
インライン要素（`<span>`）での上書き。nullopt なら `Config` の値を使い、すべて nullopt なら
出力は `Config` だけを使っていたころと完全に同じ。境界の規則は:
- (1) の分割クラスの解決は**そのアイテム自身の** `strictness` で行う。(2) のペア表・文脈規則は
  解決済みのクラスに対して従来どおり働く。例外は loose の「直前が ID ならハイフン ‐ – の前で
  割ってよい」だけで、これは行頭に来る側（後ろのアイテム）の `strictness` で決める
- (5) の緊急分割は、**両側のアイテムがともに** `Wrap::Normal` 以外の位置でだけ起こす。
  発動条件と位置選びの優先順は変わらない。割れる位置が 1 つもなければ A4 ではみ出す
- `break_opportunities()` は `wrap` の影響を受けない（緊急分割は「分割可能位置」ではない）。
  `min_content_width()` は `strictness` と `wrap` の影響を受ける: 分割可能位置に加えて
  **両側がともに `Wrap::Anywhere` のクラスタ境界**でも区間を切る（CSS Text 3 §5.4。
  `BreakWord` では切らない。A35）。切った位置は緊急分割の候補そのものなので、
  返した幅は必ず達成できる

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
- run 分割: コードポイントごとにフォールバック列を cmap 引きし、**最初にそれを単色で描ける**
  フォントを採用。cmap にグリフがあっても、色データだけを持ち輪郭が空のグリフ（COLR のベース）は
  描けないので次のフォントに送る（`FontStore::has_drawable_glyph()`。A43）。
  同じフォントが続く区間をまとめて HarfBuzz に渡す。結合文字・異体字セレクタ・ZWJ は直前の
  文字と同じ run に入れる（別フォントに割らない）
- 豆腐: どのフォントでも描けないコードポイントは `ShapedCluster::missing` と
  `missing_reason`（`NotInAnyFont` / `ColorOnly`。文面のためだけの値。A43）で返し（**Shaper は
  溜めない**。警告を組み立てるのは ③ レイアウト。A31）、`□`（U+25A1）を
  **フォールバック列の順に全フォントから探して**（ここでも「単色で描けるか」で見る。A43）、
  最初に見つかったフォントのグリフを
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
  「全部の字が消えた PNG」になる前に一番早い段で落とす（A19）。
  **フォント全体ではなくグリフ単位で「色データだけ」のもの**（COLR/CPAL のベースで輪郭が空）は
  load のときに拾って覚え、`is_color_only_glyph()` / `has_drawable_glyph()` で引く（A43）。
  色データを持たないフォントでは 1 グリフも調べない（`color_probe_count()` が 0 のまま）
- テスト用フォント: リポジトリに置かず、CMake の configure 時に版（コミット SHA）とハッシュを
  固定してダウンロードする（`cmake/TestAssets.cmake`）。Noto Sans JP（OFL）+ 欧文フォント 1 つ
  （フォールバックのテスト用）。パスはコンパイル定義でテストに渡す。
  和文 2 本は **CLI に埋め込む既定フォントを兼ねる**ので `SHASHOKU_BUILD_TESTS=OFF` でも取得する
  （A38。欧文 1 本はテスト専用のまま）
- エラーメッセージは英語で書く（他のモジュールにそろえる）。`tests/text/font_store_test.cpp` の
  `ErrorMessagesAreAscii` が ASCII 以外の混入を見張る
- 受け入れ（DESIGN.md Phase 2）: 「こんにちは、世界のみんな。ABC😀」を 1 行でシェーピングでき、
  😀 だけが豆腐として報告される

### 3.6 html（①）

```cpp
namespace shashoku::html {
// 合成ルート "#root" を返す。非致命の問題は diagnostics に足して続行する（A46）
Result<Node> parse(std::string_view source, Diagnostics& diagnostics,
                   std::size_t max_nesting_depth = kMaxNestingDepth);
std::string dump_json(const Node& root);
}
```

- 入力は断片（`<html>` / `<body>` なしで `<div>…` から始まる）。トップレベルに複数ノード可
- 対応タグ: `div span p h1-h6 img ruby rt rp br style`。それ以外は `UnsupportedTag`
  （`html head body script …` も含めてエラー。集めて続行し、要素は透過にする。下記）。
  コメントと `<!DOCTYPE>` は読み飛ばす
- 対応属性: 共通 `style class id`、`img` は加えて `src width height alt`。それ以外は
  `UnsupportedAttribute`。属性の重複は `HtmlParse`（**残す属性だけ**。A49）。引用符は `"` `'` なし の 3 形式
- 空要素 `br img` は閉じタグなし（`<br/>` も可）。それ以外の要素の閉じ忘れ・対応しない終了タグ・
  入れ子の誤りは `HtmlParse`（WHATWG の暗黙の閉じ規則は実装しない。fail loudly）
- 文字参照: `&amp; &lt; &gt; &quot; &apos; &nbsp;` と数値参照（10 進・16 進）。未知の名前、
  範囲外・サロゲートの数値は `HtmlParse`。`<style>` の中身は生テキスト（文字参照を解決しない）。
  対応外の生テキスト要素（`script textarea title xmp iframe noembed noframes`）も同じ読み方で、
  中身は捨てる（A49）。**捨てる属性の値も生テキスト**（同）
- 入力が不正な UTF-8 なら `InvalidUtf8`。すべてのエラーに `SourceLocation` を付ける
- **集めて続行するもの（A46）**: `UnsupportedTag` と `UnsupportedAttribute` は `diagnostics.add_error()` に足し、
  **解析を続ける**。対応外の要素は**透過**として扱う（その開始タグ・終了タグは無いものとし、子は親の子として
  読む。閉じタグの対応は取る）ので、`<html><head><style>…</style></head><body>…</body></html>` のような入力でも
  中の `<style>` と本体の全問題が 1 回で出る。対応外の属性は捨てて要素は残す（値は読み切ってから捨てる）。
  **返る木は errors が 1 件でもあれば描画されない**（api が失敗にする）ので、透過の意味論は
  診断の網羅のためだけにある。診断は**文書順**（足した順）で足し、ここでは並べ替えない（整列は api）。
  透過の細部は **A49**:
  - HTML の空要素（`area base col embed hr input link meta param source track wbr`）は終了タグを待たず、
    開いている要素のスタックにも積まない（`<meta charset="utf-8">` の直後の `</head>` を壊さないため）
  - `/>` で閉じた対応外の要素はその場で終わる。対応する開始タグの無い終了タグ（`</table>` 単独）は
    `UnsupportedTag` を足して読み飛ばす。その名前の要素が開いているのに一致しないときは `HtmlParse`
  - 透過した要素も**スタックには積む**ので、`max_nesting_depth` の判定は透過を含む深さで行う
  - 透過した要素の境界でテキストノードは分かれる（描画されないので結果には出ない）
  - **透過した要素の属性は報告しない**（`UnsupportedAttribute` を足さない）。「そのタグが対応外」の
    1 件で足りる。重複の検査もしない（`<section class="a" class="b">` は `HtmlParse` にならない）
  - **捨てる属性の値は文字参照を検証しない**（透過した要素の全属性と、対応済みの要素の対応外の属性）。
    `<link href="…?family=Noto&display=swap">` が致命にならない。残す属性（`style` `class` `id`、
    `img` の `src` など）は今までどおり検証する
  - **対応外の生テキスト要素**（`script textarea title xmp iframe noembed noframes`）は、対応する
    終了タグまでを生テキストとして読み飛ばす（中の `<` と `&` を解釈しない。`UnsupportedTag` は 1 件、
    子は作らない）。終了タグが無ければ `HtmlParse`
  **その場で止めるもの**: `InvalidUtf8`、`HtmlParse`（閉じ忘れ・入れ子の誤り・**残す**属性の値の
  不正な文字参照・**残す**属性の重複・生テキスト要素の閉じ忘れ。安全に読み続けられない）、
  `LimitExceeded`。これらは今までどおり unexpected で返す
  （api が diagnostics に集めたものと合わせて 1 つの `RenderFailure` にする）
- `max_nesting_depth` を超える入れ子は `LimitExceeded`（位置つき。api は
  `RenderLimits::nesting_depth` を渡す）。入力が 4 GiB を超える場合も `LimitExceeded` だが、
  こちらは `SourceLocation::offset` が 32 bit であることによる絶対上限（A25）

### 3.7 style（②）

```cpp
namespace shashoku::style {
// ルートの ComputedStyle は初期値。非致命の問題は diagnostics に足して続行する（A46）
Result<StyledNode> resolve(const html::Node& root, Diagnostics& diagnostics,
                           std::size_t max_style_rules = kMaxStyleRules,
                           float max_length_px = kMaxLengthPx);
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
  `gap` `row-gap` `column-gap`、`background`（色のみ。`background-color` の別名）、
  `word-wrap`（`overflow-wrap` の legacy name alias。CSS Text 3 §5.4。名前の表で写し替えるだけで、
  カスケード・継承・計算値・ダンプの名前はすべて `overflow-wrap` と同じ。A35）。
  一覧にないプロパティは `UnsupportedProperty`、値が対応外なら `UnsupportedValue`
  （別名に対応外の値を書いたときの文面は**著者の綴り**のまま。`` `word-wrap: foo` is not supported … ``）
- **集めて続行するもの（A46 / A48 / A55）**: `CssParse`（宣言の単位で読み飛ばす。セレクタが読めなければ規則の単位）、
  `UnsupportedProperty` / `UnsupportedValue`（その宣言を捨てる）、`UnsupportedTag`（対応外のタグを
  名指しするセレクタ。A55）、`img` の `src` の欠落と
  `width` / `height` 属性の不正、計算値の検査で分かる `UnsupportedLayout`（inline への箱プロパティ、
  `writing-mode` の途中変更・トップレベルの食い違い）は `diagnostics.add_error()` に足して続行する。
  返る木は errors が 1 件でもあれば描画されない（判定は api の `Diagnostics::has_errors()`）。
  **その場で止めるもの**: `LimitExceeded`（規則数・長さの上限）と `Internal`。今までどおり unexpected
  - **読み飛ばしの単位**（`css_parser.cpp`）: 宣言が読めなければ次の `;` / `}` / 入力の終わりまで捨て、
    **後ろの宣言は効かせる**。セレクタが読めなければ規則の単位（`{…}` を対応づけて丸ごと、`;` で終わる
    `@import` はそこまで）。未終了のコメントだけは読み飛ばし先が決められないので、記録して残りを捨てる。
    捨てた宣言は「**書かれなかった**」扱いで、途中まで展開された longhand も残さない
  - **捨てる規則の宣言も、構文の診断だけは出す**（**A55 で A48 の「規則ごと捨てる」を改めた**）。
    セレクタが読めない規則と、対応外のタグしか名指ししていない規則は、**宣言ブロックの括弧が対応していて
    安全に読めるなら**通常の宣言パーサに通し、宣言レベルの診断（`UnsupportedProperty` /
    `UnsupportedValue` / 宣言単位の `CssParse`。hint も通常どおり）を出す。**適用はしない**
    （カスケードに入れないので、その規則を適用した前提の計算値の検査は出ない）。
    安全に読めないとき（閉じていない `{`、入れ子の `{`、未終了のコメント）は今までどおり規則ごと捨てる。
    位置順・`max_diagnostics` の扱いは他の診断と同じ
  - **対応外のタグを名指しするタイプセレクタは `UnsupportedTag`**（A55）。① は対応外の要素を透過するので、
    `body` `table` `ul` `li` `section` … を名指しした規則は**決して一致しない**。位置はセレクタ、
    1 セレクタにつき 1 件（要素ごとには増えない）。カンマ区切りは**その部分だけ**落として残りは適用する。
    hint は「外側の `div` にクラスを付けて移す or 規則を削る」。**一致しないクラス / ID / `*` は
    「正常な選択の結果」なので報告しない**（未使用の CSS を毎回警告すると `--strict` が使えなくなる）。
    対応タグの表は `css_parser.cpp` が ① の `kSupportedTags` の写しを持つ（style は html に依存しないため。
    タグを増やすときは両方を直す）
  - **順序**: 診断は「足した順」（`<style>` の規則 → トップレベルの `writing-mode` の食い違い → 木を
    前順に辿った各要素の `style` 属性と計算値の検査）。入力位置での整列は api が `Diagnostics::sort()` で行う
  - **二重に出さない**: 文書の `writing-mode` を決める先読み（`document_writing_mode`）は同じ要素を
    もう一度カスケードするので、そこでの診断は捨てる `Diagnostics` に流し、致命エラーも握り潰す
    （どちらも `build_element` が通るときに正しい順序で出る）
  - **計算値の診断の重複は style の中で落とす**（A48 の追記）。1 つの規則に複数の要素が一致すると
    計算値の検査（`validate()` と `document_writing_mode` の食い違い）が要素ごとに同じ診断を出すので、
    **同じ (kind, location, message) は 1 回だけ**足す。覚えるのは順序つきの `std::set`（`unordered_map` の
    反復順を出力に影響させない。DESIGN.md §3-5）で、`Diagnostics` が上限で捨てたものは覚えない
    （大きさは `max_diagnostics` で抑えられる）。宣言の単位の診断（`UnsupportedProperty` など）は
    もともと規則を 1 回しか読まないので対象外。core の `Diagnostics` は変えない
- **hint は `RenderError::hint` に入れ、`message` には混ぜない**（A46。機械側が分けて読める。以前は message の
  末尾に括弧で足していた）。`kPropertyHints` の規則「shashoku で同じ結果が出せると確かめた代替だけ」は変えない。
  足すもの: (a) ベンダー接頭辞（`-webkit-*` / `-moz-*` / `-ms-*` / `-o-*`）は「接頭辞を外す（対応表にあれば）」
  (b) **削ると危険な組**: `background-clip` / `-webkit-background-clip` は「`color: transparent` も外さないと
  文字が消える」(c) inline への箱プロパティの `UnsupportedLayout` は「宣言を削る（`display: block` にすると
  文の流れが切れる）」。hint の無いものは空のまま。
  `value_parser.cpp` の `kPropertyHints` に **未対応だと分かっているものだけ**を載せる。
  表に無い名前（綴り間違いなど）には何も足さない。**載せてよいのは shashoku で実際に
  同じ結果が出せると確かめた代替**と、**削ると危険な組の警告**だけで、代替が無いもの（縦中横）は
  「未実装」とだけ言う。接頭辞つきの名前は表を引く前に接頭辞を外して引き直すので、
  `-webkit-background-clip` は `background-clip` の hint に当たる
- **hint は無条件の置き換えにしない**（A48 の追記 / A53）。**見た目が変わる代替**（gradient → 単色、
  片側の `border` → 1px の div）は「変わる」と書き、**成立条件**があるなら条件を添える
  （`min-height`: 親が既定の `align-items: stretch` の flex なら削ってよい）。宣言だけから条件を
  判定できないときは代替を断定せず、ガイドの節番号（`docs/guide/writing-html-for-shashoku.md §4.3` など。
  英語版も同じ番号）を示す。等価な書き方が無い場合は「no equivalent」と書く。
  hint は英語で 1〜2 文
- **値レベルの hint**（A53）。プロパティ名では対応外だと分からないもの
  （`display: grid` / `display: inline-block` / `background` と `background-color` の `gradient(`）には、
  **値を見てから** hint を付ける。`value_parser.cpp` の `bad_value_with_hint()` が
  `bad_value()` と同じ message に hint だけを足す（core の `fail()` / `Error` は変えない）
- 名前の引き方は 3 段（`direct_hint_for()`）: (1) `kPropertyHints` の**完全一致**
  (2) 片側だけの `border-*` の接頭辞一致（`border-top` `-right` `-bottom` `-left` と、その
  `-width` / `-style` / `-color` の 16 通り。`border-top-left-radius` のような**角**には当てない）
  (3) `grid-template*` / `grid-auto-*` の接頭辞一致。完全一致が先なので、
  legacy の `grid-gap` / `grid-row-gap` / `grid-column-gap`（A35）は grid の hint に飲み込まれない
- 単位: `px` `em`、`0`（単位なし）。`%` は `width` と `flex-basis` のみ。`line-height` は
  `normal` / 数値 / px / em。色: `#rgb #rgba #rrggbb #rrggbbaa`、`rgb()` `rgba()`、
  CSS の色名、`transparent`、`currentColor`（border-color のみ）
- 継承するのは computed_style.hpp で「継承する」とした群。`inherit` キーワードは全プロパティで可。
  `em` は親の（`font-size` 自身は親の、それ以外は自分の）font-size で解決する
- 合成ルート `#root` は `display: block`、それ以外のプロパティは初期値（`writing-mode` だけ A1 の規則で決まる）。
  `img` の `width` / `height` 属性は px の数値として `attr_width` / `attr_height` に入れる（不正なら `UnsupportedValue`）
- **flex コンテナの直接の子要素は block 化する**（CSS Display 3 §2.7 / CSS Flexbox 1 §4。**A53**）。
  親の計算値の `display` が `flex` で、自分の計算値の `display` が `inline` なら `block` にする
  （作者が明示的に `display: inline` と書いていても block にする。仕様どおり）。**孫は変えない**し、
  テキストノードも変えない。block 化は**カスケードのあと**に行うので、計算値の検査もダンプ
  （`--dump-stage style`）も layout も block を見る。例外は 3 つで、どれも layout が inline のまま
  別扱いしているもの: `img`（置換要素。inline でも箱プロパティを取れる）、`ruby`（layout が意図的に
  無名アイテムの中へ inline のまま入れる）、`br`（強制改行）。`display: none` は none のまま
- `display: inline` の要素への `width height margin padding border` 指定は `UnsupportedLayout`
  （`img` を除く）。A53 のあと、ここに来るのは**文中の inline 要素**（と flex の子の `ruby` / `br`）だけで、
  flex の子の `span` は block 化されて通る。`writing-mode` の途中変更も `UnsupportedLayout`（A1）。
  **作者が書いた箱の宣言は全部報告する**（A55。1 宣言 = 1 件で、ショートハンドは展開列の先頭の名前で
  1 件。`<span style="border: …; border-radius: …; padding: …; margin: …">` なら border-width /
  border-radius / padding-top / margin-top の 4 件が 1 回で出る）。数える単位が「作者が書いた宣言」なのは
  `style` 属性の中では位置がすべて属性を指すからで、`Declaration::source_head`（展開列の先頭か）で数える。
  UA 由来は数えない
- `<style>` から読んだ規則が `max_style_rules` を超えたら `LimitExceeded`（位置つき）。
  セレクタの照合は「規則数 x 要素数」なので、規則の数そのものに上限が要る（A25）
- **出力の不変条件（A36）**: 返る木の `ComputedStyle` に入っている長さは、`font-size` を除いて
  すべて**有限で、絶対値が `max_length_px` 以内**である。`em` の乗算の結果（`1e38em x 16px`）も、
  px で直接書いた値（`3e38px`）も、同じ上限で止める。超えたら `LimitExceeded` + **宣言の位置**
  （`<style>` の中なら宣言そのもの、`style` 属性なら属性の位置）。`line-height` の倍率は
  「倍率 x その要素の font-size」が上限以内であることを、カスケードのあとに見る。
  `<img>` の `width` / `height` 属性も同じ上限。
  **対象外が 2 つある**: (1) `%` と `auto` は包含ブロックが要るので layout が解決する（A5）ので、
  ここでは検査できない。(2) `font-size` は A25 の `font_size_device_px`（scale 込みでより厳しい）が
  api で止める。ただし非有限な `font-size` だけはここで止める（`LimitExceeded` + 要素の位置）

### 3.8 layout（③）

```cpp
namespace shashoku::layout {
struct Options {
  float viewport_width;                  // 物理 px
  std::optional<float> viewport_height;  // 縦書きでは必須（InvalidOption）
  linebreak::Config line_break;          // strictness / wrap は CSS が上書きする
  float max_geometry_px;                 // 出口の検査の上限（A36。既定 length_px x dom_nodes）
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
  ブロックも**元要素の位置**（`BlockBox::location`。A36）を持つ。
  加えて豆腐の記録（`BoxTree::missing_glyphs`。A31。コードポイント・位置・理由
  （`text::MissingReason`。A43））を持つ。**絵には影響しない**
  （paint は読まない）が、api が `Warning` にし、`dump_json()` が出す。
  理由は報告順にも重複除去にも効かない（順序は今までどおり位置 → コードポイント）
- **紙面からのはみ出しの記録（`BoxTree::overflows`。A46）**: `struct ContentOverflow { SourceLocation location;
  float overflow_px; OverflowEdge edge; }` の列（`edge` は実装時に足した。下の細目）。layout の出口で箱を走査し、**箱の border box が出力の矩形の外に 0.5 px を超えて
  出ている**ものを見つける。出力の矩形は幅 `viewport_width`、高さは `viewport_height`（固定のときだけ。省略
  = 内容追従のときは縦方向にはみ出せないので横方向だけ判定する）。対象の箱はブロックの border box、行ボックス、
  置換要素（`<img>`）。**最も外側の該当要素ごとに 1 件**（祖先がはみ出していればその子孫は数えない）。行ボックスの
  はみ出しはそれを含むブロック要素の位置で報告する。`overflow_px` はその要素の超過量の最大（右・下・左・上の
  いずれか。負のマージンで左・上に出た場合も対象）。並びは位置の昇順。**絵には影響しない**（paint は読まない）が、
  api が `Warning{ContentOverflow}` にし、`dump_json()` が `"overflows": [{"location", "overflow_px", "edge"}]`
  として出す（1 件も無ければキーごと省く。豆腐と同じ流儀）。
  **見ないもの（受け入れ例として先にテストに書く）**: グリフのインク（イタリックの張り出し、ぶら下げで行ボックスの
  外に出た約物）は箱ではないので判定しない／ルビの注記は行ボックスの中にあるので単独では判定しない／
  固定幅の箱から文字がはみ出しても紙面の中なら対象外（箱からのはみ出しは別種 `BoxOverflow` として将来）／
  0.5 px 以下の差は丸めとして無視する
  - 実装は `layout/check_overflow.cpp`（`collect_overflows()`）。`check_geometry()` の**あと**に呼ぶので、
    引き算の入力はすべて有限で上限以内。失敗はしない（記録するだけ）。費用は O(N) の走査 1 回
  - **判定は物理座標で行う**: 論理座標のまま見ると縦書きで上下左右を取り違える（縦書きの「下」は論理の
    inline 方向の終端、「左」は block 方向の終端）。読み替えは paint と同じ式（A1。`vertical-rl` は
    `x = viewport_width - block_end`、`y = inline_start`）
  - **`overflow_px` の単位は CSS px**（scale を掛ける前）。レイアウトの座標が CSS px なので、api は
    割らずにそのまま `Warning::overflow_px` へ入れる（A50）
  - **辺を一緒に返す**: `ContentOverflow::edge`（`OverflowEdge{Right, Bottom, Left, Top}`。**物理**の 4 辺で、
    縦書きでも「下」は物理の下）。直し方が辺で変わる（下 = 背が高すぎる、右 = 幅が広すぎる）ので、
    `warning.hpp` が例に書いている文面（`"content overflows the canvas by 42.5px (bottom) at 12:3"`）を
    api が作れるようにする。同点なら 右 → 下 → 左 → 上 の順で先のものを採る（A50）
  - **合成ルート `#root` は候補にしない**（A50）。高さが `auto` のルートは中身に追随するので、候補にすると
    「最も外側の 1 件」が毎回ルートになり、位置が入力の先頭に化けて実際に突き出した要素を隠す
  - **画像断片（`ImageFragment`）も、行ボックスと同じく含むブロック要素の位置で報告する**（A50）。
    断片は `SourceLocation` を持たないため。行が収まっていても画像は行の外に出られる（行の inline 範囲は
    ブロックの content 幅で固定）ので、行が収まっている行の中だけ断片を見る
  - **同じ位置の複数件は 1 件にまとめ、`overflow_px` は最大を採る**（A50）。並べ替えは (offset, line, column)
    の昇順で安定ソート（`MissingGlyph::operator<` と同じ比べ方）
- **block**: 幅は親から降り、高さは子から戻る。`width: auto` は利用可能幅いっぱい。
  `margin: 0 auto` の中央寄せ。兄弟間のマージン相殺（A10）。子が inline と block の混在なら
  inline の連続を無名ブロックで包む
- **inline**: インライン整形文脈ごとに、(a) 空白の畳み込み（A14。文字を 1 つも持たない
  インラインボックスは「文字のない支柱」として位置つきで別に記録する。A42）
  → (b) **シェーピング属性**が
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
  ブロックの支柱と**空のインラインボックス**（A42）も、文字を持たないまま同じ計算に参加する。
  (b) のあと、豆腐のクラスタ（`ShapedCluster::missing` と `missing_reason`）を文字ごとの属性の
  表の位置の層と突き合わせて `LayoutEngine` に記録する（A31 / A43。段落が何度組まれても重複しない）。
  ファイルは段の境界で分けてある（A27 の末尾）。(a)〜(c) の結果 `PreparedParagraph` は
  行の幅に依らないので、固有寸法の計測と実際の配置で同じものを使える
  （`inline_intrinsic()` の min-content / max-content にもアイテムごとのポリシーが効く）
- **flex**（Phase 6）: 単一行のみ（`flex-wrap` は対応外）。CSS Flexbox §9 のアルゴリズムのうち、
  flex base size の解決 → grow / shrink の配分 → 交差軸の整列 → justify-content → gap。
  **アイテムの 2 つの下限を取り違えない**（A52）:
  - **flex base size**（§9.2）は `flex-basis`。auto なら主軸のサイズプロパティ（row は `width`、
    column は `height`）、それも auto なら内容サイズ（row は max-content、column は
    「交差軸の幅を決めて組んでみた高さ」）
  - **自動最小サイズ**（§4.5。`min-width: auto` / `min-height: auto` の content-based minimum size）は
    **min(specified size suggestion, content size suggestion)**。specified size suggestion は
    **主軸のサイズプロパティが definite ならその値**で、**`flex-basis` は入らない**。
    content size suggestion は主軸の min-content（column では内容の高さ）。
    主軸が不定の column では `%` の `height` は definite ではないので内容の高さが下限になる。
    `<img>` は内容サイズより縮めない
  - 配分（§9.7-4d）はこの下限で clamp する。**下限の合計がコンテナを超えればはみ出す**
    （紙面から出れば `content-overflow` の警告。§3.8 のはみ出し検査）
  アイテムの max-content / min-content は `kUnbounded` と `min_content_width()` で測る
  （`overflow-wrap: break-word` の分割位置は min-content に数えない。A35 = CSS Text 3 §5.4。
  したがって `flex: 1` + `break-word` の子は 1 行ぶんの幅より縮まない。Chrome と同じ）。
  column のアイテムの主軸サイズ（高さ）は「交差軸の幅を決めたうえで部分木を組んでみる」で出すが、
  その**計測**は `LayoutEngine::measure_block_size()` を通してメモする（A29。組み直していたのが
  issue #5 の 2^depth）。配置の `layout_block()` は従来どおり毎回 1 回ずつ実行するので、
  座標は 1 ビットも変わらない。固有寸法（`content_intrinsic()`）と準備済み段落も同じメモに乗る
- **ルビ**（Phase 7）: `<ruby>` 内の「親文字の並び + `<rt>`」を 1 組とし、組ごとに 1 つの Atomic。
  行ボックスはルビのぶん block-start 側に広がる。
  **組の内部は通常のインライン内容と同じ規則で配置する**（字間・装飾・背景。CSS Ruby 1 §2
  「ruby base は inline box として扱う」。A37）。Atomic なのは**行分割だけ**で、親文字は
  クラスタごとに `letter-spacing` 込みの送りで並ぶ（= 計測と配置が同じ数値を使う）。
  行の高さは親文字の**全クラスタ**の ascent / descent / `line-height` から求め、ルビはその
  外側に置く（注釈側は行の高さに参加しない。CSS Ruby 1 §3.4）。`<rt>` の `letter-spacing` は
  **適用しない**（A37 に根拠）。組の内部の**行分割**指定が効かないのは従来どおり（A28）。
  組の中の位置は **JLREQ 3.3.6 の 1:2:…:2:1 の配分**で決める（B = 親文字の送りの合計、
  R = ルビの送りの合計、E = 余り、n = 配る側のクラスタ数。A41 / issue #28）:
  1. 組の送りは `W = max(B, R)`。**配分は組の送りを変えない**（行分割位置・固有寸法・
     `Spacing` は 1 ビットも動かない。変えるのは組の内部の位置だけ）
  2. `B < R`: ルビはベタで組の先頭から。親文字は `E = R − B` を**端 `E/(2n)`、字間 `E/n`** で配る
  3. `B > R`: 親文字はベタで組の先頭から。ルビは `E = B − R` を同じ比率で配る
  4. `B == R`: 両方ベタ
  5. 端の空きは**ルビ文字サイズの全角**（`<rt>` の `font-size` の 1em）が上限で、上限で
     止めたぶんは字間に回す（JLREQ 3.3.6 の注。極端に短いルビで端だけが大きく開くのを避ける）。
     **クラスタが 1 つ**のときは配る字間が無いので中央に置き、この上限は適用しない
  6. **ルビの掛け**（JLREQ 3.3.8。A44）: `B < R` の組は、余り `E` のうち前後の文字に
     はみ出してよい量を**掛ける**。掛けてよい相手は隣の**平仮名・片仮名**（長音・小書きを含む。
     cl-15 / cl-16 / cl-10 / cl-11）だけで、漢字等（cl-19）・欧文・数字・約物・`<img>`・
     ほかのルビ組・`<br>` には掛けない。量は**前後の両方に掛けられるなら 1:1**、片側だけなら
     その側に寄せ、**片側あたりルビ文字サイズの全角**（`<rt>` の 1em）が上限。
     **行頭・行末では掛けない**（版面の外に出さない。落とすのは行分割器 = §3.4 (4')）。
     掛けたぶんだけ組の送りが縮むので、**改行位置・固有寸法は変わりうる**。
     掛けきれずに残った余りは規則 2〜5 で組の内部に配る（= 親文字は「行の送りの範囲」に、
     ルビは「組の箱」に置く。掛けが無ければ 2 つは同じ）

  配分で入れた空きは `letter-spacing` の字間と同じ扱いで、**手前のクラスタの背景が覆う**
  （隣り合う `<span>` の背景の間に隙間を開けない）。組の端に接する背景スコープは従来どおり
  組の箱の端まで（A37）。**背景の矩形は掛けを含まない**「行の送りの範囲」で決める（A44）
- **縦書き**（Phase 8）: 論理座標のまま。`TextStyle::direction = Vertical` で測るだけ
- **計算量**: 1 つの IFC を組む仕事は、アイテム数 N に対して線形。行ごとに段落全体を舐めたり、
  段落全体ぶんの作業バッファを確保したりしない（A22）。flex の入れ子は深さ d に対して d²
  （A29。メモがないと 2^d）。守れているかは計測カウンタ（A21）で
  検査する（`tests/layout/complexity_test.cpp`）。時間ではなく回数で見る
- **出力の不変条件（A36）**: 返る `BoxTree` に入っている数値 —— ブロック・行・画像断片・
  インライン背景の矩形（位置・大きさ・端）、枠線の幅と半径、padding、ベースライン、
  テキスト断片の font-size と inline 範囲、グリフごとの位置と offset —— は、**すべて有限で、
  絶対値が `Options::max_geometry_px` 以内**である。`layout()` は最後に `check_geometry()` で
  木を前順に 1 回辿ってこれを確かめ、破れていたら `LimitExceeded` + **その箱（断片なら断片）の
  入力位置**を返す。②の `length_px` と違い、この上限は**足し算の結果**に掛かるので
  `length_px x dom_nodes` で取る（根拠は A36。②を通った文書では発動しない）。
  後段（paint / raster）はこの前提に寄りかかってよい
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
文面は `MissingGlyph::reason` で 2 通り（`no font has a glyph for U+XXXX` /
`the glyph for U+XXXX has only color layers (COLR); drawn as tofu`。A43）。`WarningKind` は
どちらも `MissingGlyph`。
api は並べ替えない（順序を決めるのは ③ の仕事）。CLI は `warning[missing-glyph]: <detail>` を
stderr に出す。

**診断の組み立て（A46）**: api は `Diagnostics diag{opts.limits.max_diagnostics}` を作り、`html::parse` と
`style::resolve` に渡す。**②の出口のゲートは 1 か所**（`check_computed_limits` のあと）で、`diag.has_errors()` なら
layout に進まず `RenderFailure`（`std::move(diag).into_failure()`。整列は `into_failure` の中）を返す。
①② が unexpected（致命）を返したときは、その 1 件を `into_failure(extra)` で集めたものと合わせて返す
（`to_failure(diag, error)`。`LimitExceeded` などもこの経路なので、集めた対応外と一緒に出る）。
③ 以降の失敗は今までどおり 1 件で、`RenderFailure{errors = {その 1 件}} + 集まっていた警告`。

警告は ③ が成功した直後に `BoxTree::missing_glyphs`（A31）と `BoxTree::overflows`（A46 / A50）から作り、
`diag.add_warning()` に通してから（= 上限が掛かる）(offset, kind, codepoint, detail) で安定に整列する
（豆腐だけの列では A31 の順序と同じ）。一度上限に達したらその段の残りは作らない。ここで診断に入れておくので、
④以降で失敗したときも `RenderFailure::warnings` に載る。`ContentOverflow` の `detail` は
`content overflows the canvas by <px>px (<edge>) at L:C`（`<px>` は小数 1 桁、`<edge>` は
`to_string(OverflowEdge)`）。`overflow_px` は ③ が CSS px で出しているのでそのまま写し（A50。api は割らない）、
辺は `layout::OverflowEdge` → 公開 API の `OverflowEdge` に写す（③ は「はみ出していない」状態を持たないので
`None` にはならない）。

`opts.warnings_as_errors` が true で警告が 1 件以上あれば、PNG を作らず `RenderFailure` を返す: errors は警告 1 件に
つき `RenderError{kind = WarningAsError, message = warning.detail から末尾の " at L:C" を除いたもの,
location = warning.location, warning = warning.kind}`（`RenderFailure::warnings` は空）。**message に位置を残さない**のは、
`to_string(RenderError)` が `location` から同じ書式で付け直すため（残すと 1 行に位置が二重に出る。`--diagnostics json` の
`message` も同じ）。並べ替えは `into_failure(extra)` に任せる（errors の契約は
位置 → kind → message で、警告の並びとは規則が違う）。**判定は ⑥ の直後**に置く: strict は診断を増やすだけで
減らさない（`warnings_as_errors` を立てても ⑤b / ⑥ の `LimitExceeded` が隠れない）。
`dump()` も Box 以降の段で同じ組み立てをする（Dom / Style は layout に入らないので警告は出ない）。
`RenderResult::diagnostics_truncated` / `RenderFailure::truncated` は
`diag.truncated()` を写す。`max_diagnostics` の**実効の下限は 1**（`Diagnostics` が 0 を 1 として扱う）:
0 件だと対応外の入力でも `has_errors()` が false になり、診断なしで「成功」する穴ができる。**検査の順序はどの経路でも同じ**なので、同じ入力からは同じ診断が同じ順で出る。

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

CLI は `tools/shashoku/`: `shashoku input.html [--font A.otf [--font B.ttf …]] [--image name=path …]
-o out.png [--width N] [--height N] [--scale S] [--compression 0-9]
[--overflow oidashi|oikomi|burasage] [--dump-stage dom|style|box|display-list|svg]`。
エラーは `to_string(RenderFailure)` を stderr に出して終了コード 1（1 行 1 件、hint は `  hint: …` の行）。
値の範囲の検査は `render()` に任せる（オプションの正は 1 か所。CLI は形だけを見る）。

**A46 の CLI**: `--strict` は `warnings_as_errors` を立てる。**失敗したときは出力ファイルを作らない・上書きしない**
（`render()` が成功してから開いて書く。`-o` の既存ファイルは失敗時に触らない）。`--diagnostics json` は
**標準出力に JSON を 1 オブジェクト**で出す（成功でも失敗でも。人向けの stderr 出力は出さない）:
```json
{"ok": true, "width": 1200, "height": 630, "truncated": false,
 "errors": [{"kind": "unsupported-property", "message": "…", "hint": "…", "line": 3, "column": 14, "offset": 120, "warning": null}],
 "warnings": [{"kind": "missing-glyph", "detail": "…", "codepoint": 128512, "line": 3, "column": 1, "offset": 88, "overflow_px": 0, "edge": null},
              {"kind": "content-overflow", "detail": "…", "codepoint": 0, "line": 19, "column": 1, "offset": 700, "overflow_px": 430, "edge": "bottom"}]}
```
`line` / `column` / `offset` は位置が無ければ `null`、`hint` が無ければ `""`、`warning` は `WarningAsError` のとき
`"missing-glyph"` のような識別子、それ以外は `null`。`edge` は `content-overflow` のとき `"top"` / `"right"` / `"bottom"` / `"left"`
（最大の超過量を出した物理の辺。`Warning::overflow_edge`）、それ以外は `null`。成功時は `width` / `height` に出力の寸法、失敗時は `null`。
JSON を選んだときは `-o` が必須で、`--dump-stage` と併用できない（`InvalidOption` 相当の使い方の誤りとして
stderr に出し終了コード 2）。人向けの出力（既定）は、成功時に `wrote out.png (1200x630)` を stderr に 1 行出す
（`--height` 省略時の実際の高さが分かる。検証 B の指摘）。

実装した細部:

- `--diagnostics` の値は `human`（既定）と `json`。値を取るオプションなので、片方しか綴りが無いと
  誤りの文面が不自然になる（「`--diagnostics` は json のいずれかです」）。`human` を明示して書けるのは
  スクリプトで既定に戻したいときにも要る
- JSON は**改行を含まない 1 行 + 末尾の改行**（キーの順は上の例のまま）。`float` は元の値に戻せる最短表現
  （`430` / `42.5` / `0`）で、`codepoint` は 10 進数
- **JSON ライタは CLI の中に置く**（`src/core/json_writer.hpp` は使わない）。CLI は公開ヘッダだけを見る
  約束（`shashoku::shashoku` の PUBLIC な include は `include/` だけ）で、内部ヘッダを見に行くと
  `tests/api/public_header_check.cpp` が守っている境界が崩れる。必要なのは `"` `\` と制御文字の
  エスケープだけ（非 ASCII は UTF-8 のバイト列のまま出す）
- **入出力の失敗（入力を読めない・出力を書けない）は診断ではない**ので、`--diagnostics json` でも
  `error: …` を stderr に出し、JSON は出さない。PNG の書き出しは JSON を出す前に行う
  （書けなかったのに `"ok": true` を出さないため）
- **失敗時に出力ファイルを触らない**のは、`render()` が成功してから `-o` を開くことで満たす
  （`--strict` で失敗したときも既存のファイルはそのまま）。`tools/shashoku/cli_test.cmake` の
  `strict` / `content_overflow` ケースが、既存ファイルの中身が変わらないことと新しいファイルが
  作られないことの両方を見る

**CLI 層だけの機能**（A38 / A39。ライブラリには一切漏らさない）:

| | |
|---|---|
| 既定フォント | `--font` を省いたら `tools/shashoku/embedded.cpp` が `.incbin` で焼き込んだ Noto Sans JP を Regular → Bold の順に `FontSet` へ入れる。`--font` を書けばそちらだけを使う |
| `--version` | shashoku（公開 API の `version()`）・zlib / FreeType / HarfBuzz（`cmake/Dependencies.cmake` の版をコンパイル定義で渡す）・既定フォント（コミット SHA） |
| `--license` | 焼き込んだ `LICENSE` と `THIRD_PARTY_LICENSES`。SIL OFL 1.1 が求める「ライセンス文の同梱」を、バイナリ 1 つでも満たすため |
| 配布のビルド | `dist` プリセット = Release + `SHASHOKU_STATIC_RUNTIME`（`-static-libstdc++ -static-libgcc`。glibc は動的のまま。A39）+ `SHASHOKU_EMBED_DEFAULT_FONT` |

CLI が zlib / FreeType / HarfBuzz のヘッダを見ることはない（版は文字列で受け取る）。
検査は `tools/shashoku/cli_test.cmake`（`ctest -R Cli`）。`default_font` が「既定フォントの PNG と
`--font <同じ OTF>` の PNG がバイト単位で一致すること」を、`examples` が「`examples/*.html` の
先頭コメントに書いた**そのまま貼れる 1 行**が実際に動くこと」を見る。

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
**ルビを含む行の高さ**。最後のものは float の誤差ではなく**モデルの違い**で、横書きは
ascent ベース（`max_base_ascent + rt_ascent + rt_descent`）、縦書きは em ベース
（`max_base_font_size / 2 + rt_font_size`）で張り出しを出している。`font-size: 16px` の
ブロックで親文字だけ 24px にすると 横 46.328125 / 縦 37.375 になる。縦書きに ascent の
概念（行の中心軸からの上下）が無いためで、揃えるかどうかは Chrome と見比べてから決める（#15）。
