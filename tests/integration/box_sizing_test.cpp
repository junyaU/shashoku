// box-sizing（ARCHITECTURE.md A56。CSS Box Sizing 3 §3）を公開 API から見たときの姿。
//
// A56 の動機は「普段の AI の HTML は 10/10 が `* { box-sizing: border-box }` を書く」こと
// （docs/benchmark/results_a53_2026-09-24.md）。だから検査したいのは 2 つ:
//   1. その 1 行がそのまま通る（診断 0）
//   2. `border-box` で書いた紙面と、引き算を手でやった `content-box` の紙面が
//      **バイト単位で同じ PNG** になる（= ガイドの手計算が本当に要らなくなった）

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

namespace shashoku::test {
namespace {

RenderOptions card_options() {
  RenderOptions options;
  options.viewport_width = 600;
  options.viewport_height = 360;
  return options;
}

std::vector<std::uint8_t> render_png(std::string_view html) {
  const auto result = render(html, japanese_fonts(), ImageSet{}, card_options());
  if (!result) {
    ADD_FAILURE() << "render: " << to_string(result.error());
    return {};
  }
  EXPECT_TRUE(result->warnings.empty());
  return result->png;
}

// `* { box-sizing: border-box }` を先頭に書いた紙面。外寸をそのまま書ける
// （600 = 紙面の幅、360 = 紙面の高さ）。
constexpr std::string_view kBorderBoxHtml = R"(<style>
  * { box-sizing: border-box }
  .sheet { display: flex; flex-direction: column; width: 600px; height: 360px;
           padding: 24px; border: 2px solid #23201a; background: #fbf8f2; color: #23201a; }
  .body  { flex: 1 1 0; font-size: 26px; line-height: 1.9; }
  .by    { flex: none; text-align: right; font-size: 15px; color: #7a7266; }
</style>
<div class="sheet">
  <div class="body">おそれるな。おそれは、まだ起きていないことの影にすぎない。</div>
  <div class="by">架空　花『影の書』</div>
</div>
)";

// 同じ絵を content-box（既定）で書いたもの。ガイド §4.14 の手計算そのまま:
// 600 − 24x2 − 2x2 = 548、360 − 24x2 − 2x2 = 308。
constexpr std::string_view kContentBoxHtml = R"(<style>
  .sheet { display: flex; flex-direction: column; width: 548px; height: 308px;
           padding: 24px; border: 2px solid #23201a; background: #fbf8f2; color: #23201a; }
  .body  { flex: 1 1 0; font-size: 26px; line-height: 1.9; }
  .by    { flex: none; text-align: right; font-size: 15px; color: #7a7266; }
</style>
<div class="sheet">
  <div class="body">おそれるな。おそれは、まだ起きていないことの影にすぎない。</div>
  <div class="by">架空　花『影の書』</div>
</div>
)";

TEST(BoxSizing, UniversalBorderBoxMatchesTheHandSubtractedContentBox) {
  const std::vector<std::uint8_t> border_box = render_png(kBorderBoxHtml);
  const std::vector<std::uint8_t> content_box = render_png(kContentBoxHtml);
  ASSERT_FALSE(border_box.empty());
  EXPECT_EQ(border_box, content_box);
}

// 寸法は `--dump-stage box` の数字でも確かめられる（A56 の受け入れ条件）。
// `width: 200px; padding: 20px; border: 2px` の箱は border box 200 / content 156。
TEST(BoxSizing, DumpShowsTheBorderBoxAndContentSizes) {
  constexpr std::string_view kHtml = R"(<style>* { box-sizing: border-box }</style>
<div style="width: 200px; padding: 20px; border: 2px solid #000">あ</div>
)";
  RenderOptions options;
  options.viewport_width = 400;
  const auto box = dump(kHtml, japanese_fonts(), ImageSet{}, options, DumpStage::Box);
  ASSERT_TRUE(box.has_value()) << to_string(box.error());
  // 箱の border-box は 200、中の行は content 幅 200 − 20x2 − 2x2 = 156 で、
  // 開始位置は border 2 + padding 20 = 22（ダンプは 1 要素 1 行の整形済み JSON）
  EXPECT_NE(box->find("\"tag\": \"div\""), std::string::npos) << *box;
  EXPECT_NE(box->find("      200,\n"), std::string::npos) << *box;
  EXPECT_NE(box->find("              22,\n              22,\n              156,\n"),
            std::string::npos)
      << *box;
}

}  // namespace
}  // namespace shashoku::test
