#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"

// 単一行 flexbox（ARCHITECTURE.md §3.8 / CSS Flexbox Level 1 §9）。
namespace shashoku::layout::test {
namespace {

using style::AlignItems;
using style::ComputedStyle;
using style::Dimension;
using style::FlexDirection;
using style::JustifyContent;

// flex コンテナ（ルートの最初の子）の子の矩形を並べて返す。
std::vector<LogicalRect> item_rects(const BoxTree& tree) {
  std::vector<LogicalRect> out;
  const std::vector<BlockBox>* items = tree.root.blocks();
  if (items == nullptr || items->empty()) {
    return out;
  }
  const std::vector<BlockBox>* children = items->front().blocks();
  if (children == nullptr) {
    return out;
  }
  for (const BlockBox& child : *children) {
    out.push_back(child.rect);
  }
  return out;
}

const BlockBox& container_of(const BoxTree& tree) { return tree.root.blocks()->front(); }

StyleFn sized(float width, float height) {
  return [width, height](ComputedStyle& style) {
    style.width = Dimension::px(width);
    style.height = Dimension::px(height);
  };
}

// ---- 基本の配置 -------------------------------------------------------------------

TEST(LayoutFlex, RowPlacesItemsAlongTheInlineAxis) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({}, sized(100, 20)), block({}, sized(50, 20))})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  ASSERT_EQ(rects.size(), 2U);
  EXPECT_FLOAT_EQ(rects[0].inline_start, 0);
  EXPECT_FLOAT_EQ(rects[0].inline_size, 100);
  EXPECT_FLOAT_EQ(rects[1].inline_start, 100);
  EXPECT_FLOAT_EQ(rects[1].inline_size, 50);
  EXPECT_FLOAT_EQ(rects[0].block_start, 0);
  EXPECT_FLOAT_EQ(rects[1].block_start, 0);
  // コンテナの高さは行の交差サイズ（アイテムの margin-box の最大）
  EXPECT_FLOAT_EQ(container_of(*tree).rect.block_size, 20);
  EXPECT_FLOAT_EQ(container_of(*tree).rect.inline_size, 400);
}

TEST(LayoutFlex, ColumnStacksItemsAlongTheBlockAxis) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({}, sized(100, 20)), block({}, sized(50, 30))}, [](ComputedStyle& style) {
        style.flex_direction = FlexDirection::Column;
        style.row_gap = 10;
      })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  ASSERT_EQ(rects.size(), 2U);
  EXPECT_FLOAT_EQ(rects[0].block_start, 0);
  EXPECT_FLOAT_EQ(rects[0].block_size, 20);
  EXPECT_FLOAT_EQ(rects[1].block_start, 30);  // 20 + gap 10
  EXPECT_FLOAT_EQ(rects[1].block_size, 30);
  // height: auto の column は内容の高さになる
  EXPECT_FLOAT_EQ(container_of(*tree).rect.block_size, 60);
}

TEST(LayoutFlex, RowGapUsesColumnGap) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({}, sized(100, 20)), block({}, sized(50, 20))},
                                [](ComputedStyle& style) { style.column_gap = 10; })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[1].inline_start, 110);
}

// ---- justify-content -------------------------------------------------------------

std::vector<float> justified(FakeMeasurer& measurer, JustifyContent justify) {
  const auto root = build(
      {flex({block({}, sized(100, 20)), block({}, sized(50, 20))}, [justify](ComputedStyle& style) {
        style.column_gap = 10;
        style.justify_content = justify;
      })});
  const auto tree = run_layout(root, 400, measurer);
  EXPECT_TRUE(tree.has_value());
  std::vector<float> out;
  for (const LogicalRect& rect : item_rects(*tree)) {
    out.push_back(rect.inline_start);
  }
  return out;
}

TEST(LayoutFlex, JustifyContentAllValues) {
  FakeMeasurer measurer;
  // 利用可能 400、使用 160（100 + 50 + gap 10）→ 余り 240
  EXPECT_EQ(justified(measurer, JustifyContent::FlexStart), (std::vector<float>{0, 110}));
  EXPECT_EQ(justified(measurer, JustifyContent::FlexEnd), (std::vector<float>{240, 350}));
  EXPECT_EQ(justified(measurer, JustifyContent::Center), (std::vector<float>{120, 230}));
  EXPECT_EQ(justified(measurer, JustifyContent::SpaceBetween), (std::vector<float>{0, 350}));
  EXPECT_EQ(justified(measurer, JustifyContent::SpaceAround), (std::vector<float>{60, 290}));
  EXPECT_EQ(justified(measurer, JustifyContent::SpaceEvenly), (std::vector<float>{80, 270}));
}

