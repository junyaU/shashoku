#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
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

// ---- A54: stretch で伸びた項目の組み直しの計算量 -------------------------------------
//
// row の flex で stretch した項目は「行の交差サイズが決まってから」中身を組まなければ
// ならない（A54）。素直に「まず組んで、伸びていたら組み直す」と 1 段につき 2 回組むので、
// 入れ子の深さ d に対して 2^d になる。ここでは 3 つの形で **回数** を固定する。
// 時間は環境に依るので assert せず、表（ARCHITECTURE.md A54 の追記）に載せる。
//
// 3 つの形（どれも row の flex を d 段重ねる。align-items は既定の stretch）:
//   Worst  各段 = [背の高いブロック（段ごとに 1 行ずつ高くする）, 次の段]。次の段の
//          伸び幅が必ず > 0 になるので、素直な実装では毎段で組み直しが起きる = 2^d
//   Chain  各段 = [次の段] だけ。行の交差サイズをその項目自身が決めるので伸び幅 0
//   NoTall 各段 = [次の段, 1 行のテキスト]。兄弟はいるが背が低いので伸び幅 0

using style::AlignItems;
using style::Dimension;

constexpr float kStretchBar = 24;  // 最内の棒の高さ（1 行 = 16 より高くしておく）

enum class StretchShape : std::uint8_t { Worst, Chain, NoTall };

std::string_view shape_name(StretchShape shape) {
  switch (shape) {
    case StretchShape::Worst:
      return "worst";
    case StretchShape::Chain:
      return "chain";
    case StretchShape::NoTall:
      return "no-tall";
  }
  return "?";
}

// lines 行ぶんの高さ（16 × lines）を持つブロック。
Tree tall_block(std::size_t lines) {
  std::vector<Tree> children;
  for (std::size_t i = 0; i < lines; ++i) {
    if (i > 0) {
      children.push_back(br());
    }
    children.push_back(text("あ"));
  }
  return block(std::move(children));
}

// 最内のしるし: 自分の行の交差サイズの中央に来る棒。stretch が最内まで届いたかを座標で見る。
Tree centered_bar() {
  return flex({block({},
                     [](ComputedStyle& style) {
                       style.width = Dimension::px(24);
                       style.height = Dimension::px(kStretchBar);
                     })},
              [](ComputedStyle& style) { style.align_items = AlignItems::Center; });
}

Tree nest_stretch(std::size_t depth, StretchShape shape) {
  Tree inner = centered_bar();
  for (std::size_t i = 0; i < depth; ++i) {
    std::vector<Tree> children;
    switch (shape) {
      case StretchShape::Worst:
        // i 段目の中身の自然な高さは max(24, 16(i+1))。1 行ぶん高いブロックを兄弟に置くと、
        // どの段でも「伸び幅 > 0」になる = 組み直しが毎段で起きる
        children.push_back(tall_block(i + 2));
        children.push_back(std::move(inner));
        break;
      case StretchShape::Chain:
        children.push_back(std::move(inner));
        break;
      case StretchShape::NoTall:
        children.push_back(std::move(inner));
        children.push_back(text("あ"));
        break;
    }
    inner = flex(std::move(children));  // row・align-items: stretch（どちらも既定）
  }
  return inner;
}

// 深さ d の入れ子を 1 回組んで、そのときのカウンタを返す。
Counters stretch_counters(std::size_t depth, StretchShape shape) {
  FakeMeasurer measurer;
  const auto root = build({nest_stretch(depth, shape)});
  Counters counters;
  const auto tree = run_layout(root, make_options(600), measurer, counters);
  EXPECT_TRUE(tree.has_value()) << "depth=" << depth;
  return counters;
}

