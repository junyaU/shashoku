#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

// ---- #5: flex の入れ子 ---------------------------------------------------------
//
// flex は「測ってから置く」ので、同じ部分木・同じ段落に何度も触る。素直に書くと
//   * column の flex アイテムは、高さを知るために部分木を一度まるごと組んで捨て、配置の段で
//     もう一度組む → 各階層が子を 2 回組むので深さ d の鎖で 2^d（issue #5）
//   * row でも、固有寸法の計測と実配置が同じ段落を別々に準備する → シェーピングが d に比例
// になる。どちらも A29 のメモで消す。測るのは時間ではなく回数。

using style::FlexDirection;

// 深さ d の flex の鎖。いちばん内側に段落が 1 つだけある。
constexpr std::string_view kSample = "日本語の文章を正しく組む。";
constexpr std::uint64_t kSampleChars = 13;

FlexDirection flipped(FlexDirection direction) {
  return direction == FlexDirection::Row ? FlexDirection::Column : FlexDirection::Row;
}

Tree nest_flex(std::size_t depth, FlexDirection outermost, bool alternate) {
  Tree inner = block({text(std::string(kSample))});
  for (std::size_t i = 0; i < depth; ++i) {
    // 外側から数えて i 段目の向き（alternate なら row と column が交互）
    const std::size_t from_outside = depth - 1 - i;
    const bool flip = alternate && (from_outside % 2 == 1);
    const FlexDirection direction = flip ? flipped(outermost) : outermost;
    std::vector<Tree> children;
    children.push_back(std::move(inner));
    inner = flex(std::move(children),
                 [direction](ComputedStyle& style) { style.flex_direction = direction; });
  }
  return inner;
}

// 1 つの段落は、固有寸法の計測にも配置にも使われるが、準備（収集 → 空白の畳み込み →
// シェーピング → アイテム化）は 1 回だけ（A6 / A29）。入れ子の深さで増えてはいけない。
void expect_paragraph_prepared_once(FlexDirection outermost, bool alternate) {
  for (const std::size_t depth : {std::size_t{4}, std::size_t{8}, std::size_t{12}}) {
    FakeMeasurer measurer;
    const auto root = build({nest_flex(depth, outermost, alternate)});
    Counters counters;
    const auto tree = run_layout(root, make_options(600), measurer, counters);
    ASSERT_TRUE(tree.has_value()) << "depth=" << depth;

    EXPECT_EQ(counters.shape_calls, 1U) << "depth=" << depth;
    EXPECT_EQ(counters.shaped_chars, kSampleChars) << "depth=" << depth;
    EXPECT_EQ(counters.inline_prepare, 1U) << "depth=" << depth;
  }
}

TEST(LayoutComplexity, RowFlexNestingPreparesTheParagraphOnce) {
  expect_paragraph_prepared_once(FlexDirection::Row, false);
}

TEST(LayoutComplexity, ColumnFlexNestingPreparesTheParagraphOnce) {
  expect_paragraph_prepared_once(FlexDirection::Column, false);
}

TEST(LayoutComplexity, MixedFlexNestingPreparesTheParagraphOnce) {
  expect_paragraph_prepared_once(FlexDirection::Column, true);
}

// d 段の入れ子を組んだときの作業量が d² の定数倍に収まる。
// 閾値は 2d² + 32 なので 2^d は必ず落ちる（d = 8 で 2^9 = 512 > 160。直す前の実測は
// column の depth 16 で layout_block = 131,072 = 2^17、shape_calls = 65,536）。
void expect_polynomial_nesting(FlexDirection outermost, bool alternate) {
  for (const std::size_t depth :
       {std::size_t{4}, std::size_t{8}, std::size_t{12}, std::size_t{16}}) {
    FakeMeasurer measurer;
    const auto root = build({nest_flex(depth, outermost, alternate)});
    Counters counters;
    const auto tree = run_layout(root, make_options(600), measurer, counters);
    ASSERT_TRUE(tree.has_value()) << "depth=" << depth;

    const auto d = static_cast<std::uint64_t>(depth);
    const std::uint64_t budget = (2 * d * d) + 32;
    EXPECT_LE(counters.layout_block, budget) << "depth=" << depth;
    EXPECT_LE(counters.content_intrinsic, budget) << "depth=" << depth;
    EXPECT_LE(counters.line_boxes, budget) << "depth=" << depth;
  }
}

TEST(LayoutComplexity, ColumnFlexNestingStaysPolynomial) {
  expect_polynomial_nesting(FlexDirection::Column, false);
}

TEST(LayoutComplexity, RowFlexNestingStaysPolynomial) {
  expect_polynomial_nesting(FlexDirection::Row, false);
}

TEST(LayoutComplexity, MixedFlexNestingStaysPolynomial) {
  expect_polynomial_nesting(FlexDirection::Column, true);
}

// 幅の広い木: 1 段に複数のアイテムがある入れ子。シェーピングはテキストノードの数だけ。
TEST(LayoutComplexity, WideFlexTreeShapesOncePerTextNode) {
  constexpr std::size_t kDepth = 6;
  constexpr std::size_t kSiblings = 3;  // 1 段あたり「段落 2 つ + 次の段」

  Tree inner = block({text(std::string(kSample))});
  std::uint64_t text_nodes = 1;
  for (std::size_t i = 0; i < kDepth; ++i) {
    std::vector<Tree> children;
    children.push_back(block({text(std::string(kSample))}));
    children.push_back(std::move(inner));
    children.push_back(block({text(std::string(kSample))}));
    text_nodes += kSiblings - 1;
    inner = flex(std::move(children),
                 [](ComputedStyle& style) { style.flex_direction = FlexDirection::Column; });
  }

  FakeMeasurer measurer;
  const auto root = build({std::move(inner)});
  Counters counters;
  const auto tree = run_layout(root, make_options(600), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  EXPECT_EQ(counters.shape_calls, text_nodes);
  EXPECT_EQ(counters.shaped_chars, text_nodes * kSampleChars);
  EXPECT_EQ(counters.inline_prepare, text_nodes);
  // ノード数 n = depth×siblings に対して n² の定数倍まで
  const std::uint64_t nodes = kDepth * kSiblings;
  EXPECT_LE(counters.layout_block, (2 * nodes * nodes) + 32);
}

}  // namespace
}  // namespace shashoku::layout::test
