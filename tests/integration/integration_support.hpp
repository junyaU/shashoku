#pragma once

#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/result.hpp"
#include "png/png.hpp"
#include "shashoku/shashoku.hpp"
#include "support/golden.hpp"
#include "text/test_fonts.hpp"

// end-to-end のテスト（ARCHITECTURE.md §4）。render() を通した PNG を
// デコードしてゴールデンと比べる。フォントは cmake/TestAssets.cmake が版を固定して落とす。
namespace shashoku::test {

// 和文のみ（Regular → Bold）。font-weight: bold が別ファイルに解決されることを使いたいので
// 2 つ入れる。
inline FontSet japanese_fonts() {
  FontSet fonts;
  fonts.add(text::assets::noto_sans_jp_regular());
  fonts.add(text::assets::noto_sans_jp_bold());
  return fonts;
}

// 欧文 → 和文の順。和文は欧文フォントにグリフがないのでフォールバックで拾われる
// （ARCHITECTURE.md §3.5）。絵文字はどちらにも無いので豆腐になる。
inline FontSet latin_then_japanese() {
  FontSet fonts;
  fonts.add(text::assets::noto_sans());
  fonts.add(text::assets::noto_sans_jp_regular());
  return fonts;
}

// render() → PNG → Bitmap。失敗したらその場でテストを落とす。
inline Bitmap render_bitmap(std::string_view html, const FontSet& fonts,
                            const RenderOptions& options) {
  const auto result = render(html, fonts, options);
  if (!result) {
    ADD_FAILURE() << "render: " << to_string(result.error());
    return {};
  }
  Result<Bitmap> bitmap = png::decode(result->png);
  if (!bitmap) {
    ADD_FAILURE() << "png::decode: " << to_string(bitmap.error());
    return {};
  }
  EXPECT_EQ(static_cast<int>(bitmap->width), result->width);
  EXPECT_EQ(static_cast<int>(bitmap->height), result->height);
  return std::move(*bitmap);
}

// 幅だけ指定して高さは内容に追従させる（ゴールデンの既定）。
inline RenderOptions options_for(int width) {
  RenderOptions options;
  options.viewport_width = width;
  return options;
}

}  // namespace shashoku::test