// 余りが負（あふれている）なら space-* は flex-start と同じ。
TEST(LayoutFlex, JustifyContentFallsBackWhenOverflowing) {
  FakeMeasurer measurer;
  const auto wide = [](ComputedStyle& style) {
    style.width = Dimension::px(300);
    style.height = Dimension::px(20);
    style.flex_shrink = 0;  // 縮めずにあふれさせる
  };
  const auto root = build({flex({block({}, wide), block({}, wide)}, [](ComputedStyle& style) {
    style.justify_content = JustifyContent::SpaceBetween;
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  EXPECT_FLOAT_EQ(rects[0].inline_start, 0);
  EXPECT_FLOAT_EQ(rects[1].inline_start, 300);  // はみ出したまま（クリップしない）
}

// ---- align-items -----------------------------------------------------------------

TEST(LayoutFlex, AlignItemsPositionsOnTheCrossAxis) {
  FakeMeasurer measurer;
  const auto make = [](AlignItems align) {
    return build({flex({block({}, sized(100, 20))}, [align](ComputedStyle& style) {
      style.height = Dimension::px(100);
      style.align_items = align;
    })});
  };
  const auto start = run_layout(make(AlignItems::FlexStart), 400, measurer);
  ASSERT_TRUE(start.has_value());
  EXPECT_FLOAT_EQ(item_rects(*start)[0].block_start, 0);
  const auto center = run_layout(make(AlignItems::Center), 400, measurer);
  ASSERT_TRUE(center.has_value());
  EXPECT_FLOAT_EQ(item_rects(*center)[0].block_start, 40);
  const auto end = run_layout(make(AlignItems::FlexEnd), 400, measurer);
  ASSERT_TRUE(end.has_value());
  EXPECT_FLOAT_EQ(item_rects(*end)[0].block_start, 80);
  // 高さが確定しているアイテムは stretch でも伸びない
  const auto stretch = run_layout(make(AlignItems::Stretch), 400, measurer);
  ASSERT_TRUE(stretch.has_value());
  EXPECT_FLOAT_EQ(item_rects(*stretch)[0].block_start, 0);
  EXPECT_FLOAT_EQ(item_rects(*stretch)[0].block_size, 20);
}

// 交差軸サイズが auto のアイテムは stretch で交差軸いっぱいに伸びる。
TEST(LayoutFlex, StretchFillsTheCrossAxis) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({}, [](ComputedStyle& style) { style.width = Dimension::px(100); })},
                  [](ComputedStyle& style) { style.height = Dimension::px(100); })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].block_size, 100);
}

// column の stretch は幅を伸ばす（伸ばした幅で中身を組む）。
TEST(LayoutFlex, ColumnStretchUsesTheContainerWidth) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({text("あいうえお")})}, [](ComputedStyle& style) {
    style.flex_direction = FlexDirection::Column;
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].inline_size, 400);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あいうえお"}));
}

// column の stretch 以外は shrink-to-fit = min(max-content, 利用可能幅)。
TEST(LayoutFlex, ColumnShrinkToFitWithoutStretch) {
  FakeMeasurer measurer;
  const auto make = [](float viewport) {
    (void)viewport;
    return build({flex({block({text("あいう")})}, [](ComputedStyle& style) {
      style.flex_direction = FlexDirection::Column;
      style.align_items = AlignItems::FlexStart;
    })});
  };
  const auto wide = run_layout(make(400), 400, measurer);
  ASSERT_TRUE(wide.has_value());
  EXPECT_FLOAT_EQ(item_rects(*wide)[0].inline_size, 48);  // max-content = 全角 3 文字
  // 利用可能幅の方が狭ければそちらに合わせる（min-content までは下がる）
  const auto narrow = run_layout(make(30), 30, measurer);
  ASSERT_TRUE(narrow.has_value());
  EXPECT_FLOAT_EQ(item_rects(*narrow)[0].inline_size, 30);
}

// ---- flex-grow / flex-shrink ------------------------------------------------------

