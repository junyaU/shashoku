#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "shashoku/error.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {

// shape() / metrics() は Result を返す（A30 / issue #3）。テストの大半は「成功するはず」
// なので、ここで 1 回だけ剥がす。失敗したらそのテストを落とす。
inline ShapedText shape_ok(TextMeasurer& measurer, std::u32string_view text,
                           const TextStyle& style) {
  Result<ShapedText> out = measurer.shape(text, style);
  EXPECT_TRUE(out.has_value()) << (out.has_value() ? std::string{} : to_string(out.error()));
  return out.has_value() ? std::move(*out) : ShapedText{};
}

inline FontMetrics metrics_ok(TextMeasurer& measurer, const TextStyle& style) {
  const Result<FontMetrics> out = measurer.metrics(style);
  EXPECT_TRUE(out.has_value()) << (out.has_value() ? std::string{} : to_string(out.error()));
  return out.value_or(FontMetrics{});
}

}  // namespace shashoku::text

namespace shashoku::text::assets {

// text_measurer.hpp が約束する不変条件:
//   clusters は入力全体を隙間なく覆い、text_begin は単調増加、
//   glyph の範囲も順に並んで ShapedText::glyphs をちょうど覆う。
inline void expect_valid_clusters(const ShapedText& shaped, std::size_t text_length) {
  if (text_length == 0) {
    EXPECT_TRUE(shaped.clusters.empty());
    EXPECT_TRUE(shaped.glyphs.empty());
    return;
  }
  ASSERT_FALSE(shaped.clusters.empty());
  EXPECT_EQ(shaped.clusters.front().text_begin, 0U);
  EXPECT_EQ(shaped.clusters.back().text_end, text_length);
  EXPECT_EQ(shaped.clusters.front().glyph_begin, 0U);
  EXPECT_EQ(shaped.clusters.back().glyph_end, shaped.glyphs.size());

  for (std::size_t i = 0; i < shaped.clusters.size(); ++i) {
    const ShapedCluster& cluster = shaped.clusters[i];
    EXPECT_LT(cluster.text_begin, cluster.text_end) << "空のクラスタ " << i;
    EXPECT_LE(cluster.glyph_begin, cluster.glyph_end) << "クラスタ " << i;
    EXPECT_LE(cluster.text_end, text_length) << "クラスタ " << i;
    if (i > 0) {
      EXPECT_EQ(cluster.text_begin, shaped.clusters[i - 1].text_end) << "クラスタ " << i;
      EXPECT_EQ(cluster.glyph_begin, shaped.clusters[i - 1].glyph_end) << "クラスタ " << i;
    }
  }
}

}  // namespace shashoku::text::assets
