#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/result.hpp"

// ゴールデンテストの比較ヘルパー（DESIGN.md §10-1 / ARCHITECTURE.md §4）。
// 期待画像は tests/golden/<name>.png、比較はピクセル完全一致。
// 不一致なら <SHASHOKU_TEST_OUTPUT_DIR>/<name>.{actual,expected,diff}.png を書き出す。
//
//   EXPECT_TRUE(shashoku::test::expect_golden(bitmap, "rect_overlap"));
//
// 環境変数 SHASHOKU_UPDATE_GOLDEN=1 で走らせると、比較せずに期待画像を書き出して成功を返す。
// **期待画像の追加・更新は必ず人間が差分を目で見てから**（CLAUDE.md / ARCHITECTURE.md §4）。

namespace shashoku::test {

struct GoldenOptions {
  std::filesystem::path golden_dir;  // 期待画像の置き場
  std::filesystem::path output_dir;  // 失敗時に actual / expected / diff を書き出す先
  bool update = false;               // true なら比較せず期待画像を書き出す
};

// コンパイル定義（SHASHOKU_GOLDEN_DIR / SHASHOKU_TEST_OUTPUT_DIR）と
// 環境変数 SHASHOKU_UPDATE_GOLDEN から作る既定値。
GoldenOptions default_golden_options();

::testing::AssertionResult expect_golden(const Bitmap& actual, std::string_view name);
::testing::AssertionResult expect_golden(const Bitmap& actual, std::string_view name,
                                         const GoldenOptions& options);

// 差分画像: 違うピクセルは赤、同じピクセルは白に寄せた薄い色（アルファは 255 に潰す）。
// 寸法が違うときは大きい方に合わせ、片方にしか無いピクセルも「違う」として扱う。
Bitmap make_diff_image(const Bitmap& actual, const Bitmap& expected);

Result<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path);
Result<void> write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
Result<Bitmap> read_png_file(const std::filesystem::path& path);
Result<void> write_png_file(const std::filesystem::path& path, const Bitmap& bitmap);

}  // namespace shashoku::test
