#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"
#include "linebreak/line_breaker.hpp"

// 縦書き（DESIGN.md Phase 8 / ARCHITECTURE.md A1）。
// 論理座標のまま組み、物理座標への変換は paint が 1 回だけ行う:
//   vertical-rl: x = viewport_width − block_end、y = inline_start
// つまり block は「紙面の右端からの距離」。行は右から左へ進む。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;
using style::FlexDirection;

// 幅 400 × 高さ 200 の縦書き。行の長さ（inline 方向）は 200 になる。
Result<BoxTree> flow_vertical(const style::StyledNode& root, FakeMeasurer& measurer,
                              float width = 400, float height = 200) {
  return run_layout(root, vertical_options(width, height), measurer);
}

// ---- 行の進み方 -------------------------------------------------------------------

// Phase 3 相当: 高さ 200 の縦書きブロックに 16px の全角 13 文字 → 2 行。
TEST(LayoutVertical, LinesAdvanceAlongTheBlockAxis) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あいうえおかきくけこさしす")})});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(tree->writing_mode, WritingMode::VerticalRl);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あいうえおかきくけこさし", "す"}));

  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  const float line = fake_line_height(16);
  // 行は block 方向（= 紙面の右から左）に積まれる
  EXPECT_FLOAT_EQ(lines[0]->rect.block_start, 0);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, line);
  EXPECT_FLOAT_EQ(lines[1]->rect.block_start, line);
  // 文字は inline 方向（上から下）に進む
  EXPECT_EQ(glyph_positions(*lines[0])[0], 0);
  EXPECT_EQ(glyph_positions(*lines[0])[1], 16);
  EXPECT_FLOAT_EQ(lines[0]->rect.inline_size, 200);
  // ルートは block 方向に 2 行ぶん伸びる（api はこれを出力の幅には使わない）
  EXPECT_FLOAT_EQ(tree->content_block_size(), 2 * line);
}

// 縦書きの「ベースライン」は行の中心軸。行の block 方向の中央に来る。
TEST(LayoutVertical, BaselineIsTheCentreAxis) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あ")})});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, fake_line_height(16));
  EXPECT_FLOAT_EQ(lines[0]->baseline, fake_line_height(16) / 2);
}

// サイズの違う断片は中心軸でそろう（横書きのような ascent / descent の非対称は使わない）。
TEST(LayoutVertical, FragmentsOfDifferentSizesShareTheCentreAxis) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block(
      {text("あ"), inline_box({text("い")}, [](ComputedStyle& style) { style.font_size = 32; })})});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, fake_line_height(32));
  EXPECT_FLOAT_EQ(lines[0]->baseline, fake_line_height(32) / 2);
  const std::vector<const TextFragment*> fragments = text_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 2U);
  EXPECT_FLOAT_EQ(fragments[0]->baseline, lines[0]->baseline);
  EXPECT_FLOAT_EQ(fragments[1]->baseline, lines[0]->baseline);
}

// ---- 物理プロパティの読み替え（A1）------------------------------------------------

TEST(LayoutVertical, PhysicalPropertiesMapToLogicalDirections) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({}, [](ComputedStyle& style) {
    style.width = Dimension::px(30);    // block 方向のサイズ
    style.height = Dimension::px(100);  // inline 方向のサイズ
    style.margin = {Dimension::px(5), Dimension::px(7), Dimension::px(9), Dimension::px(11)};
    style.padding = {1, 2, 3, 4};
  })});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  ASSERT_EQ(rects.size(), 2U);
  // margin: top → inline-start、right → block-start
  EXPECT_FLOAT_EQ(rects[1].rect.inline_start, 5);
  EXPECT_FLOAT_EQ(rects[1].rect.block_start, 7);
  // padding: top/bottom → inline、right/left → block
  EXPECT_FLOAT_EQ(rects[1].rect.inline_size, 104);  // 100 + 1 + 3
  EXPECT_FLOAT_EQ(rects[1].rect.block_size, 36);    // 30 + 2 + 4
  // 親の block 方向の大きさは margin-right + 箱 + margin-left
  EXPECT_FLOAT_EQ(tree->root.rect.block_size, 7 + 36 + 11);
  // ルートの inline サイズは viewport の高さ
  EXPECT_FLOAT_EQ(tree->root.rect.inline_size, 200);
}

// 幅 auto のブロックは block 方向に「内容から」ではなく親から降りてこない
// （inline 方向が高さ側なので、auto の伸びるのは inline = 高さの方）。
TEST(LayoutVertical, AutoInlineSizeFillsTheViewportHeight) {
  FakeMeasurer measurer;
  const auto root = build_vertical(
      {block({text("あ")}, [](ComputedStyle& style) { style.padding = {10, 0, 10, 0}; })});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockRect> rects = block_rects(*tree);
  EXPECT_FLOAT_EQ(rects[1].rect.inline_size, 200);  // 180 + padding 20
  EXPECT_FLOAT_EQ(all_lines(*tree)[0]->rect.inline_size, 180);
  EXPECT_FLOAT_EQ(all_lines(*tree)[0]->rect.inline_start, 10);
}

