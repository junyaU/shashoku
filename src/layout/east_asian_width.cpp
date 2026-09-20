#include "layout/east_asian_width.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace shashoku::layout {
namespace {

struct Range {
  char32_t first;
  char32_t last;
};

// EastAsianWidth.txt の W と F。昇順・重なりなし（二分探索する）。
// 表は scripts/gen_unicode_tables.py が生成する（docs/UNICODE_TABLES.md）。手で編集しない。
constexpr std::array kWideRanges = std::to_array<Range>({
#include "layout/east_asian_width_table.inc"
});

}  // namespace

bool is_fullwidth(char32_t cp) {
  // first <= cp となる最後の範囲を二分探索し、その last と比べる。
  std::size_t low = 0;
  std::size_t high = kWideRanges.size();
  while (low < high) {
    const std::size_t mid = low + ((high - low) / 2);
    if (cp < kWideRanges[mid].first) {
      high = mid;
    } else {
      low = mid + 1;
    }
  }
  return low > 0 && cp <= kWideRanges[low - 1].last;
}

}  // namespace shashoku::layout
