#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "layout/counters.hpp"
#include "layout/test_support.hpp"

// 計算量の回帰テスト（issue #4 / #10-3）。
//
// 時間ではなく **回数** で見る: 計測カウンタ（layout/counters.hpp）が数えた作業量が、
// 入力の大きさ N に対して線形の範囲（定数 × N 以下）に収まることを確かめる。
// 時間で測ると環境依存でぶれるうえ、O(N×L) が「遅いが通る」状態で気づけない。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

// 幅 1em の版面に流すと 1 行 1 文字になる長文。
constexpr std::size_t kChars = 20000;
constexpr float kFontSize = 16;

std::string repeat(const std::string& unit, std::size_t count) {
  std::string out;
  out.reserve(unit.size() * count);
  for (std::size_t i = 0; i < count; ++i) {
    out += unit;
  }
  return out;
}

// #4: 狭い版面の長文。行数 L が文字数 N に比例するので、1 行あたり N の作業をすると二乗になる。
TEST(LayoutComplexity, NarrowLongParagraphKeepsLineWorkLinear) {
  FakeMeasurer measurer;
  const auto root = build({block({text(repeat("あ", kChars))})});
  Counters counters;
  const auto tree = run_layout(root, make_options(kFontSize), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  // 前提: 1 行 1 文字（L = N）
  ASSERT_EQ(all_lines(*tree).size(), kChars);
  EXPECT_EQ(counters.line_boxes, kChars);

  // A6: シェーピングは段落全体で 1 回。行ごとに測り直していない
  EXPECT_EQ(counters.shape_calls, 1U);
  EXPECT_EQ(counters.shaped_chars, kChars);
  EXPECT_EQ(counters.inline_prepare, 1U);
  EXPECT_EQ(counters.layout_block, 2U);  // #root と div

  // 行の構築で確保・初期化する作業バッファは、段落全体で O(N)。
  // 行ごとに段落全体ぶん確保していると N×L になる
  EXPECT_LE(counters.line_scratch, 4 * kChars);
}

// #4 の「ついで」: 背景スコープが多い段落でも、行ごとに全スコープを舐めない。
TEST(LayoutComplexity, ManyBackgroundScopesProbeOnlyIntersectingOnes) {
  constexpr std::size_t kSpans = 1000;
  constexpr std::size_t kCharsPerSpan = 2;
  FakeMeasurer measurer;
  std::vector<Tree> children;
  children.reserve(kSpans);
  for (std::size_t i = 0; i < kSpans; ++i) {
    children.push_back(inline_box({text("あい")}, [](ComputedStyle& style) {
      style.background_color = Color{0, 0, 255, 255};
    }));
  }
  const auto root = build({block(std::move(children))});
  Counters counters;
  const auto tree = run_layout(root, make_options(kFontSize), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  constexpr std::size_t kTotalChars = kSpans * kCharsPerSpan;
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), kTotalChars);  // 1 行 1 文字
  // 出力は変わらない: どの行にも、その行の文字が属する span の背景が 1 枚ずつ出る
  EXPECT_EQ(backgrounds(*lines.front()).size(), 1U);
  EXPECT_EQ(backgrounds(*lines.back()).size(), 1U);
  EXPECT_FLOAT_EQ(backgrounds(*lines.front())[0]->rect.inline_size, kFontSize);

  // 行と交差するスコープ（この入力では 1 行あたり 1 個）だけを見る
  EXPECT_LE(counters.background_probes, 4 * kTotalChars);
  EXPECT_LE(counters.line_scratch, 4 * kTotalChars);
}

}  // namespace
}  // namespace shashoku::layout::test