TEST(LayoutFlex, GrowDistributesFreeSpaceInProportion) {
  FakeMeasurer measurer;
  const auto item = [](float grow) {
    return block({}, [grow](ComputedStyle& style) {
      style.flex_grow = grow;
      style.flex_basis = Dimension::px(0);
    });
  };
  const auto root = build({flex({item(1), item(2)})});
  const auto tree = run_layout(root, 300, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  EXPECT_FLOAT_EQ(rects[0].inline_size, 100);
  EXPECT_FLOAT_EQ(rects[1].inline_size, 200);
  EXPECT_FLOAT_EQ(rects[1].inline_start, 100);
}

TEST(LayoutFlex, ShrinkDistributesTheDeficit) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({}, [](ComputedStyle& style) { style.width = Dimension::px(100); }),
                   block({}, [](ComputedStyle& style) { style.width = Dimension::px(100); })})});
  const auto tree = run_layout(root, 100, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  EXPECT_FLOAT_EQ(rects[0].inline_size, 50);
  EXPECT_FLOAT_EQ(rects[1].inline_size, 50);
}

// 自動最小サイズ: row のアイテムは min-content 幅より小さくならない（§9.7 の最小サイズ違反）。
TEST(LayoutFlex, ShrinkStopsAtTheAutomaticMinimumSize) {
  FakeMeasurer measurer;
  const auto item = []() {
    return block({text("abcdef")},  // 欧文 6 文字 = 48px（空白がないので割れない）
                 [](ComputedStyle& style) { style.width = Dimension::px(100); });
  };
  const auto root = build({flex({item(), item()})});
  const auto tree = run_layout(root, 60, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  EXPECT_FLOAT_EQ(rects[0].inline_size, 48);
  EXPECT_FLOAT_EQ(rects[1].inline_size, 48);
  EXPECT_FLOAT_EQ(rects[1].inline_start, 48);  // 合計 96 > 60。あふれたまま
}

// ---- flex-basis -------------------------------------------------------------------

TEST(LayoutFlex, FlexBasisPxPercentAndAuto) {
  FakeMeasurer measurer;
  const auto root = build({flex({
      block({},
            [](ComputedStyle& style) {  // basis が width より優先
              style.flex_basis = Dimension::px(80);
              style.width = Dimension::px(200);
            }),
      block({}, [](ComputedStyle& style) { style.flex_basis = Dimension::percent(25); }),
      block({}, [](ComputedStyle& style) { style.width = Dimension::px(120); }),
      block({text("あいう")}),  // basis auto + width auto = max-content
  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  ASSERT_EQ(rects.size(), 4U);
  EXPECT_FLOAT_EQ(rects[0].inline_size, 80);
  EXPECT_FLOAT_EQ(rects[1].inline_size, 100);
  EXPECT_FLOAT_EQ(rects[2].inline_size, 120);
  EXPECT_FLOAT_EQ(rects[3].inline_size, 48);
}

// `flex: 1`（grow 1 / shrink 1 / basis 0）相当。長い和文が伸びた幅で折り返される。
TEST(LayoutFlex, FlexOneWrapsLongJapaneseText) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({text("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほ")},
                         [](ComputedStyle& style) {
                           style.flex_grow = 1;
                           style.flex_shrink = 1;
                           style.flex_basis = Dimension::px(0);
                         })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].inline_size, 400);
  EXPECT_EQ(line_texts(*tree),
            (std::vector<std::string>{"あいうえおかきくけこさしすせそたちつてとなにぬねの",
                                      "はひふへほ"}));
}

// ---- 自動最小サイズ（CSS Flexbox §4.5 / A52） ----------------------------------------
// `min-width: auto` / `min-height: auto` の flex アイテムの content-based minimum size は
// **min(specified size suggestion, content size suggestion)**（CSS Flexbox Level 1 §4.5
// "Automatic Minimum Size of Flex Items"）。
//   * specified size suggestion = **主軸のサイズプロパティ**（row なら width、column なら
//     height）が definite なら、その値
//   * content size suggestion = 主軸の min-content サイズ
// **flex-basis は specified size suggestion ではない**（A52）。したがって `flex: 1 1 0` でも
// 下限は内容で決まり、0 には潰れない。下限が効くのは §9.7-4d「最小サイズ違反」の段。

// `flex: 1` の展開（style の parse_flex は `1 1 0px` にする）。width も一緒に指定できる。
StyleFn flex_one(std::optional<float> width = std::nullopt) {
  return [width](ComputedStyle& style) {
    style.flex_grow = 1;
    style.flex_shrink = 1;
    style.flex_basis = Dimension::px(0);
    if (width) {
      style.width = Dimension::px(*width);
    }
  };
}

struct AutoMinSizeCase {
  std::string_view name;
  FlexDirection direction = FlexDirection::Row;
  float viewport = 400;                   // コンテナの inline サイズ
  std::optional<float> container_height;  // column の主軸が定まるかどうか
  std::vector<Tree> children;
  std::vector<float> expected_main;   // アイテムの主軸サイズ（border-box）
  float expected_container_main = 0;  // コンテナの主軸サイズ
};

std::vector<AutoMinSizeCase> auto_min_size_cases() {
  const float line = fake_line_height(16);  // 和文 1 行の高さ
  // 偽 TextMeasurer: 全角 = 1em（16）、ASCII = 0.5em（8）。
  // "あいうえお" の min-content は 1 文字 = 16、max-content は 80。
  // "abcdefgh" は空白がないので min-content = max-content = 64。
  return {
      {.name = "(a) 高さ auto の column + flex: 1 1 0 → 子は内容の高さ（§4.5）",
       .direction = FlexDirection::Column,
       .viewport = 400,
       .container_height = std::nullopt,
       .children = {block({text("あいうえお")}, flex_one()), block({}, sized(100, 20))},
       .expected_main = {line, 20},
       .expected_container_main = line + 20},
      {.name = "(b) 定まった column + flex: 1 1 0 → 内容の高さを下回らず、あふれる（§9.7-4d）",
       .direction = FlexDirection::Column,
       .viewport = 400,
       .container_height = 10,
       .children = {block({text("あいうえお")}, flex_one())},
       .expected_main = {line},
       .expected_container_main = 10},
      {.name = "(b') 定まった column + flex: 1 1 0 → 余りがあれば従来どおり伸びる（§9.7）",
       .direction = FlexDirection::Column,
       .viewport = 400,
       .container_height = 100,
       .children = {block({text("あいうえお")}, flex_one()), block({}, sized(100, 20))},
       .expected_main = {80, 20},
       .expected_container_main = 100},
      {.name = "(c) row + flex: 1 1 0 → min-content を下回らない（余りは残りのアイテムへ）",
       .direction = FlexDirection::Row,
       .viewport = 100,
       .container_height = std::nullopt,
       .children = {block({text("あい")}, flex_one()), block({text("abcdefgh")}, flex_one())},
       .expected_main = {36, 64},
       .expected_container_main = 100},
      {.name = "(c') row + flex: 1 1 0 → 合計がコンテナを超えるならあふれる（§4.5 の下限が優先）",
       .direction = FlexDirection::Row,
       .viewport = 60,
       .container_height = std::nullopt,
       .children = {block({text("あい")}, flex_one()), block({text("abcdefgh")}, flex_one())},
       .expected_main = {16, 64},
       .expected_container_main = 60},
      {.name = "(d) row + flex: 1 1 0 + width < min-content → min(width, min-content)（§4.5）",
       .direction = FlexDirection::Row,
       .viewport = 60,
       .container_height = std::nullopt,
       .children = {block({text("abcdefgh")}, flex_one(20)), block({text("abcdefgh")}, flex_one())},
       .expected_main = {20, 64},
       .expected_container_main = 60},
      {.name = "(e) row + flex-basis auto + width 指定 → 下限は min(width, min-content) のまま",
       .direction = FlexDirection::Row,
       .viewport = 60,
       .container_height = std::nullopt,
       .children = {block({text("abcdefgh")},
                          [](ComputedStyle& style) { style.width = Dimension::px(100); }),
                    block({text("abcdefgh")},
                          [](ComputedStyle& style) { style.width = Dimension::px(100); })},
       .expected_main = {64, 64},
       .expected_container_main = 60},
      {.name = "(f) 高さ auto の column + flex-basis: 50% → 内容の高さ（% は definite でない）",
       .direction = FlexDirection::Column,
       .viewport = 400,
       .container_height = std::nullopt,
       .children = {block({text("あいうえお")},
                          [](ComputedStyle& style) {
                            style.flex_grow = 1;
                            style.flex_basis = Dimension::percent(50);
                          }),
                    block({}, sized(100, 20))},
       .expected_main = {line, 20},
       .expected_container_main = line + 20},
  };
}

TEST(LayoutFlex, AutomaticMinimumSizeComesFromTheMainSizePropertyNotFlexBasis) {
  for (const AutoMinSizeCase& test_case : auto_min_size_cases()) {
    FakeMeasurer measurer;
    const bool row = test_case.direction == FlexDirection::Row;
    const FlexDirection direction = test_case.direction;
    const std::optional<float> height = test_case.container_height;
    const auto root = build({flex(test_case.children, [direction, height](ComputedStyle& style) {
      style.flex_direction = direction;
      if (height) {
        style.height = Dimension::px(*height);
      }
    })});
    const auto tree = run_layout(root, test_case.viewport, measurer);
    ASSERT_TRUE(tree.has_value()) << test_case.name;
    const std::vector<LogicalRect> rects = item_rects(*tree);
    ASSERT_EQ(rects.size(), test_case.expected_main.size()) << test_case.name;
    for (std::size_t i = 0; i < rects.size(); ++i) {
      const float main = row ? rects[i].inline_size : rects[i].block_size;
      EXPECT_FLOAT_EQ(main, test_case.expected_main[i]) << test_case.name << " / アイテム " << i;
    }
    const LogicalRect& container = container_of(*tree).rect;
    EXPECT_FLOAT_EQ(row ? container.inline_size : container.block_size,
                    test_case.expected_container_main)
        << test_case.name;
  }
}

// ---- auto マージン ----------------------------------------------------------------

TEST(LayoutFlex, AutoMarginOnTheMainAxisAbsorbsFreeSpace) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({}, [](ComputedStyle& style) {
    style.width = Dimension::px(100);
    style.height = Dimension::px(20);
    style.margin.left = Dimension::auto_();
  })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].inline_start, 300);
}

