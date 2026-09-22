# Unicode の表の再生成

shashoku は Unicode 文字データベース（UCD）から起こした表を 3 つ持つ。
**どれも [scripts/gen_unicode_tables.py](../scripts/gen_unicode_tables.py) が生成する。手で編集しない。**

| 生成物 | 中身 | 生成元 | 使うところ |
|---|---|---|---|
| [src/linebreak/break_class_table.inc](../src/linebreak/break_class_table.inc) | UAX #14 Line_Break → `BreakClass` | `LineBreak.txt` | 行分割（ARCHITECTURE.md §3.4 (1)） |
| [src/text/vertical_orientation_table.inc](../src/text/vertical_orientation_table.inc) | UAX #50 Vertical_Orientation | `VerticalOrientation.txt` | 縦書きの字の向き（§3.5） |
| [src/layout/east_asian_width_table.inc](../src/layout/east_asian_width_table.inc) | East_Asian_Width の W / F | `EastAsianWidth.txt` | 空白の畳み込み（A14）と layout のテスト用計測器 |

生成物はコミットする。**ビルドに Python もネットワークも要らない**（製品コードとテストは実行時に
UCD を読まない。DESIGN.md §3-5「グローバル状態・ネットワーク禁止」）。
ネットワークに出るのはこのスクリプトだけ。

固定している版は **Unicode 18.0.0**。3 つの生成物の先頭に、生成元のファイル名と SHA256 が
書いてあるので、**同じ版から作られていることはファイルを見れば確認できる**。

```
$ head -3 src/text/vertical_orientation_table.inc
// このファイルは scripts/gen_unicode_tables.py が生成する。手で編集しない。
// 生成元: Unicode 18.0.0 の UCD
//   VerticalOrientation.txt  sha256 0803e096...
```

## 使い方

```bash
scripts/gen_unicode_tables.py             # 再生成してファイルを書く
scripts/gen_unicode_tables.py --check     # コミット済みの表と一致するか検査（不一致なら差分 + 終了 1）
scripts/gen_unicode_tables.py --fetch     # UCD をキャッシュに取るだけ（CI がキャッシュを温める）
scripts/gen_unicode_tables.py --print-sets  # break_class.cpp が手で持つ集合の UCD 由来の中身
```

- Python 3 の標準ライブラリだけで動く（外部パッケージを使わない）
- UCD のファイルは `build/ucd/<版>/` にキャッシュする。UCD 自体はコミットしない
- 取得したファイルの SHA256 がスクリプトの固定値と違えば**必ず失敗する**
- `--ucd-dir DIR` でローカルに置いたファイルを使える（ハッシュは同じように検査する）。
  `--offline` はキャッシュが無ければ取得せずに失敗する
- 出力は決定的（同じ入力 → バイト単位で同じ出力）

## Unicode の版を上げるとき

1. 新しい版のファイルの SHA256 を取る。

   ```bash
   curl -sO https://www.unicode.org/Public/19.0.0/ucd/LineBreak.txt
   sha256sum LineBreak.txt
   ```

2. `scripts/gen_unicode_tables.py` の `UCD_VERSION` と `UCD_FILES` の 3 つのハッシュを書き換える。
   **3 つとも同じ版に揃える**（表どうしの整合性はここでしか担保していない）。
3. `scripts/gen_unicode_tables.py --check` を走らせ、**差分を読む**。
   `--check` は行単位の差分に加えて「どのコードポイントの値が、何から何へ変わるか」を
   文字名つきで出す。和文に出てくる文字（かな・漢字・約物・絵文字）が動いていないかを見る。
   新しい Line_Break クラスが増えていればスクリプトが止まる
   （`LINE_BREAK_FOLDED` / `LINE_BREAK_KEPT` に寄せ先を足す。ARCHITECTURE.md §3.4 (1) も更新する）。
4. `scripts/gen_unicode_tables.py` で書き込む。
5. **行分割のテーブル駆動テストを通す**（`ctest --preset dev -R LineBreak`）。
   このテスト群が禁則処理の実質的な仕様書なので（DESIGN.md §10-2）、ここが落ちたら
   「表が新しくなった」のではなく「規則の解釈が変わった」可能性を先に疑う。
6. `ctest --preset dev` を全部通す。ゴールデン（`tests/golden/*.png`）が落ちたら、
   **差分画像を人が目で見てから**更新する（`SHASHOKU_UPDATE_GOLDEN=1`。CLAUDE.md「テスト方針」）。
7. `--print-sets` を走らせ、`src/linebreak/break_class.cpp` が手で持っている集合
   （`is_east_asian_bracket` = OP/CP かつ EAW が F/W/H、`is_wide_numeric_affix` = PO/PR かつ
   EAW が A/F/W）と見比べる。これらは生成の対象外なので、版上げのたびに目で照合する。
   2026-09-20 時点では 18.0.0 と一致している（29 個 / 19 個）。
8. `src/text/char_properties.cpp` の `kClusterExtenderTable` は UCD から起こしていない
   （日本語と基本ラテン、絵文字列に必要な範囲だけの手書き。§3.5）。版上げでは触らなくてよい。

## 全角表の「据え置き」は解消済み（issue #24）

`src/layout/east_asian_width_table.inc` は、かつてコミットされていた手起こしの
`kWideRanges`（**Unicode 15.1 相当**）を再現するために、生成の最後に
`LEGACY_WIDE_DEVIATIONS` という 32 件の例外リストを当てていた（issue #11 で表を生成に
切り替えたとき、振る舞いを変えないためにそうした）。issue #24 でこれを削除し、
**3 つの表とも UCD 18.0.0 そのまま**になっている。

