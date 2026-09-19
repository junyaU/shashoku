# shashoku（写植）

日本語組版特化の HTML→PNG レンダリングエンジン（C++23）。
設計の正は 2 つ: [docs/DESIGN.md](docs/DESIGN.md)（何を・なぜ作るか）と
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)（どう作るか。モジュール仕様・契約ヘッダ・設計判断の記録）。
**実装・レビュー・提案の前に、必ず該当節を読むこと。** 設計と矛盾する変更をするときは、
矛盾していることを明示した上で理由を述べ、ARCHITECTURE.md §1 の判断記録を更新する。

やりとりは日本語。識別子は英語、コメントは日本語でよい。

## 役割分担

2026-09-19 にユーザーの指示で方針を変更した（DESIGN.md §11 の「コアは人間が書く」は失効）:
**実装は Claude のサブエージェントが行い、メインの Claude は設計とオーケストレーションを担当する。**

- **オーケストレーター（メインの Claude）**: 詳細設計（ARCHITECTURE.md と契約ヘッダ）、作業の分割と
  サブエージェントの起動、成果物のレビュー・検証・統合。製品コードの実装は自分で書かず委譲する
- **実装エージェント**: 割り当てられたモジュール（`src/<module>/` と `tests/<module>/`）だけを変更する。
  - 契約ヘッダ（ARCHITECTURE.md 冒頭の表）と他モジュールのファイルは勝手に変えない。
    契約の誤り・不足に気づいたら最小限の変更にとどめ、何をなぜ変えたかを最終報告に必ず書く
  - 完了条件: `dev` / `asan` プリセットでビルド（`-Werror`）とテストが通り、`scripts/format.sh --check` と
    `scripts/tidy.sh` が通ること。通っていないものを「完了」と報告しない。未完・既知の不具合は隠さず書く
  - テストは実装と同じ重みの成果物。仕様（ARCHITECTURE.md §3）の各項目に対応するテストを書く

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
libc++-18 が未インストールのマシンでは素の `cmake --preset` は通らない。その場合はオーケストレーターが
用意したラッパー（`dev configure|build|test|all <preset>`。パスは作業指示に書かれる）を使う。

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
src/<module>/       内部実装。パイプラインの段ごとに 1 モジュール = 1 静的ライブラリ（shashoku::<module>）
tests/<module>/     GoogleTest（実行ファイル名は <module>_test）。end-to-end は tests/integration/、期待画像は tests/golden/
tools/shashoku/     CLI
cmake/              CompilerOptions（警告・決定性フラグ）/ Modules（shashoku_add_module, shashoku_add_test）/ Dependencies（FetchContent）
scripts/            format.sh / tidy.sh
docs/               DESIGN.md
```

モジュール一覧と依存の向きは ARCHITECTURE.md §2。未着手のモジュールのディレクトリは先に作らない。

- モジュールを足す: `src/<module>/CMakeLists.txt` で `shashoku_add_module(<module> SOURCES ... DEPS ...)`、
  `tests/<module>/CMakeLists.txt` で `shashoku_add_test(<module>_test SOURCES ... LIBS shashoku::<module>)`。
  `src/CMakeLists.txt` と `tests/CMakeLists.txt` は存在するディレクトリを自動で拾うので触らない
- ソースは明示的に列挙する（GLOB は使わない）
- モジュール間の依存は ARCHITECTURE.md §2 の表の向きにだけ許す。`linebreak` は何にも依存しない（core にも）

## コード規約

- C++23。エラーは `std::expected<T, E>`。**例外を投げない**（ライブラリ境界を越えさせない）。RTTI に依存しない
- 命名（.clang-tidy で強制）: 型 `CamelCase` / 関数・変数 `snake_case` / private メンバ `name_` / 定数 `kCamelCase` /
  enum 値 `CamelCase` / 名前空間 `shashoku`。ファイル名は `snake_case.hpp` / `.cpp`、ヘッダは `#pragma once`
- 整形は .clang-format（Google ベース、100 桁）。手で整形を議論しない
- include は `src/` 起点（`#include "linebreak/line_breaker.hpp"`）、公開ヘッダは `"shashoku/..."`
- 失敗しうる関数は `Result<T>`（`core/result.hpp`）を返し、`fail(kind, message, location)` で作る
- 中間表現は素の struct + `std::variant`。継承は注入点（TextMeasurer / GlyphSource）に限る
- 浮動小数点: `-ffast-math` 禁止、`-ffp-contract=off` は外さない。layout / raster では `pow` `exp` `sin` 等の
  libm 依存の関数を使わない（決定性のため。ARCHITECTURE.md A9）
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