TEST(LayoutFlex, AutoMarginOnTheCrossAxisAbsorbsFreeSpace) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({},
                                       [](ComputedStyle& style) {
                                         style.width = Dimension::px(100);
                                         style.height = Dimension::px(20);
                                         style.margin.top = Dimension::auto_();
                                       })},
                                [](ComputedStyle& style) { style.height = Dimension::px(100); })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].block_start, 80);
}

// 高さが確定した column + margin-top: auto でフッターを下端に押し付ける（OG 画像の定番）。
TEST(LayoutFlex, ColumnWithAutoTopMarginPushesTheLastItemDown) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({}, sized(100, 20)), block({},
                                                    [](ComputedStyle& style) {
                                                      style.width = Dimension::px(100);
                                                      style.height = Dimension::px(30);
                                                      style.margin.top = Dimension::auto_();
                                                    })},
                  [](ComputedStyle& style) {
                    style.flex_direction = FlexDirection::Column;
                    style.height = Dimension::px(200);
                  })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  EXPECT_FLOAT_EQ(rects[0].block_start, 0);
  EXPECT_FLOAT_EQ(rects[1].block_start, 170);
  EXPECT_FLOAT_EQ(rects[1].block_end(), 200);
}

// column で高さが auto なら伸びる余地がない（grow は効かない）。
TEST(LayoutFlex, ColumnWithAutoHeightDoesNotGrow) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({},
                         [](ComputedStyle& style) {
                           style.height = Dimension::px(20);
                           style.flex_grow = 1;
                         })},
                  [](ComputedStyle& style) { style.flex_direction = FlexDirection::Column; })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].block_size, 20);
  EXPECT_FLOAT_EQ(container_of(*tree).rect.block_size, 20);
}

