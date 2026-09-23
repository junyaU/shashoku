#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"
#include "linebreak/line_breaker.hpp"

// 紙面（出力の矩形）からのはみ出しの記録（ARCHITECTURE.md A46 / §3.8）。
//
// 固定した幅・高さの外に出た部分は PNG で切れる。切れたことを黙って通さない（DESIGN.md §3-6）
// ための記録で、**絵には影響しない**（paint は読まない）。api がこれを Warning にする。
//
// 判定の約束:
//   * 対象はブロックの border box・行ボックス・置換要素（<img>）。文字の断片は見ない
//   * 最も外側の該当要素ごとに 1 件（祖先が出ていれば子孫は数えない）
//   * 行ボックスと断片は、それを含むブロック要素の位置で報告する
//   * 0.5 px 以下は丸めとして無視する
//   * 高さは viewport_height があるときだけ見る（省略 = 内容追従なら縦には出られない）
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;
using style::Display;

// 固定した紙面（既定は 200 x 100）。
Options fixed(float width = 200, float height = 100) {
  Options out = make_options(width);
  out.viewport_height = height;
  return out;
}

// 固有寸法 80 x 40 の画像 1 枚。
ImageLookup photos() { return image_table({{.src = "photo", .id = 7, .width = 80, .height = 40}}); }

// ---- 横書き ----------------------------------------------------------------------

// 固定した高さより背の高い本文。報告するのは「実際に突き出した要素」で、合成ルートではない
// （ルートの border box は常に中身に追随するので、候補にすると位置が入力の先頭に化ける）。
TEST(LayoutOverflow, ContentTallerThanTheFixedHeightIsReported) {
  FakeMeasurer measurer;
  const auto root = build({at(
      block({text("あいうえお")}, [](ComputedStyle& style) { style.height = Dimension::px(300); }),
      10)});
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_EQ(tree->overflows[0].location.column, 11U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 200);  // 300 - 100
  EXPECT_EQ(tree->overflows[0].edge, OverflowEdge::Bottom);
}

// ぴったり収まっているものは出さない（0 は「超えて」いない）。
TEST(LayoutOverflow, ExactFitIsNotReported) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいうえお")}, [](ComputedStyle& style) {
    style.width = Dimension::px(200);
    style.height = Dimension::px(100);
  })});
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_TRUE(tree->overflows.empty());
}

// 0.5 px 以下は丸めとして無視する。超えたら出す。
TEST(LayoutOverflow, HalfAPixelIsRoundingNoise) {
  FakeMeasurer measurer;
  const auto ignored =
      run_layout(build({block({text("あ")},
                              [](ComputedStyle& style) { style.height = Dimension::px(100.5F); })}),
                 fixed(), measurer);
  ASSERT_TRUE(ignored.has_value());
  EXPECT_TRUE(ignored->overflows.empty());

  const auto reported = run_layout(
      build({block({text("あ")},
                   [](ComputedStyle& style) { style.height = Dimension::px(100.75F); })}),
      fixed(), measurer);
  ASSERT_TRUE(reported.has_value());
  ASSERT_EQ(reported->overflows.size(), 1U);
  EXPECT_FLOAT_EQ(reported->overflows[0].overflow_px, 0.75F);
}

// 幅からの横はみ出し（固定幅の子が紙面より広い）。
TEST(LayoutOverflow, WiderThanTheViewportIsReported) {
  FakeMeasurer measurer;
  const auto root = build({at(
      block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(300); }), 10)});
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 100);  // 300 - 200
  EXPECT_EQ(tree->overflows[0].edge, OverflowEdge::Right);
}

// 入れ子。親も子もはみ出していたら、親の 1 件だけ（子孫は数えない）。
TEST(LayoutOverflow, OnlyTheOutermostBoxIsReported) {
  FakeMeasurer measurer;
  const auto root = build({at(
      block({at(block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(400); }),
                20)},
            [](ComputedStyle& style) { style.width = Dimension::px(300); }),
      10)});
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);  // 外側の 300px の箱
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 100);
}

// 負のマージンで紙面の上に出た場合も対象（超過量は 4 辺のうち最大）。
TEST(LayoutOverflow, NegativeMarginAboveTheCanvasIsReported) {
  FakeMeasurer measurer;
  const auto root = build(
      {at(block({text("あ")}, [](ComputedStyle& style) { style.margin.top = Dimension::px(-20); }),
          10)});
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 20);
  EXPECT_EQ(tree->overflows[0].edge, OverflowEdge::Top);
}

// 置換要素（<img>）が紙面の下に出る。
TEST(LayoutOverflow, ImageBelowTheCanvasIsReported) {
  FakeMeasurer measurer;
  const auto root = build({at(img("photo", std::nullopt, std::nullopt,
                                  [](ComputedStyle& style) {
                                    style.display = Display::Block;
                                    style.width = Dimension::px(50);
                                    style.height = Dimension::px(300);
                                  }),
                              30)});
  const auto tree = run_layout(root, fixed(), measurer, photos());
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 30U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 200);  // 300 - 100
}

