# shashoku（写植）

**日本語の文章を絶対に破綻させずに、HTML から PNG を一発で生成する組版エンジン。**

禁則処理・縦書き・ルビ・フォントフォールバックを備え、関数 1 個で PNG バイト列を返す C++23 ライブラリ。
OG 画像のように「任意の日本語文字列を流し込んでも組版が壊れない」ことを保証するのが目的。

> 🚧 開発初期（Phase 0 着手前）。まだ何も描けません。設計は [docs/DESIGN.md](docs/DESIGN.md) を参照。

## ビルド

必要なもの: CMake 3.22+ / Ninja / clang-18 + libc++-18（C++23 の `std::expected` を使うため）

```bash
# Ubuntu 22.04（apt.llvm.org の llvm-toolchain-jammy-18 リポジトリが必要）/ 24.04
sudo apt install ninja-build clang-18 clang-format-18 clang-tidy-18 libc++-18-dev libc++abi-18-dev pngcheck
```

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

GCC を使う場合は 13 以上: `CXX=g++-14 cmake --preset gcc`

## やらないこと

JavaScript の実行 / 外部リソースの取得 / grid・float・table / CJK 以外の複雑スクリプト / ブラウザとのピクセル一致。
対応外のタグ・CSS は黙って無視せず、エラーで落とします（fail loudly）。詳細は [DESIGN.md §4](docs/DESIGN.md)。