// ---- アイテムの作られ方 ------------------------------------------------------------

TEST(LayoutFlex, TextChildBecomesAnAnonymousItem) {
  FakeMeasurer measurer;
  const auto root = build({flex({text("  "), text("あい"), block({}, sized(50, 10))})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *container_of(*tree).blocks();
  ASSERT_EQ(items.size(), 2U);
  EXPECT_EQ(items[0].tag, "#anonymous");
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 32);
  EXPECT_EQ(items[1].tag, "div");
  EXPECT_FLOAT_EQ(items[1].rect.inline_start, 32);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あい"}));
}

TEST(LayoutFlex, BlankTextChildDoesNotBecomeAnItem) {
  FakeMeasurer measurer;
  const auto root = build({flex({text("\n  "), block({}, sized(50, 10)), text("  ")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(container_of(*tree).blocks()->size(), 1U);
}

TEST(LayoutFlex, InlineElementChildIsBlockified) {
  FakeMeasurer measurer;
  const auto root = build({flex({inline_box({text("あい")}), block({}, sized(50, 10))})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *container_of(*tree).blocks();
  ASSERT_EQ(items.size(), 2U);
  EXPECT_EQ(items[0].tag, "span");
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 32);
}

// ---- 入れ子 -----------------------------------------------------------------------

TEST(LayoutFlex, NestedFlexContainers) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({flex({block({}, sized(30, 10)), block({}, sized(40, 10))},
                        [](ComputedStyle& style) { style.flex_direction = FlexDirection::Column; }),
                   block({}, sized(50, 10))},
                  [](ComputedStyle& style) { style.column_gap = 10; })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *container_of(*tree).blocks();
  ASSERT_EQ(items.size(), 2U);
  // 内側 column の幅は max-content（アイテムの最大 = 40）、高さは和 = 20
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 40);
  EXPECT_FLOAT_EQ(items[0].rect.block_size, 20);
  EXPECT_FLOAT_EQ(items[1].rect.inline_start, 50);
  const std::vector<BlockBox>& inner = *items[0].blocks();
  ASSERT_EQ(inner.size(), 2U);
  EXPECT_FLOAT_EQ(inner[0].rect.block_start, 0);
  EXPECT_FLOAT_EQ(inner[1].rect.block_start, 10);
}

// flex アイテムの中の block と IFC が動く。
TEST(LayoutFlex, ItemsContainBlocksAndInlineFormattingContexts) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({block({text("あい")}), block({text("うえ")})},
                         [](ComputedStyle& style) { style.width = Dimension::px(200); })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あい", "うえ"}));
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].block_size, 2 * fake_line_height(16));
}

// ---- 余白・枠線 -------------------------------------------------------------------

TEST(LayoutFlex, ItemMarginPaddingAndBorderCountTowardTheMainAxis) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({},
                                       [](ComputedStyle& style) {
                                         style.width = Dimension::px(100);
                                         style.height = Dimension::px(20);
                                         style.margin = {Dimension::px(0), Dimension::px(5),
                                                         Dimension::px(0), Dimension::px(5)};
                                         style.padding = {0, 4, 0, 4};
                                         style.border_width = 2;
                                       }),
                                 block({}, sized(50, 20))})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<LogicalRect> rects = item_rects(*tree);
  EXPECT_FLOAT_EQ(rects[0].inline_start, 5);
  EXPECT_FLOAT_EQ(rects[0].inline_size, 112);   // 100 + padding 8 + border 4
  EXPECT_FLOAT_EQ(rects[1].inline_start, 122);  // 5 + 112 + 5
}

