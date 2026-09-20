#include <cstddef>
#include <string>
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