// ---- text-align -------------------------------------------------------------------

TEST(LayoutVertical, TextAlignWorksOnTheInlineAxis) {
  FakeMeasurer measurer;
  const auto make = [](style::TextAlign align) {
    return build_vertical(
        {block({text("あいうえお")}, [align](ComputedStyle& style) { style.text_align = align; })});
  };
  const auto start = flow_vertical(make(style::TextAlign::Start), measurer);
  ASSERT_TRUE(start.has_value());
  EXPECT_FLOAT_EQ(glyph_positions(*all_lines(*start)[0])[0], 0);
  const auto end = flow_vertical(make(style::TextAlign::End), measurer);
  ASSERT_TRUE(end.has_value());
  EXPECT_FLOAT_EQ(glyph_positions(*all_lines(*end)[0])[0], 120);
  const auto center = flow_vertical(make(style::TextAlign::Center), measurer);
  ASSERT_TRUE(center.has_value());
  EXPECT_FLOAT_EQ(glyph_positions(*all_lines(*center)[0])[0], 60);
}

// ---- sideways ---------------------------------------------------------------------

// 縦書きの欧文は横倒し（sideways）。断片はそこで分かれる。
TEST(LayoutVertical, LatinRunsAreSideways) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あAい")})});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const TextFragment*> fragments = text_fragments(*all_lines(*tree)[0]);
  ASSERT_EQ(fragments.size(), 3U);
  EXPECT_FALSE(fragments[0]->sideways);
  EXPECT_TRUE(fragments[1]->sideways);
  EXPECT_FALSE(fragments[2]->sideways);
  EXPECT_EQ(fragments[1]->text, "A");
  EXPECT_FLOAT_EQ(fragments[1]->inline_start, 16);
  EXPECT_FLOAT_EQ(fragments[1]->inline_size, 8);  // 横倒しなので送りは半角ぶん
}

// ---- 禁則・あふれ処理 ---------------------------------------------------------------

TEST(LayoutVertical, ProhibitionAndHangingWork) {
  FakeMeasurer measurer;
  // 行の長さ 80（全角 5 文字）。素朴に詰めると 6 文字目の「。」が行頭に来る
  const auto root = build_vertical({block({text("あいうえお。かきくけこ")})});
  const auto tree = run_layout(root, vertical_options(400, 80), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あいうえ", "お。かきく", "けこ"}));

  // ぶら下げ: 行の inline 方向の端（下）の外に出る
  const auto hanging = build_vertical({block({text("あいうえお。")})});
  Options options = vertical_options(400, 84);
  options.line_break.overflow = linebreak::OverflowPolicy::Burasage;
  const auto hung = run_layout(hanging, options, measurer);
  ASSERT_TRUE(hung.has_value());
  ASSERT_EQ(all_lines(*hung).size(), 1U);
  const std::vector<float> positions = glyph_positions(*all_lines(*hung)[0]);
  ASSERT_EQ(positions.size(), 6U);
  EXPECT_FLOAT_EQ(positions.back(), 80);
  EXPECT_GT(positions.back() + 16, 84);
}

// 追い込みも縦で効く（約物の Spacing は字送り方向なのでそのまま使える）。
TEST(LayoutVertical, OikomiTightensPunctuation) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あ、いうえおか")})});
  Options options = vertical_options(400, 104);
  options.line_break.overflow = linebreak::OverflowPolicy::Oikomi;
  const auto tree = run_layout(root, options, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(all_lines(*tree).size(), 1U);
  EXPECT_EQ(glyph_positions(*all_lines(*tree)[0]), (std::vector<float>{0, 16, 24, 40, 56, 72, 88}));
}

// ---- インライン背景 ----------------------------------------------------------------

// 縦書きの背景は中心軸から ±(font-size / 2)。
TEST(LayoutVertical, InlineBackgroundIsCentredOnTheAxis) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({inline_box({text("あい")}, [](ComputedStyle& style) {
    style.background_color = Color{0, 0, 255, 255};
  })})});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const InlineBackground*> rects = backgrounds(*lines[0]);
  ASSERT_EQ(rects.size(), 1U);
  EXPECT_FLOAT_EQ(rects[0]->rect.inline_start, 0);
  EXPECT_FLOAT_EQ(rects[0]->rect.inline_size, 32);
  EXPECT_FLOAT_EQ(rects[0]->rect.block_start, lines[0]->baseline - 8);
  EXPECT_FLOAT_EQ(rects[0]->rect.block_size, 16);
}

// ---- <img> ------------------------------------------------------------------------

