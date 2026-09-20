// 決定性（issue #10-4a / DESIGN.md §3-5「同じ入力 → バイト単位で同じ PNG」）の
// end-to-end の検査。
//
// ここで検査するのは **同一プロセス内の純粋性**:
//   * render() を何度呼んでも同じバイト列と同じ警告が返る（グローバル状態も、
//     呼び出しをまたぐキャッシュも、時刻も乱数も使っていない）
//   * 出力に影響しないはずの入力（RenderLimits を緩める、既定値を明示的に書く）を
//     変えてもバイト列が 1 ビットも変わらない
//
// **環境をまたいだ**バイト一致の検査は 2 か所に分かれている:
//   * ピクセル: tests/integration/golden_test.cpp（CI の clang+libc++ / gcc+libstdc++ の
//     4 ジョブが同じ tests/golden/*.png に通っている）
//   * PNG のバイト列のうち shashoku 自身が決める部分: tests/png/determinism_test.cpp
// 保証する範囲・保証しない範囲は README の「決定性」と DESIGN.md §3-5 を見ること。

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

namespace shashoku::test {
namespace {

// 豆腐（絵文字）・フォールバック・ルビ・画像・flex・禁則を 1 枚に入れる。
// 決定性が壊れるとしたら、順序に依存する処理（フォールバック、豆腐の収集、
// 画像テーブルの引き当て）のどれかなので、そこを全部通す。
constexpr std::string_view kBusyHtml = R"(
<style>
  .card { display: flex; gap: 12px; padding: 16px; background-color: #fffcf5; }
  .body { flex: 1; font-size: 18px; line-height: 1.8; }
  .icon { width: 48px; height: 48px; border-radius: 12px; }
  .tag { background-color: #e8590c; color: white; }
</style>
<div class="card">
  <img class="icon" src="icon">
  <div class="body">
    <div><ruby>写植<rt>しゃしょく</rt></ruby>は、日本語の組版だけに特化している。</div>
    <div>Latin と 和文 の混植、絵文字 🎌、そして「約物」。</div>
    <div><span class="tag"> C++23 </span> HTML → PNG</div>
  </div>
</div>
)";

RenderOptions busy_options() {
  RenderOptions options;
  options.viewport_width = 420;
  options.scale = 2.0F;
  return options;
}

struct Rendered {
  std::vector<std::uint8_t> png;
  std::vector<std::string> warnings;
  int width = 0;
  int height = 0;
};

Rendered render_once(const RenderOptions& options) {
  const auto result = render(kBusyHtml, japanese_fonts(), icon_images(), options);
  if (!result) {
    ADD_FAILURE() << "render: " << to_string(result.error());
    return {};
  }
  Rendered out;
  out.png = result->png;
  out.width = result->width;
  out.height = result->height;
  for (const Warning& warning : result->warnings) {
    out.warnings.push_back(warning.detail);
  }
  return out;
}

void expect_same(const Rendered& a, const Rendered& b, std::string_view what) {
  EXPECT_FALSE(a.png.empty()) << what;
  EXPECT_EQ(a.width, b.width) << what;
  EXPECT_EQ(a.height, b.height) << what;
  EXPECT_EQ(a.warnings, b.warnings) << what;
  EXPECT_EQ(a.png.size(), b.png.size()) << what;
  EXPECT_TRUE(a.png == b.png) << what << ": PNG のバイト列が違う";
}

// pipeline_test.cpp の Determinism.SameInputGivesSameBytes の強い版:
// 順序に依存しうる処理を全部通す 1 枚で、警告の並びまで含めて一致を見る。
TEST(Determinism, ABusyDocumentIsRepeatable) {
  const Rendered first = render_once(busy_options());
  EXPECT_FALSE(first.warnings.empty()) << "絵文字の豆腐の警告が出ていない（入力が変わった？）";
  for (int i = 0; i < 3; ++i) {
    expect_same(first, render_once(busy_options()), "同じ入力での再実行");
  }
}

// 上限（A25）は「入力の一部」だが、超過しない限り出力には影響しない。
TEST(Determinism, LooseningTheLimitsDoesNotChangeTheOutput) {
  const Rendered tight = render_once(busy_options());

  RenderOptions loose = busy_options();
  loose.limits.html_bytes = std::size_t{64} * 1024 * 1024;
  loose.limits.images = 4096;
  loose.limits.nesting_depth = 4096;
  loose.limits.dom_nodes = 1'000'000;
  loose.limits.text_code_points = 1'000'000;
  loose.limits.style_rules = 100'000;
  loose.limits.font_size_device_px = 65536.0F;
  loose.limits.scale = 4096.0F;
  loose.limits.image_pixels = std::uint64_t{1} << 26U;
  loose.limits.total_image_pixels = std::uint64_t{1} << 27U;
  loose.limits.device_pixels = std::uint64_t{1} << 28U;
  expect_same(tight, render_once(loose), "上限を緩めた");
}

// 既定値を明示的に書いても出力は変わらない（RenderOptions の既定と
// LineBreakConfig の既定が、コードの既定値と食い違っていないことの確認）。
TEST(Determinism, WritingTheDefaultsExplicitlyChangesNothing) {
  const Rendered implicit = render_once(busy_options());

  RenderOptions explicit_options = busy_options();
  explicit_options.line_break.overflow = OverflowPolicy::Oidashi;
  explicit_options.line_break.strictness = LineBreakStrictness::Strict;
  explicit_options.line_break.collapse_punctuation_spacing = true;
  explicit_options.line_break.trim_line_end = true;
  explicit_options.line_break.trim_line_start = false;
  explicit_options.line_break.extra_line_start_prohibited.clear();
  explicit_options.line_break.extra_line_end_prohibited.clear();
  EXPECT_EQ(explicit_options, busy_options()) << "RenderOptions の既定値が変わっている";
  expect_same(implicit, render_once(explicit_options), "既定値を明示的に書いた");
}

// ダンプも決定的でなければならない（--dump-stage はデバッグの基盤。DESIGN.md §3-3）。
TEST(Determinism, DumpsAreRepeatable) {
  for (const DumpStage stage :
       {DumpStage::Dom, DumpStage::Style, DumpStage::Box, DumpStage::DisplayList, DumpStage::Svg}) {
    const auto first = dump(kBusyHtml, japanese_fonts(), icon_images(), busy_options(), stage);
    ASSERT_TRUE(first.has_value()) << to_string(stage) << ": " << to_string(first.error());
    const auto second = dump(kBusyHtml, japanese_fonts(), icon_images(), busy_options(), stage);
    ASSERT_TRUE(second.has_value()) << to_string(stage);
    EXPECT_EQ(*first, *second) << to_string(stage);
  }
}

}  // namespace
}  // namespace shashoku::test
