#pragma once

// api モジュールの内部ヘッダ。公開型 LoadedFonts / LoadedImages の中身
// （`src/` のフォント・画像の表）に触れる窓口はここだけ。
// このヘッダを include してよいのは src/api/*.cpp だけ。

#include <string>
#include <vector>

#include "core/bitmap.hpp"
#include "shashoku/loaded_fonts.hpp"
#include "shashoku/loaded_images.hpp"
#include "text/font_store.hpp"

namespace shashoku::detail {

// デコード済みの画像テーブルへの参照。names と images は同じ長さ・同じ順で、
// 添字がそのまま ImageId（A12）。
struct ImageTable {
  const std::vector<Bitmap>* images = nullptr;
  const std::vector<std::string>* names = nullptr;
};

struct LoadedFontsAccess {
  // ムーブ済みの LoadedFonts では nullptr（呼び出し側が InvalidOption にする）。
  [[nodiscard]] static const text::FontStore* fonts(const LoadedFonts& loaded) noexcept;
};

struct LoadedImagesAccess {
  // ムーブ済みの LoadedImages では images / names が nullptr。
  [[nodiscard]] static ImageTable table(const LoadedImages& loaded) noexcept;
};

}  // namespace shashoku::detail
