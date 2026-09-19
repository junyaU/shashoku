#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "text/text_measurer.hpp"

namespace shashoku::text {

class FontStore;

namespace detail {
struct ShaperImpl;  // HarfBuzz のハンドルを隠す（このヘッダに HarfBuzz を漏らさない）
}  // namespace detail

// 豆腐（グリフ欠落）の記録。Shaper が溜め、api が Warning に変換する
// （ARCHITECTURE.md §3.5 / DESIGN.md §6-6）。
struct MissingGlyph {
  char32_t cp = 0;

  bool operator==(const MissingGlyph&) const = default;
};

// ③ レイアウトに注入する TextMeasurer の本物の実装（HarfBuzz）。
// FontStore を参照するだけで所有しない。FontStore は Shaper より長生きすること。
//
// 決定性（DESIGN.md §3-5）: script / language を明示指定してロケールを読まない、
// フォールバック順は FontId の昇順、内部キャッシュは出力に影響しない。
class Shaper final : public TextMeasurer {
 public:
  explicit Shaper(const FontStore& fonts);
  ~Shaper() override;
  Shaper(const Shaper&) = delete;
  Shaper& operator=(const Shaper&) = delete;
  Shaper(Shaper&&) = delete;
  Shaper& operator=(Shaper&&) = delete;

  ShapedText shape(std::u32string_view text, const TextStyle& style) override;
  FontMetrics metrics(const TextStyle& style) override;

  // 溜まった豆腐を取り出して空にする。同じコードポイントは 1 回しか入らない。
  [[nodiscard]] std::vector<MissingGlyph> take_missing_glyphs();

 private:
  std::unique_ptr<detail::ShaperImpl> impl_;
};

}  // namespace shashoku::text
