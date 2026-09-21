#pragma once

#include <memory>
#include <string_view>

#include "text/text_measurer.hpp"

namespace shashoku::text {

class FontStore;

namespace detail {
struct ShaperImpl;  // HarfBuzz のハンドルを隠す（このヘッダに HarfBuzz を漏らさない）
}  // namespace detail

// ③ レイアウトに注入する TextMeasurer の本物の実装（HarfBuzz）。
// FontStore を参照するだけで所有しない。FontStore は Shaper より長生きすること。
//
// 決定性（DESIGN.md §3-5）: script / language を明示指定してロケールを読まない、
// フォールバック順は FontId の昇順、内部キャッシュは出力に影響しない。
//
// **副作用を持たない**（A31 / issue #9）: 豆腐は `ShapedCluster::missing` で返すだけで、
// Shaper 自身は何も溜めない。だから同じ段落を何度シェーピングしても結果が変わらず、
// 「どの入力位置の文字か」を知っている ③ レイアウトが警告を組み立てられる。
class Shaper final : public TextMeasurer {
 public:
  explicit Shaper(const FontStore& fonts);
  ~Shaper() override;
  Shaper(const Shaper&) = delete;
  Shaper& operator=(const Shaper&) = delete;
  Shaper(Shaper&&) = delete;
  Shaper& operator=(Shaper&&) = delete;

  Result<ShapedText> shape(std::u32string_view text, const TextStyle& style) override;
  Result<FontMetrics> metrics(const TextStyle& style) override;

 private:
  std::unique_ptr<detail::ShaperImpl> impl_;
};

}  // namespace shashoku::text
