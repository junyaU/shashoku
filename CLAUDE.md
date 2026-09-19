# shashoku（写植）

日本語組版特化の HTML→PNG レンダリングエンジン（C++23）。
**設計の唯一の正は [docs/DESIGN.md](docs/DESIGN.md)。** 設計判断・レビュー・提案の前に必ず該当節を読むこと。
設計書と矛盾する提案をするときは、矛盾していることを明示した上で理由を述べる。

やりとりは日本語。識別子は英語、コメントは日本語でよい。

## 役割分担（最重要 / DESIGN.md §11）

このプロジェクトは学習目的を兼ねる。**コアは人間が自分の手で書く。**

- **Claude が実装コードを書いてはいけない領域**（頼まれても、まず擬似コード・レビュー・仕様要約で支援する）:
  PNG エンコーダ / ラスタライザ・合成 / レイアウトエンジン（block・inline・flexbox）/ 行分割器（禁則）/
  HTML・CSS パーサとスタイル解決
- **Claude の担当**:
  - 設計の壁打ち・レビュー（DESIGN.md との整合性チェックを含む）
  - CMake / CI / clang-format / clang-tidy などの基盤整備
  - テストケースの列挙と整備（特に禁則のテーブル駆動テストの網羅）
  - FreeType / HarfBuzz の API 調査と最小サンプル
  - 仕様の調査と要約（UAX #14、JIS X 4051 / JLREQ、WHATWG HTML、CSS Text、PNG 仕様）
- コアのバグを見つけたら、直さずに「どこが・なぜ・どう確かめたか」を報告する。修正は頼まれてから。
- テストを書くときは、未実装の API を勝手に決めない。シグネチャは人間が決めたものに合わせる。

## 設計原則（DESIGN.md §3 の要約。レビュー時のチェックリスト）

1. **一方向パイプライン**: DOM → スタイル付きツリー → ボックスツリー → ディスプレイリスト → Bitmap → PNG。後段が前段を書き換えない
2. **重いデータは ID で引き回す**: フォント実体は FontStore だけが所有。ツリーには FontId / glyph_id のみ
3. **各段はダンプ可能**（`--dump-stage`）。ダンプできない中間表現を作らない
4. **行分割器は独立モジュール**: レイアウトにもフォントにも依存させない。TextMeasurer は注入する
5. **純粋関数**: 同じ入力 → バイト単位で同じ PNG。グローバル状態・時刻・乱数・ネットワーク・ロケール禁止。
   `unordered_map` の反復順やポインタ値を出力に影響させない
6. **fail loudly**: 未対応のタグ / プロパティ / 値はエラー（原因の入力位置つき）。黙って無視しない。豆腐だけは警告で続行

機能追加の判定は一問だけ: **「それは日本語の文章を正しく組むことに寄与するか？」** No なら入れない。

## ビルドとテスト

必要なもの: CMake 3.22+、Ninja、clang-18 + libc++-18（`std::expected` のため。g++ 11 では不可）。

```bash
cmake --preset dev            # configure（初回は zlib / GoogleTest を取得）
cmake --build --preset dev    # ビルド
ctest --preset dev            # 全テスト
ctest --preset dev -R Png     # 名前で絞る
scripts/format.sh             # clang-format（--check で検査のみ）
scripts/tidy.sh               # clang-tidy（build/dev の compile_commands.json を使う）
```

プリセット: `dev`（Debug）/ `asan`（ASan+UBSan）/ `release` / `gcc`（libstdc++ での移植性確認。`CXX=g++-14` 等で指定）。
メモリを触るコード（ラスタライザ、PNG、パーサ）を変更したら `asan` でもテストを回す。
コミット前に `scripts/format.sh` と `scripts/tidy.sh` を通す（CI は両方を `-Werror` 相当で検査する）。

## ディレクトリ構成

```
include/shashoku/   公開 API（DESIGN.md §8）。ここに置いたものは互換性を背負う
src/<module>/       内部実装。パイプラインの段ごとにモジュールを切る（下記）
tests/              GoogleTest。<module>_test.cpp、ゴールデンは tests/golden/、フォントは tests/assets/
cmake/              CompilerOptions.cmake（警告・決定性フラグ）、Dependencies.cmake（FetchContent）
scripts/            format.sh / tidy.sh
docs/               DESIGN.md
```

モジュール名の予定（フェーズが来たら作る。空ディレクトリは先に作らない）:
`png`（⑥ Phase 0）→ `raster`（⑤b Phase 1）→ `text`（④ FontStore・シェーピング Phase 2）→
`layout`（③ Phase 3）→ `linebreak`（③ の中核 Phase 4）→ `html` / `style`（①② Phase 5）

- ソースを足したら `src/CMakeLists.txt` に 1 行追加（GLOB は使わない）
- テストを足したら `tests/CMakeLists.txt` に `shashoku_add_test(<name> <sources>)` を追加
- モジュール間の依存はパイプラインの向きにだけ許す。`linebreak` は他のどのモジュールにも依存しない

## コード規約

- C++23。エラーは `std::expected<T, E>`。**例外を投げない**（ライブラリ境界を越えさせない）。RTTI に依存しない
- 命名（.clang-tidy で強制）: 型 `CamelCase` / 関数・変数 `snake_case` / private メンバ `name_` / 定数 `kCamelCase` /
  enum 値 `CamelCase` / 名前空間 `shashoku`。ファイル名は `snake_case.hpp` / `.cpp`、ヘッダは `#pragma once`
- 整形は .clang-format（Google ベース、100 桁）。手で整形を議論しない
- include は `src/` 起点（`#include "linebreak/line_breaker.hpp"`）、公開ヘッダは `"shashoku/..."`
- 中間表現は素の struct + `std::variant`。継承は注入点（TextMeasurer など）に限る
- 浮動小数点: `-ffast-math` 禁止、`-ffp-contract=off` は外さない（決定性のため。理由は cmake/CompilerOptions.cmake）
- 警告は `-Werror`。抑制するときは最小範囲で、理由をコメントに書く（`// NOLINT(check-name): 理由`）

## 依存ライブラリ

- **すべて FetchContent でバージョン + SHA256 固定**（cmake/Dependencies.cmake）。システムのライブラリを
  `find_package` で拾わない: 依存の版が変わると出力バイト列が変わり、ゴールデンテストが崩れる
- 依存ターゲットは `shashoku_mark_system()` でヘッダを SYSTEM 扱いにする（他人のマクロで `-Werror` が落ちるのを防ぐ）
- 現在: zlib 1.3.2、GoogleTest 1.17.0。FreeType / HarfBuzz は Phase 2 で追加
- 依存を増やす前に DESIGN.md §7「自作する / 借りる」の表と照合する

## テスト方針（DESIGN.md §10）

- 行分割器はテーブル駆動: `{入力, 幅, ポリシー} → 期待される行分割`。このテスト群が実質的な仕様書。
  ケースには出典（JIS X 4051 / JLREQ / UAX #14 の該当規則）をコメントで添える
- ゴールデンテストはピクセル完全一致。期待画像の更新は必ず人間が差分を目視してから
- `tests/toolchain_test.cpp` は環境の配線確認用。製品コードのテストをここに足さない
