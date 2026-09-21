#include "core/number_text.hpp"

#include <cmath>
#include <format>
#include <limits>
#include <string>

#include <gtest/gtest.h>

// エラーメッセージの数値の表記。NaN の符号ビットは CPU によって違う
// （x86 の SSE は `inf / inf` で負の NaN、ARM は正の NaN）ので、そのまま出すと
// 同じ入力でも文面が環境で変わる。
namespace shashoku {
namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// 本題: 符号ビットが立っていてもいなくても同じ文面になる。
TEST(NumberText, NanIsWrittenTheSameWayWhateverItsSignBit) {
  const float quiet = std::numeric_limits<float>::quiet_NaN();
  const float positive = std::copysign(quiet, 1.0F);
  const float negative = std::copysign(quiet, -1.0F);
  ASSERT_TRUE(std::isnan(positive));
  ASSERT_TRUE(std::isnan(negative));
  ASSERT_TRUE(std::signbit(negative));
  ASSERT_FALSE(std::signbit(positive));

  EXPECT_EQ(number_text(positive), "NaN");
  EXPECT_EQ(number_text(negative), "NaN");
  EXPECT_EQ(number_text(positive), number_text(negative));
}

// 実際に非有限を作る式（flex の比が通る `inf / inf`）でも同じ。
TEST(NumberText, NanFromADivisionIsWrittenTheSameWay) {
  const float ratio = kInf / kInf;
  ASSERT_TRUE(std::isnan(ratio));
  EXPECT_EQ(number_text(ratio), "NaN");
}

// 無限大の符号は入力で決まる（環境に依らない）のでそのまま出す。
TEST(NumberText, InfinitiesKeepTheirSign) {
  EXPECT_EQ(number_text(kInf), "inf");
  EXPECT_EQ(number_text(-kInf), "-inf");
}

// 有限の値の表記は変えない（std::format と 1 文字も違わない）。
TEST(NumberText, FiniteValuesAreUnchanged) {
  for (const float value : {0.0F, -0.0F, 1.0F, -2.0F, 1.5F, 23.171875F, 1e7F, 3.4e38F, 1e-7F}) {
    EXPECT_EQ(number_text(value), std::format("{}", value));
  }
}

}  // namespace
}  // namespace shashoku
