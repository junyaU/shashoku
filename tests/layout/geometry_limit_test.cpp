#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/counters.hpp"
#include "layout/layout.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"
#include "test_support.hpp"

// layout の出口の不変条件（ARCHITECTURE.md A36 の第 2 段階 / issue #19）。
//
//   BoxTree に入っている座標・寸法は、すべて有限で、絶対値が Options::max_geometry_px 以内。
//
// `%` の解決と flex の比は包含ブロックが要るので style では判定できない（A5）。ここで止める。
// 上限は「1 要素あたりの長さ x 要素数」で導いた値なので、常識的な入力では発動しない。
namespace shashoku::layout::test {
namespace {

using style::Dimension;

// 失敗を期待し、エラーを返す。
Error layout_failure(const style::StyledNode& root, const Options& options,
                     text::TextMeasurer& measurer) {
  const Result<BoxTree> tree = run_layout(root, options, measurer);
  if (tree) {
    ADD_FAILURE() << "エラーになるはずが成功した";
    return Error{};
  }
  return tree.error();
}

// ---- `%` の解決（第 1 段階では style を通り抜ける）--------------------------------

// `width: 1e38%` はビューポート幅で結果が変わっていた（200 なら 2e38 で有限、1000 なら inf）。
// **どちらも同じ種類のエラー**でなければならない（issue #19 の受け入れ条件）。
TEST(GeometryLimit, HugePercentFailsTheSameWayAtEveryViewport) {
  FakeMeasurer measurer;
  const style::StyledNode root = build(
      {at(block({text("あ")}, [](style::ComputedStyle& s) { s.width = Dimension::percent(1e38F); }),
          7)});
  for (const float width : {200.0F, 1000.0F, 16384.0F}) {
    SCOPED_TRACE(width);
    const Error error = layout_failure(root, make_options(width), measurer);
    EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
    ASSERT_TRUE(error.location.has_value()) << error.message;
    // その要素を指す（at() は桁を offset + 1 にする）
    EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 8U) << error.message;
  }
}

// ---- flex の比（inf/inf = NaN）----------------------------------------------------

// `flex-shrink: 1e38` x `width: 1e7px` で `shrink x base` が inf になり、
// `factor / factors.scaled` が inf/inf = NaN になる。NaN はどの比較も偽なので、
// 「上限以内か」の判定 1 つで一緒に捕まる。
TEST(GeometryLimit, FlexRatioNanIsRejected) {
  FakeMeasurer measurer;
  const style::StyledNode root =
      build({flex({at(block({text("あ")},
                            [](style::ComputedStyle& s) {
                              s.flex_shrink = 1e38F;
                              s.width = Dimension::px(1e7F);
                            }),
                      20)},
                  [](style::ComputedStyle& s) { s.width = Dimension::px(10); })});
  const Error error = layout_failure(root, make_options(1000), measurer);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
  ASSERT_TRUE(error.location.has_value()) << error.message;
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 21U) << error.message;
}

// ---- 上限そのもの ------------------------------------------------------------------

TEST(GeometryLimit, Boundary) {
  FakeMeasurer measurer;
  const style::StyledNode root =
      build({block({}, [](style::ComputedStyle& s) { s.height = Dimension::px(100); })});

  // ビューポート幅も座標として見るので、上限より狭くしておく
  Options options = make_options(50);
  options.max_geometry_px = 100;
  const Result<BoxTree> ok = run_layout(root, options, measurer);
  EXPECT_TRUE(ok.has_value()) << (ok ? std::string{} : ok.error().message);

  options.max_geometry_px = 99;
  const Error error = layout_failure(root, options, measurer);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::length_px"), std::string::npos) << error.message;
}