// 行の中の <img> は行ボックスの外に出られる（行の inline 範囲はブロックの content 幅で
// 固定。box_tree.hpp）。箱は収まっているのに画像だけが紙面の外、という形を拾う。
// 断片は自分の入力位置を持たないので、含むブロック要素の位置で報告する。
TEST(LayoutOverflow, ImageWiderThanItsLineIsReported) {
  FakeMeasurer measurer;
  const auto root =
      build({at(block({img("photo", std::nullopt, std::nullopt,
                           [](ComputedStyle& style) { style.width = Dimension::px(400); })},
                      [](ComputedStyle& style) { style.width = Dimension::px(100); }),
                5)});
  const auto tree = run_layout(root, fixed(200, 1000), measurer, photos());
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 5U);     // 含むブロックの位置
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 200);  // 400 - 200
}

// 固定した箱から行がはみ出しても、紙面の中なら対象外（箱からのはみ出しは別の話）。
// 同じ入力でも紙面が縮めば報告する、という対になっている。
TEST(LayoutOverflow, LinesOutsideTheirBoxButInsideTheCanvas) {
  FakeMeasurer measurer;
  const auto root = build({at(block({text("あいうえおかきくけこさしすせそ")},
                                    [](ComputedStyle& style) {
                                      style.width = Dimension::px(80);
                                      style.height = Dimension::px(16);
                                    }),
                              10)});
  // 3 行 = 48px ぶんの文字が高さ 16px の箱から出るが、紙面（100px）の中
  const auto inside = run_layout(root, fixed(200, 100), measurer);
  ASSERT_TRUE(inside.has_value());
  ASSERT_EQ(all_lines(*inside).size(), 3U);
  EXPECT_TRUE(inside->overflows.empty());

  // 紙面を 32px にすると 3 行目（32〜48px）が外に出る。位置は行を含むブロック
  const auto outside = run_layout(root, fixed(200, 32), measurer);
  ASSERT_TRUE(outside.has_value());
  ASSERT_EQ(outside->overflows.size(), 1U);
  EXPECT_EQ(outside->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(outside->overflows[0].overflow_px, 16);  // 48 - 32
}

// 内容追従（viewport_height を省略）では縦にはみ出せない。横は今までどおり見る。
TEST(LayoutOverflow, ContentFollowingHeightNeverOverflowsVertically) {
  FakeMeasurer measurer;
  const auto tall = run_layout(
      build({block({text("あ")}, [](ComputedStyle& style) { style.height = Dimension::px(300); })}),
      make_options(200), measurer);
  ASSERT_TRUE(tall.has_value());
  EXPECT_FALSE(tall->viewport_height.has_value());
  EXPECT_TRUE(tall->overflows.empty());

  const auto wide = run_layout(
      build({block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(300); })}),
      make_options(200), measurer);
  ASSERT_TRUE(wide.has_value());
  ASSERT_EQ(wide->overflows.size(), 1U);
  EXPECT_FLOAT_EQ(wide->overflows[0].overflow_px, 100);
}

// ---- 並びとまとめ方 ---------------------------------------------------------------

// 並びは入力位置の昇順（文書順ではない）。
TEST(LayoutOverflow, SortedByInputPosition) {
  FakeMeasurer measurer;
  const auto root = build({
      at(block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(400); }), 20),
      at(block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(300); }), 10),
  });
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 2U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 100);
  EXPECT_EQ(tree->overflows[1].location.offset, 20U);
  EXPECT_FLOAT_EQ(tree->overflows[1].overflow_px, 200);
}

// 同じ位置の複数件は 1 件にまとめ、超過量は最大を採る。
TEST(LayoutOverflow, SameLocationIsMergedKeepingTheLargest) {
  FakeMeasurer measurer;
  const auto root = build({
      at(block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(300); }), 10),
      at(block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(400); }), 10),
  });
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 200);
}

// ---- 縦書き（A1: 論理座標のまま判定すると上下左右を取り違える） -------------------------
//
//   vertical-rl: x = viewport_width - block_end、y = inline_start
//   → 字送り（inline）は紙面の高さ、行送り（block）は紙面の幅と突き合わせる

// 字送り方向（= 紙面の高さ）から出る。
TEST(LayoutOverflow, VerticalContentPastTheBottomIsReported) {
  FakeMeasurer measurer;
  const auto root = build_vertical({at(
      block({text("あ")}, [](ComputedStyle& style) { style.height = Dimension::px(300); }), 10)});
  const auto tree = run_layout(root, vertical_options(200, 100), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 200);  // 300 - 100
  // 縦書きの inline は物理の下向き
  EXPECT_EQ(tree->overflows[0].edge, OverflowEdge::Bottom);
}