TEST(LayoutFlex, ContainerPaddingOffsetsTheContentBox) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({}, sized(100, 20))},
                                [](ComputedStyle& style) { style.padding = {10, 10, 10, 10}; })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].inline_start, 10);
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].block_start, 10);
  EXPECT_FLOAT_EQ(container_of(*tree).rect.block_size, 40);
}

// ---- 固有寸法 ---------------------------------------------------------------------
// column の shrink-to-fit を通して min-content / max-content を確かめる。

TEST(LayoutFlex, IntrinsicSizesOfLatinText) {
  FakeMeasurer measurer;
  const auto make = []() {
    return build({flex({block({text("ab cdef")})}, [](ComputedStyle& style) {
      style.flex_direction = FlexDirection::Column;
      style.align_items = AlignItems::FlexStart;
    })});
  };
  // 利用可能幅が広ければ max-content（7 文字 × 0.5em = 56）
  const auto wide = run_layout(make(), 400, measurer);
  ASSERT_TRUE(wide.has_value());
  EXPECT_FLOAT_EQ(item_rects(*wide)[0].inline_size, 56);
  // 狭ければ min-content（最長の語 "cdef" = 32）までしか下がらない
  const auto narrow = run_layout(make(), 20, measurer);
  ASSERT_TRUE(narrow.has_value());
  EXPECT_FLOAT_EQ(item_rects(*narrow)[0].inline_size, 32);
}