// 座標も寸法と同じ上限で見る（積み上がった位置が壊れるのも同じ事故）。
TEST(GeometryLimit, CoordinatesAreCheckedToo) {
  FakeMeasurer measurer;
  const style::StyledNode root = build({
      block({}, [](style::ComputedStyle& s) { s.height = Dimension::px(80); }),
      at(block({}, [](style::ComputedStyle& s) { s.height = Dimension::px(10); }), 30),
  });
  Options options = make_options(50);
  options.max_geometry_px = 85;  // 2 つめの block_start = 80 は通るが、block_end = 90 が超える
  const Error error = layout_failure(root, options, measurer);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
}

// ---- 常識的な入力が新しく落ちないこと ------------------------------------------------

// 既定の上限は「1 要素あたり length_px x dom_nodes 個」で導いてあるので、
// 縦に長い文書（要素をたくさん積む）は通らなければならない。
TEST(GeometryLimit, TallDocumentsStillLayOut) {
  FakeMeasurer measurer;
  std::vector<Tree> children;
  children.reserve(400);
  for (std::size_t i = 0; i < 400; ++i) {
    children.push_back(block({}, [](style::ComputedStyle& s) { s.height = Dimension::px(10000); }));
  }
  const style::StyledNode root = build(std::move(children));
  const Result<BoxTree> tree = run_layout(root, make_options(600), measurer);
  ASSERT_TRUE(tree.has_value()) << (tree ? std::string{} : tree.error().message);
  EXPECT_FLOAT_EQ(tree->content_block_size(), 4e6F);  // 既定の上限 3.36e11 の 1 万分の 1
}

TEST(GeometryLimit, OrdinaryDocumentsAreUnaffected) {
  FakeMeasurer measurer;
  const style::StyledNode root = build({block({text("日本語の組版")}, [](style::ComputedStyle& s) {
    s.padding = Edges<float>{8, 8, 8, 8};
    s.width = Dimension::px(300);
  })});
  const Result<BoxTree> tree = run_layout(root, make_options(600), measurer);
  ASSERT_TRUE(tree.has_value()) << (tree ? std::string{} : tree.error().message);
}

// ---- 計算量（A21: 時間ではなく回数で測る）-------------------------------------------

// 出口の走査は木の大きさに線形（paint の走査 1 回ぶん）。段落の数を 2 倍にしたら
// 見た数も 2 倍になる（二乗なら 4 倍になって落ちる）。
TEST(GeometryLimit, ScanIsLinearInTheTreeSize) {
  const auto work_for = [](std::size_t paragraphs) {
    FakeMeasurer measurer;
    std::vector<Tree> children;
    children.reserve(paragraphs);
    for (std::size_t i = 0; i < paragraphs; ++i) {
      children.push_back(block({text("日本語の組版をする")}));
    }
    const style::StyledNode root = build(std::move(children));
    Counters counters;
    const Result<BoxTree> tree = run_layout(root, make_options(600), measurer, counters);
    EXPECT_TRUE(tree.has_value());
    return counters.geometry_nodes;
  };

  const std::uint64_t small = work_for(100);
  const std::uint64_t large = work_for(200);
  EXPECT_GT(small, 0U);
  // ちょうど 2 倍（ルート 1 個ぶんだけずれる）。余裕を見て 2.1 倍以下
  EXPECT_LE(large, small * 21 / 10) << small << " -> " << large;
  EXPECT_GE(large, small * 19 / 10) << small << " -> " << large;
}

// ---- BlockBox::location（A36）--------------------------------------------------------

TEST(GeometryLimit, BlockBoxCarriesTheSourceLocation) {
  FakeMeasurer measurer;
  const style::StyledNode root = build({at(block({text("あ")}), 12)});
  const Result<BoxTree> tree = run_layout(root, make_options(600), measurer);
  ASSERT_TRUE(tree.has_value()) << (tree ? std::string{} : tree.error().message);
  const BlockBox* div = find_block(*tree, "div");
  ASSERT_NE(div, nullptr);
  EXPECT_EQ(div->location.column, 13U);
  EXPECT_EQ(div->location.offset, 12U);
}

}  // namespace
}  // namespace shashoku::layout::test
