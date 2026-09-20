#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"

// fail loudly（DESIGN.md §3-6）: 未実装・対応外はエラーで返す。黙って崩さない。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;
using style::Display;

TEST(LayoutError, BlockInsideInlineIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あ"), inline_box({block({text("い")})})})});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("inline"), std::string::npos);
  EXPECT_TRUE(tree.error().location.has_value());
}

TEST(LayoutError, ImageNotFound) {
  FakeMeasurer measurer;
  const ImageLookup images = image_table({{.src = "icon", .id = 0, .width = 10, .height = 10}});
  // インライン級（IFC の中）
  const auto inline_root = build({block({text("あ"), img("missing")})});
  const auto inline_tree = run_layout(inline_root, 200, measurer, images);
  ASSERT_FALSE(inline_tree.has_value());
  EXPECT_EQ(inline_tree.error().kind, ErrorKind::ImageNotFound);
  EXPECT_NE(inline_tree.error().message.find("missing"), std::string::npos);
  EXPECT_TRUE(inline_tree.error().location.has_value());
  // ブロック級
  const auto block_root =
      build({img("missing", std::nullopt, std::nullopt,
                 [](ComputedStyle& style) { style.display = Display::Block; })});
  const auto block_tree = run_layout(block_root, 200, measurer, images);
  ASSERT_FALSE(block_tree.has_value());
  EXPECT_EQ(block_tree.error().kind, ErrorKind::ImageNotFound);
  // flex アイテム
  const auto flex_root = build({flex({img("missing")})});
  const auto flex_tree = run_layout(flex_root, 200, measurer, images);
  ASSERT_FALSE(flex_tree.has_value());
  EXPECT_EQ(flex_tree.error().kind, ErrorKind::ImageNotFound);
}

// A1: writing-mode は文書全体で 1 つ。
TEST(LayoutError, MixedWritingModesAreRejected) {
  FakeMeasurer measurer;
  const auto root = build({block(
      {text("あ")}, [](ComputedStyle& style) { style.writing_mode = WritingMode::VerticalRl; })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("writing-mode"), std::string::npos);
  EXPECT_TRUE(tree.error().location.has_value());
}

TEST(LayoutError, InvalidViewportSize) {
  FakeMeasurer measurer;
  const auto root = build({});
  for (const float width : {0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()}) {
    const auto tree = run_layout(root, width, measurer);
    ASSERT_FALSE(tree.has_value());
    EXPECT_EQ(tree.error().kind, ErrorKind::InvalidOption);
    EXPECT_FALSE(tree.error().location.has_value());
  }
  Options opts = make_options(200);
  opts.viewport_height = 0;
  const auto tree = run_layout(root, opts, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::InvalidOption);
}

TEST(LayoutError, PercentageHeightIsRejected) {
  FakeMeasurer measurer;
  const auto root =
      build({block({}, [](ComputedStyle& style) { style.height = Dimension::percent(50); })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedValue);
  EXPECT_TRUE(tree.error().location.has_value());
}

// 有効な viewport_height はそのままボックスツリーに載る（paint が物理座標に使う）。
TEST(LayoutError, ViewportHeightIsCarried) {
  FakeMeasurer measurer;
  Options opts = make_options(200);
  opts.viewport_height = 630;
  const auto tree = run_layout(build({}), opts, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_TRUE(tree->viewport_height.has_value());
  EXPECT_FLOAT_EQ(tree->viewport_height.value_or(0), 630);
}

}  // namespace
}  // namespace shashoku::layout::test