- 32 件の内訳: 16.0〜18.0 で N → W になった / 追加された 25 件、15.1 の時点で W なのに
  写し漏れていた 3 件（**U+1B155 小書きカタカナ「コ」**、U+2FFC..2FFF / U+31EF 漢字構成記述文字）、
  未割り当ての穴をまたいで範囲をつなぎ余分に全角にしていた 4 件
- 「この文字は据え置くべきだ」という判断は 1 件も無かった（A20 に反する上書きが残っていただけ）
- 実害: `<p>あ\n𛅕い</p>`（U+1B155）の畳み込みで空白が消えず、縦書きではその空白が
  `sideways` の断片として独立して 1 本の断片が 3 本に割れていた（A14 / CSS Text 3 §4.1.3）
- 影響の検証: ゴールデン 16 枚は 1 ビットも変わらず、`examples/*.html` の `--dump-stage box` と
  PNG も修正前のバイナリとバイト一致した。32 区間のコードポイントはテストにも
  `examples/` にも出てこないため
- 回帰テスト: `tests/layout/east_asian_width_test.cpp`（32 区間の代表と対照、
  分類ごとの畳み込み、ノード境界をまたぐ場合、横書き・縦書きの断片）

いまは `--check` が終了 0 になるので、**CI の lint ジョブがこれを検査する**
（`.github/workflows/ci.yml`。UCD は `build/ucd/<版>/` にキャッシュし、キャッシュがあれば
`--offline` でネットワークに出ない）。表を手で触った / 版だけ上げて再生成を忘れた、は CI で止まる。

## East Asian Width の表が linebreak と layout に 2 つあるのはなぜか

`linebreak` は **何にも依存しない**（core にも依存しない）のが設計の約束
（CLAUDE.md「ディレクトリ構成」、ARCHITECTURE.md §2）。行分割器をレイアウトから切り離して
おくと、テーブル駆動テストがフォントもレイアウトもなしに書ける（DESIGN.md §10-2）。
だから `layout` の `is_fullwidth()` を呼ぶわけにはいかず、行分割器が必要とする範囲
（UAX #14 LB30 の「東アジア幅の括弧」）だけを `break_class.cpp` の
`is_east_asian_bracket()` が 29 個の集合として持っている。

つまり重複は意図したもので、問題は「同じ元データから作られている保証が無いこと」だった。
それはこのスクリプトが埋める:

- `break_class_table.inc` と `east_asian_width_table.inc` は同じ `UCD_VERSION` から生成される
- `is_east_asian_bracket()` / `is_wide_numeric_affix()` は `--print-sets` で照合できる

## UCD の公式テストデータ（LineBreakTest.txt）と照合するか

いまは照合していない。見立ては次のとおり（Unicode 18.0.0 の `auxiliary/LineBreakTest.txt`、
全 19,346 行で数えた）:

- shashoku は UAX #14 のクラスのうち 17 個を受け皿に寄せている（AI SG XX SA CB HL → AL、
  HH → HY、JL JV JT H2 H3 → ID、AK AP AS VF VI → AL）。寄せたクラスの文字を含む行は
  期待値と食い違って当然なので除くと、残るのは **10,252 行（53%）**
- さらに shashoku は和文向けの仕立て（CSS Text 3 §5.3 の strictness、JLREQ の禁則、
  `Config::extra_*`）を既定で入れている。これが効くクラス（CJ NS OP CL CP IS EX IN B2 PO PR
  HY BA）を含む行も除くと **3,411 行（18%）** しか残らない。残るのは AL・ID・NU・SP・
  BK/CR/LF・CM・ZW/ZWJ・GL・WJ・RI が中心で、**禁則処理の中核はほとんど検査できない**
- 仕立てを切った「素の UAX #14」モードを `linebreak::Config` に足せば 53% まで戻せるが、
  そのモードは製品では一度も使われない。テストのためだけの経路を本番のコードに足すことになる

結論: **費用に見合わない。**`LineBreaker::break_opportunities()` は公開されているので
実装自体は難しくない（テストデータをパースして `÷` / `×` と突き合わせるだけ）が、
検査できるのは「寄せていない・仕立てていない」部分だけで、そこは表がそのまま効く素直な領域。
表の正しさは `--check`（生成元との機械的な一致）で担保し、禁則の正しさは
JIS X 4051 / JLREQ を出典に書いたテーブル駆動テストで担保する、という今の分担を続ける。

## CI での検査（issue #24 で入れた）

lint ジョブ（`.github/workflows/ci.yml`）が `scripts/gen_unicode_tables.py --check --offline` を
走らせる。「表を手で触った」「版だけ上げて再生成を忘れた」はここで止まる。

- 取得するのは 3 ファイル・約 1 MB。`build/ucd/` を `actions/cache` で保持する
  （鍵は `scripts/gen_unicode_tables.py` のハッシュ。`UCD_VERSION` がこのファイルにあるため）
- 取得するのはキャッシュが外れたときの `--fetch` ステップだけ。検査そのものは `--offline` なので、
  **ネットワークが落ちても lint は落ちない**（キャッシュがあるかぎり）
- 版を上げた PR は、その 1 回だけ新しい版を取りに行く（スクリプトが変わるので鍵も変わる）