TEST(LayoutFlex, IntrinsicSizeOfNestedBlocksIsTheMaximum) {
  FakeMeasurer measurer;
  const auto root = build({flex({block({block({text("あいうえお")}), block({text("あい")})})},
                                [](ComputedStyle& style) {
                                  style.flex_direction = FlexDirection::Column;
                                  style.align_items = AlignItems::FlexStart;
                                })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].inline_size, 80);  // 深い方の max-content
}

// 強制改行があると max-content は「もっとも長い行」になる。
TEST(LayoutFlex, IntrinsicSizeSplitsAtForcedBreaks) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({text("あいうえお"), br(), text("あい")})}, [](ComputedStyle& style) {
        style.flex_direction = FlexDirection::Column;
        style.align_items = AlignItems::FlexStart;
      })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_FLOAT_EQ(item_rects(*tree)[0].inline_size, 80);
}

// ---- DESIGN.md Phase 6 の受け入れ条件 ------------------------------------------------
// 「アイコン + タイトル + フッター」の典型的な OG 画像レイアウトが組める。

TEST(LayoutFlex, OpenGraphImageLayout) {
  FakeMeasurer measurer;
  const ImageLookup images = image_table({{.src = "icon", .id = 3, .width = 96, .height = 96}});
  const auto root = build({flex(
      {
          // ヘッダ: アイコン + サイト名
          flex({img("icon"), text("しゃしょく通信")},
               [](ComputedStyle& style) { style.column_gap = 16; }),
          // タイトル: 残りの高さを全部もらい、長い和文を禁則つきで折り返す
          block({text("日本語の組版を、ブラウザなしで、正しく。タイトルはここに入ります。")},
                [](ComputedStyle& style) {
                  style.flex_grow = 1;
                  style.flex_basis = Dimension::px(0);
                  style.font_size = 48;
                }),
          // フッター
          block({text("example.com")}),
      },
      [](ComputedStyle& style) {
        style.flex_direction = FlexDirection::Column;
        style.height = Dimension::px(510);  // content-box（A11）: 630 − padding 60 × 2
        style.padding = {60, 60, 60, 60};
      })});
  Options options = make_options(1200);
  options.viewport_height = 630;
  const auto tree = run_layout(root, options, measurer, images);
  ASSERT_TRUE(tree.has_value());

  const BlockBox& container = container_of(*tree);
  EXPECT_FLOAT_EQ(container.rect.inline_size, 1200);
  EXPECT_FLOAT_EQ(container.rect.block_size, 630);
  const std::vector<BlockBox>& items = *container.blocks();
  ASSERT_EQ(items.size(), 3U);

  // ヘッダ: 高さはアイコンの 96px、アイコンとサイト名が gap 16 で並ぶ
  EXPECT_FLOAT_EQ(items[0].rect.block_start, 60);
  EXPECT_FLOAT_EQ(items[0].rect.block_size, 96);
  const std::vector<const ImageFragment*> icons = all_images(*tree);
  ASSERT_EQ(icons.size(), 1U);
  EXPECT_FLOAT_EQ(icons[0]->rect.inline_start, 60);
  EXPECT_FLOAT_EQ(icons[0]->rect.inline_size, 96);
  EXPECT_FLOAT_EQ(icons[0]->rect.block_size, 96);
  const std::vector<BlockBox>& header_items = *items[0].blocks();
  ASSERT_EQ(header_items.size(), 2U);
  EXPECT_FLOAT_EQ(header_items[1].rect.inline_start, 172);  // 60 + 96 + 16

  // タイトル: 余りの高さを全部取る
  const float footer_height = fake_line_height(16);
  EXPECT_FLOAT_EQ(items[1].rect.block_start, 156);
  EXPECT_FLOAT_EQ(items[1].rect.block_size, 510 - 96 - footer_height);
  EXPECT_FLOAT_EQ(items[1].rect.inline_size, 1080);

  // フッターは content の下端にぴったり付く
  EXPECT_FLOAT_EQ(items[2].rect.block_end(), 570);

  // タイトルは折り返し、句点が行頭に出ない（A4 / Phase 4）
  const std::vector<std::string> title = line_texts(items[1]);
  ASSERT_GE(title.size(), 2U);
  for (const std::string& line : title) {
    EXPECT_NE(line.rfind("。", 0), 0U) << line;
    EXPECT_NE(line.rfind("、", 0), 0U) << line;
  }
}

}  // namespace
}  // namespace shashoku::layout::test