// 縦書きの画像は回転せず、行の中心軸に中央揃えする。
TEST(LayoutVertical, InlineImageIsCentredOnTheAxis) {
  FakeMeasurer measurer;
  const ImageLookup images = image_table({{.src = "photo", .id = 4, .width = 80, .height = 40}});
  const auto root = build_vertical({block({text("あ"), img("photo")})});
  const auto tree = run_layout(root, vertical_options(400, 200), measurer, images);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  // 画像は物理寸法のまま: inline（縦）方向が高さ 40、block（横）方向が幅 80
  const std::vector<const ImageFragment*> fragments = image_fragments(*lines[0]);
  ASSERT_EQ(fragments.size(), 1U);
  EXPECT_FLOAT_EQ(fragments[0]->rect.inline_size, 40);
  EXPECT_FLOAT_EQ(fragments[0]->rect.block_size, 80);
  EXPECT_FLOAT_EQ(fragments[0]->rect.inline_start, 16);
  // 中心軸に中央揃え
  EXPECT_FLOAT_EQ(fragments[0]->rect.block_start, lines[0]->baseline - 40);
  EXPECT_FLOAT_EQ(lines[0]->rect.block_size, 80);
  EXPECT_FLOAT_EQ(lines[0]->baseline, 40);
}

// ---- flex -------------------------------------------------------------------------

// row の主軸は inline 方向（縦書きでは上から下）。
TEST(LayoutVertical, FlexRowRunsAlongTheInlineAxis) {
  FakeMeasurer measurer;
  const auto root = build_vertical({flex({block({},
                                                [](ComputedStyle& style) {
                                                  style.height = Dimension::px(40);
                                                  style.width = Dimension::px(20);
                                                }),
                                          block({},
                                                [](ComputedStyle& style) {
                                                  style.height = Dimension::px(60);
                                                  style.width = Dimension::px(30);
                                                })},
                                         [](ComputedStyle& style) { style.column_gap = 10; })});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 2U);
  EXPECT_FLOAT_EQ(items[0].rect.inline_start, 0);
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 40);
  EXPECT_FLOAT_EQ(items[1].rect.inline_start, 50);  // 40 + gap 10
  EXPECT_FLOAT_EQ(items[1].rect.inline_size, 60);
  // 交差軸（block = 横）はアイテムの最大
  EXPECT_FLOAT_EQ(tree->root.blocks()->front().rect.block_size, 30);
}

// column の主軸は block 方向（縦書きでは右から左）。
TEST(LayoutVertical, FlexColumnRunsAlongTheBlockAxis) {
  FakeMeasurer measurer;
  const auto root = build_vertical(
      {flex({block({}, [](ComputedStyle& style) { style.width = Dimension::px(20); }),
             block({}, [](ComputedStyle& style) { style.width = Dimension::px(30); })},
            [](ComputedStyle& style) {
              style.flex_direction = FlexDirection::Column;
              style.row_gap = 10;
            })});
  const auto tree = flow_vertical(root, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 2U);
  EXPECT_FLOAT_EQ(items[0].rect.block_start, 0);
  EXPECT_FLOAT_EQ(items[0].rect.block_size, 20);
  EXPECT_FLOAT_EQ(items[1].rect.block_start, 30);  // 20 + gap 10
  // stretch なので inline（縦）方向は viewport の高さいっぱい
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 200);
}

// ---- エラー -----------------------------------------------------------------------

TEST(LayoutVertical, VerticalNeedsAViewportHeight) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({text("あ")})});
  const auto tree = run_layout(root, make_options(400), measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(tree.error().message.find("viewport height"), std::string::npos);
}

// ---- dump -------------------------------------------------------------------------

TEST(LayoutVertical, DumpJson) {
  FakeMeasurer measurer;
  measurer.ascent_ratio = 0.75F;
  measurer.descent_ratio = 0.25F;
  const auto root = build_vertical({block({text("あ")})});
  const auto tree = run_layout(root, vertical_options(100, 48), measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(dump_json(*tree), R"JSON({
  "writing_mode": "vertical-rl",
  "viewport_width": 100,
  "viewport_height": 48,
  "root": {
    "tag": "#root",
    "rect": [
      0,
      0,
      48,
      16
    ],
    "blocks": [
      {
        "tag": "div",
        "rect": [
          0,
          0,
          48,
          16
        ],
        "lines": [
          {
            "rect": [
              0,
              0,
              48,
              16
            ],
            "baseline": 8,
            "fragments": [
              {
                "type": "text",
                "text": "あ",
                "font": 0,
                "font_size": 16,
                "color": "#000000ff",
                "inline_start": 0,
                "inline_size": 16,
                "baseline": 8,
                "location": "1:1",
                "glyphs": [
                  [
                    12354,
                    0,
                    0,
                    0
                  ]
                ]
              }
            ]
          }
        ]
      }
    ]
  }
})JSON");
}

}  // namespace
}  // namespace shashoku::layout::test
