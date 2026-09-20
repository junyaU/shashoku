#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
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

  // 行分割器の中も N に線形（A24）。layout が渡した計測カウンタで見る
  EXPECT_EQ(counters.line_breaker.lines, kChars);
  EXPECT_LE(counters.line_breaker.line_scan, 4 * kChars);
  EXPECT_LE(counters.line_breaker.width_items, 8 * kChars);
  EXPECT_LE(counters.line_breaker.mandatory_scan, 4 * kChars);
  EXPECT_LE(counters.line_breaker.rule_scan, 4 * kChars);
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

// #8 / #10: 色だけが違う span が S 個ある段落。
//   * シェーピングは段落で 1 回（色の境界では切らない。かつては S 回呼んでいた）
//   * 文字ごとの属性の表は二分探索で引く（登録のたびに線形探索すると S² に膨らむ）
TEST(LayoutComplexity, ManyColorOnlySpansShapeOnceAndInternInLogTime) {
  constexpr std::size_t kSpans = 2000;
  constexpr std::size_t kCharsPerSpan = 2;
  FakeMeasurer measurer;
  std::vector<Tree> children;
  children.reserve(kSpans);
  for (std::size_t i = 0; i < kSpans; ++i) {
    // span ごとに違う色（= 装飾属性は S 種類）。シェーピング属性はすべて同じ
    const auto low = static_cast<std::uint8_t>(i & 0xFFU);
    const auto high = static_cast<std::uint8_t>((i >> 8U) & 0xFFU);
    children.push_back(inline_box({text("あい")}, [low, high](ComputedStyle& style) {
      style.color = Color{low, high, 0, 255};
    }));
  }
  const auto root = build({block(std::move(children))});
  Counters counters;
  const auto tree = run_layout(root, make_options(kFontSize), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  constexpr std::size_t kTotalChars = kSpans * kCharsPerSpan;
  ASSERT_EQ(all_lines(*tree).size(), kTotalChars);  // 1 行 1 文字
  EXPECT_EQ(counters.shape_calls, 1U) << "色の境界でシェーピングが切れている（#8）";
  EXPECT_EQ(counters.shaped_chars, kTotalChars);
  // 断片（= 描き分け）は色の境界で分かれたまま
  EXPECT_EQ(text_fragments(*all_lines(*tree).front()).size(), 1U);

  // 二分探索なら S×log S 程度。線形探索だと S²/2 = 2,000,000 を超える
  EXPECT_LE(counters.style_probes, 64 * kSpans);
}

}  // namespace
}  // namespace shashoku::layout::test
