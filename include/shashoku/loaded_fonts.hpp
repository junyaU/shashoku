#pragma once

#include <cstddef>
#include <expected>
#include <memory>

#include "shashoku/error.hpp"
#include "shashoku/font_set.hpp"

namespace shashoku {

class LoadedFonts;

namespace detail {
// 実装（src/api/）が中身を取り出すための窓口。ここには名前しか出さない。
struct LoadedFontsAccess;
}  // namespace detail

// 解釈済みのフォント（ARCHITECTURE.md A33）。`FontSet` のバイト列を一度だけ解釈して持つ、
// **読み取り専用の共有資源**で、何本の `render()` にまたがって、何スレッドから同時に
// 使ってもよい。OG 画像をリクエストごとに作るような使い方で、フォントのバイト列の
// 二重コピー（3 本で約 9 MiB）と解釈を実行ごとに繰り返さずに済む。
//
//   auto fonts = shashoku::LoadedFonts::prepare(font_set);
//   if (!fonts) { /* エラー処理 */ }
//   const auto a = shashoku::render(html_a, *fonts);   // 何度でも、何スレッドからでも
//   const auto b = shashoku::render(html_b, *fonts);
//
// **出力は変わらない**: 同じ HTML・同じ `RenderOptions` なら、共有しても毎回 `FontSet` から
// 作り直しても、バイト単位で同じ PNG と同じ警告が出る。過去の実行にも同時実行にも
// 影響されない（DESIGN.md §3-5）。
//
// スレッドの約束:
//   - `prepare()` が返ったあとは完全に読み取り専用。同時に何本の `render()` から参照しても
//     よい（内部に可変の状態はなく、FreeType のハンドルも持たない）
//   - ただし**破棄とムーブ代入は例外**で、その `LoadedFonts` を使っている `render()` と
//     同時に行わないのは利用者の責務（ふつうの C++ のオブジェクトと同じ規則）
//
// エラー: `FontSet` が空なら `NoFonts`、解釈できないバイト列・輪郭を持たないフォント
// （埋め込みビットマップ専用のカラー絵文字など）は `FontLoad`、確保に失敗したら `OutOfMemory`。
class LoadedFonts {
 public:
  [[nodiscard]] static std::expected<LoadedFonts, RenderError> prepare(const FontSet& fonts);

  LoadedFonts(LoadedFonts&&) noexcept;
  LoadedFonts& operator=(LoadedFonts&&) noexcept;
  LoadedFonts(const LoadedFonts&) = delete;
  LoadedFonts& operator=(const LoadedFonts&) = delete;
  ~LoadedFonts();

  // 読み込んだ face の本数（TTC / OTC は展開後の本数）。
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

 private:
  friend struct detail::LoadedFontsAccess;

  struct Impl;  // FreeType / HarfBuzz も `src/` の型も、この宣言から先には出てこない
  explicit LoadedFonts(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace shashoku
