#pragma once

#include <cstddef>
#include <expected>
#include <memory>

#include "shashoku/error.hpp"
#include "shashoku/image_set.hpp"
#include "shashoku/limits.hpp"

namespace shashoku {

class LoadedImages;

namespace detail {
// 実装（src/api/）が中身を取り出すための窓口。ここには名前しか出さない。
struct LoadedImagesAccess;
}  // namespace detail

// デコード済みの画像（ARCHITECTURE.md A33）。契約は `LoadedFonts` と同じ:
// `prepare()` が返ったあとは読み取り専用の共有資源で、何本の `render()` から、
// 何スレッドから同時に使ってもよい（破棄とムーブ代入だけは利用者が直列化する）。
//
// フォントより効く場面が多い: 全面の背景画像（1200x630）の PNG デコードは実測 16 ms で、
// `render()` 1 回の約 26% にあたる。テンプレートの素材を使い回すなら、ここを共有すると
// そのぶんがまるごと消える。
//
// `limits` は「大きな確保の直前」の判定（A25 の (c)）に使う: 1 枚あたりの画素数
// （`image_pixels`）と合計（`total_image_pixels`）を、デコードして画素を確保する**前**に見る。
// `render()` 側でも `opts.limits` で同じ検査をやり直すので、ここと `render()` に違う
// `RenderLimits` を渡しても「同じ HTML + 同じ limits なら同じ結果」は崩れない
// （厳しい方が効く）。
//
// エラー: 同じ名前を 2 回足した `ImageSet` は `InvalidOption`、解釈できないバイト列は
// `ImageDecode`、上限超過は `LimitExceeded`、確保に失敗したら `OutOfMemory`。
class LoadedImages {
 public:
  [[nodiscard]] static std::expected<LoadedImages, RenderError> prepare(
      const ImageSet& images, const RenderLimits& limits = {});

  LoadedImages(LoadedImages&&) noexcept;
  LoadedImages& operator=(LoadedImages&&) noexcept;
  LoadedImages(const LoadedImages&) = delete;
  LoadedImages& operator=(const LoadedImages&) = delete;
  ~LoadedImages();

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

 private:
  friend struct detail::LoadedImagesAccess;

  struct Impl;  // `src/` の型は、この宣言から先には出てこない
  explicit LoadedImages(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace shashoku