// 行送り方向（= 紙面の幅）から出る。縦書きの block は紙面の右端からの距離なので、
// はみ出す先は**左**（物理 x < 0）。
TEST(LayoutOverflow, VerticalColumnsPastTheLeftEdgeAreReported) {
  FakeMeasurer measurer;
  const auto root = build_vertical({at(
      block({text("あ")}, [](ComputedStyle& style) { style.width = Dimension::px(300); }), 10)});
  const auto tree = run_layout(root, vertical_options(200, 100), measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(tree->overflows.size(), 1U);
  EXPECT_EQ(tree->overflows[0].location.offset, 10U);
  EXPECT_FLOAT_EQ(tree->overflows[0].overflow_px, 100);  // 300 - 200
  // 縦書きの block は紙面の右端からの距離なので、出る先は物理の左
  EXPECT_EQ(tree->overflows[0].edge, OverflowEdge::Left);
}

// 縦書きでも、収まっているものは出さない（論理座標のまま見ていると block 方向の
// 0〜viewport_width が「上下」に化けて、ここが誤検出になる）。
TEST(LayoutOverflow, VerticalFittingContentIsNotReported) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あいうえお")})});
  const auto tree = run_layout(root, vertical_options(200, 100), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_TRUE(tree->overflows.empty());
}

// ---- 見ないもの（A46 の「箱ではないもの」） ---------------------------------------------

// ぶら下げ（burasage）で行ボックスの外に出た約物。グリフは箱ではないので数えない。
TEST(LayoutOverflow, HangingPunctuationIsNotAnOverflow) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あいうえお。")})});
  Options options = fixed(84, 16);
  options.line_break.overflow = linebreak::OverflowPolicy::Burasage;
  const auto tree = run_layout(root, options, measurer);
  ASSERT_TRUE(tree.has_value());

  // 前提: 「。」は紙面（84px）の外（80 + 16 = 96）にぶら下がっている
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<float> pens = glyph_positions(*lines.front());
  ASSERT_EQ(pens.size(), 6U);
  EXPECT_FLOAT_EQ(pens.back(), 80);
  EXPECT_TRUE(tree->overflows.empty());
}

// ルビの注記。行ボックスがルビのぶん広がるので、注記だけでは判定しない。
// 紙面の高さを「行ボックスの下端ぴったり」にして、行の外に出ているものがあれば
// 必ず 1 件になる状態で見る。
TEST(LayoutOverflow, RubyAnnotationIsInsideItsLine) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢字"), rt("かんじ")})})});
  const auto probe = run_layout(root, fixed(200, 1000), measurer);
  ASSERT_TRUE(probe.has_value());
  const std::vector<const LineBox*> lines = all_lines(*probe);
  ASSERT_EQ(lines.size(), 1U);
  // 前提: ルビの断片（行とは別のベースライン）がある
  std::size_t ruby_fragments = 0;
  for (const TextFragment* fragment : text_fragments(*lines.front())) {
    if (fragment->baseline != lines.front()->baseline) {
      ++ruby_fragments;
    }
  }
  EXPECT_GT(ruby_fragments, 0U);

  const auto tree = run_layout(root, fixed(200, lines.front()->rect.block_end()), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_TRUE(tree->overflows.empty());
}

// 固定幅の箱から文字がはみ出しても、紙面の中なら対象外。
TEST(LayoutOverflow, TextOutsideItsBoxButInsideTheCanvas) {
  FakeMeasurer measurer;
  // 分割できない欧文 10 文字（80px）を幅 50px の箱に入れる（A4: クリップしない）
  const auto root = build(
      {block({text("abcdefghij")}, [](ComputedStyle& style) { style.width = Dimension::px(50); })});
  const auto tree = run_layout(root, fixed(200, 100), measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const TextFragment*> fragments = text_fragments(*all_lines(*tree).front());
  ASSERT_EQ(fragments.size(), 1U);
  // 前提: 文字は箱（50px）の外に出ている
  EXPECT_FLOAT_EQ(fragments.front()->inline_size, 80);
  EXPECT_TRUE(tree->overflows.empty());
}

// ---- ダンプ -----------------------------------------------------------------------

TEST(LayoutOverflow, DumpJsonListsOverflows) {
  FakeMeasurer measurer;
  const auto root = build({at(
      block({text("あ")}, [](ComputedStyle& style) { style.height = Dimension::px(300); }), 10)});
  const auto tree = run_layout(root, fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_NE(dump_json(*tree).find(R"("overflows": [
    {
      "location": "1:11",
      "overflow_px": 200,
      "edge": "bottom"
    }
  ])"),
            std::string::npos);
}

// 1 件も無ければキーごと省く（豆腐と同じ流儀）。
TEST(LayoutOverflow, DumpJsonOmitsTheKeyWhenThereIsNoOverflow) {
  FakeMeasurer measurer;
  const auto tree = run_layout(build({block({text("あ")})}), fixed(), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(dump_json(*tree).find("overflows"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::layout::test