// 入力に含まれるテキストノードの数（= シェーピングの回数の期待値。A6）。
std::uint64_t text_nodes_of(std::size_t depth, StretchShape shape) {
  const auto d = static_cast<std::uint64_t>(depth);
  switch (shape) {
    case StretchShape::Worst:
      return d * (d + 3) / 2;  // 段ごとに 2, 3, … 行のブロックが 1 つ
    case StretchShape::Chain:
      return 0;  // テキストなし
    case StretchShape::NoTall:
      return d;  // 段ごとに 1 行
  }
  return 0;
}

// 組み直しの回数は入れ子の深さ **d に線形**（A54 の追記）。
//
// 直す前（A54 のまま）の最悪の形は 2^(d+2) − 1 回で、d = 12 で 16,383 回・67ms だった。
// 対策後は「計測 1 + 配置 1」なので、1 段あたりの増分が定数になる。2 つの見方で固定する:
//   * 1 段増やしたときの増分が定数以下（これが破れると多項式にも指数にもなる）
//   * 絶対値が 6d + 16 以下（実測は最悪の形で 4d + 3、鎖で 2d + 4）
TEST(LayoutComplexity, StretchRelayoutStaysLinearInTheNestingDepth) {
  constexpr std::size_t kMaxDepth = 12;
  constexpr std::uint64_t kPerDepth = 6;
  for (const StretchShape shape :
       {StretchShape::Worst, StretchShape::Chain, StretchShape::NoTall}) {
    std::uint64_t previous = 0;
    for (std::size_t depth = 1; depth <= kMaxDepth; ++depth) {
      const Counters counters = stretch_counters(depth, shape);
      const auto d = static_cast<std::uint64_t>(depth);
      SCOPED_TRACE(testing::Message() << shape_name(shape) << " depth=" << depth);
      EXPECT_LE(counters.layout_block, (kPerDepth * d) + 16);
      if (depth > 1) {
        EXPECT_LE(counters.layout_block - previous, kPerDepth) << "1 段あたりの増分";
      }
      previous = counters.layout_block;
      // シェーピングは入力のテキストノードごとに 1 回（A6 / A29）。組み直しでは増えない
      EXPECT_EQ(counters.shape_calls, text_nodes_of(depth, shape));
      // 段落の準備は入力の段落の数だけ（どの形も 1 段につき多くて 1 つ増える）
      EXPECT_LE(counters.inline_prepare, d + 2);
    }
  }
}

// 表を出す口（ARCHITECTURE.md A54 の追記の数字はこれで採った）。回数は環境に依らないが
// 時間は依るので、ふつうのテスト実行からは外してある。出し方:
//   build/dev/tests/layout/layout_test --gtest_also_run_disabled_tests
//     --gtest_filter='*StretchRelayoutCostTable*'
TEST(LayoutComplexity, DISABLED_StretchRelayoutCostTable) {
  constexpr int kRepeats = 3;
  constexpr std::size_t kMaxDepth = 12;
  std::cout << "shape\td\tlayout_block\tshape_calls\tms(median of " << kRepeats << ")\n";
  for (const StretchShape shape :
       {StretchShape::Worst, StretchShape::Chain, StretchShape::NoTall}) {
    for (std::size_t depth = 1; depth <= kMaxDepth; ++depth) {
      std::vector<double> times;
      Counters counters;
      for (int run = 0; run < kRepeats; ++run) {
        FakeMeasurer measurer;
        const auto root = build({nest_stretch(depth, shape)});
        Counters run_counters;
        const auto start = std::chrono::steady_clock::now();
        const auto tree = run_layout(root, make_options(600), measurer, run_counters);
        const auto end = std::chrono::steady_clock::now();
        ASSERT_TRUE(tree.has_value()) << "depth=" << depth;
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        counters = run_counters;
      }
      std::sort(times.begin(), times.end());
      std::cout << shape_name(shape) << '\t' << depth << '\t' << counters.layout_block << '\t'
                << counters.shape_calls << '\t' << std::fixed << std::setprecision(3)
                << times[times.size() / 2] << '\n';
    }
  }
}

}  // namespace
}  // namespace shashoku::layout::test
