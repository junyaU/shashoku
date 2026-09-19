#include "core/geometry.hpp"

#include <gtest/gtest.h>

namespace shashoku {
namespace {

TEST(Geometry, Defaults) {
  EXPECT_EQ(Point{}, (Point{.x = 0, .y = 0}));
  EXPECT_EQ(Size{}, (Size{.width = 0, .height = 0}));
  EXPECT_EQ(Rect{}, (Rect{.x = 0, .y = 0, .width = 0, .height = 0}));
}

TEST(Geometry, RectEdges) {
  const Rect rect{.x = 10, .y = 20, .width = 30, .height = 40};
  EXPECT_EQ(rect.right(), 40.0F);
  EXPECT_EQ(rect.bottom(), 60.0F);
  EXPECT_FALSE(rect.empty());
}

TEST(Geometry, RectEdgesWithNegativeOrigin) {
  const Rect rect{.x = -5, .y = -2.5F, .width = 10, .height = 5};
  EXPECT_EQ(rect.right(), 5.0F);
  EXPECT_EQ(rect.bottom(), 2.5F);
}

// 幅か高さが 0 以下なら空（塗るピクセルがない）
TEST(Geometry, RectIsEmptyWhenAnyExtentIsNotPositive) {
  EXPECT_TRUE((Rect{.x = 0, .y = 0, .width = 0, .height = 10}.empty()));
  EXPECT_TRUE((Rect{.x = 0, .y = 0, .width = 10, .height = 0}.empty()));
  EXPECT_TRUE((Rect{.x = 0, .y = 0, .width = -1, .height = 10}.empty()));
  EXPECT_TRUE((Rect{.x = 0, .y = 0, .width = 10, .height = -1}.empty()));
  EXPECT_FALSE((Rect{.x = 0, .y = 0, .width = 0.5F, .height = 0.5F}.empty()));
}

TEST(Geometry, Equality) {
  EXPECT_EQ((Point{.x = 1, .y = 2}), (Point{.x = 1, .y = 2}));
  EXPECT_NE((Point{.x = 1, .y = 2}), (Point{.x = 2, .y = 1}));
  EXPECT_NE((Size{.width = 1, .height = 2}), (Size{.width = 1, .height = 3}));
  EXPECT_NE((Rect{.x = 0, .y = 0, .width = 1, .height = 1}),
            (Rect{.x = 0, .y = 0, .width = 1, .height = 2}));
}

// 並びは CSS と同じ「上・右・下・左」
TEST(Geometry, EdgesKeepCssOrder) {
  const Edges<float> margin{.top = 1, .right = 2, .bottom = 3, .left = 4};
  EXPECT_EQ(margin.top, 1.0F);
  EXPECT_EQ(margin.right, 2.0F);
  EXPECT_EQ(margin.bottom, 3.0F);
  EXPECT_EQ(margin.left, 4.0F);
  EXPECT_EQ(margin, (Edges<float>{.top = 1, .right = 2, .bottom = 3, .left = 4}));
  EXPECT_NE(margin, (Edges<float>{.top = 4, .right = 3, .bottom = 2, .left = 1}));
}

TEST(Geometry, EdgesDefaultToValueInitialized) {
  EXPECT_EQ(Edges<float>{}, (Edges<float>{.top = 0, .right = 0, .bottom = 0, .left = 0}));
  EXPECT_EQ(Edges<int>{}, (Edges<int>{.top = 0, .right = 0, .bottom = 0, .left = 0}));
}

}  // namespace
}  // namespace shashoku
