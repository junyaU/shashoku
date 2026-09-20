#include "text/char_properties.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace shashoku::text {
namespace {

using Vo = VerticalOrientation;

struct VoRange {
  char32_t lo;
  char32_t hi;
  Vo value;
};

// UAX #50 Vertical_Orientation。R 以外の範囲だけを持つ（lo の昇順・重なりなし）。
// 載っていない文字は R（データファイルの @missing 行が 0000..10FFFF; R）。
// 表は scripts/gen_unicode_tables.py が生成する（docs/UNICODE_TABLES.md）。手で編集しない。
constexpr std::array kVerticalOrientationTable = std::to_array<VoRange>({
#include "text/vertical_orientation_table.inc"
});

struct CpRange {
  char32_t lo;
  char32_t hi;
};

// 直前の文字にくっつく文字（lo の昇順・重なりなし）。日本語と基本ラテン、
// および絵文字列に必要な範囲だけを持つ（DESIGN.md §4「CJK 以外の複雑スクリプトは対象外」）。
constexpr std::array kClusterExtenderTable = std::to_array<CpRange>({
    {0x00300, 0x0036F},  // Combining Diacritical Marks
    {0x00483, 0x00489},  // Combining Cyrillic
    {0x00591, 0x005BD},  // Hebrew points
    {0x005BF, 0x005BF},
    {0x005C1, 0x005C2},
    {0x005C4, 0x005C5},
    {0x005C7, 0x005C7},
    {0x00610, 0x0061A},  // Arabic marks
    {0x0064B, 0x0065F},
    {0x00670, 0x00670},
    {0x006D6, 0x006DC},
    {0x006DF, 0x006E4},
    {0x006E7, 0x006E8},
    {0x006EA, 0x006ED},
    {0x00E31, 0x00E31},  // Thai
    {0x00E34, 0x00E3A},
    {0x00E47, 0x00E4E},
    {0x01AB0, 0x01AFF},  // Combining Diacritical Marks
                         // Extended
    {0x01DC0, 0x01DFF},  // Combining Diacritical Marks Supplement
    {0x0200C, 0x0200D},  // ZWNJ / ZWJ
    {0x020D0, 0x020F0},  // Combining Diacritical Marks for Symbols（U+20E3 キーキャップを含む）
    {0x02CEF, 0x02CF1},  // Coptic
    {0x0302A, 0x0302F},  // 漢字の声調記号・ハングルの記号
    {0x03099, 0x0309A},  // 濁点・半濁点（か + U+3099 = が）
    {0x0A66F, 0x0A672},
    {0x0A674, 0x0A67D},
    {0x0FE00, 0x0FE0F},  // 異体字セレクタ VS1..VS16（U+FE0F
                         // 絵文字表示セレクタを含む）
    {0x0FE20, 0x0FE2F},  // Combining Half Marks
    {0x101FD, 0x101FD},
    {0x102E0, 0x102E0},
    {0x1F3FB, 0x1F3FF},  // 絵文字の肌色修飾子
    {0xE0100, 0xE01EF},  // 異体字セレクタ補助 VS17..VS256（葛 + U+E0100）
});

template <std::size_t N, class Range>
[[nodiscard]] const Range* find_range(const std::array<Range, N>& table, char32_t cp) noexcept {
  const auto* it = std::upper_bound(table.begin(), table.end(), cp,
                                    [](char32_t value, const Range& r) { return value < r.lo; });
  if (it == table.begin()) {
    return nullptr;
  }
  --it;
  return cp <= it->hi ? it : nullptr;
}

}  // namespace

VerticalOrientation vertical_orientation(char32_t cp) noexcept {
  const VoRange* found = find_range(kVerticalOrientationTable, cp);
  return found != nullptr ? found->value : VerticalOrientation::Rotated;
}

bool is_cluster_extender(char32_t cp) noexcept {
  return find_range(kClusterExtenderTable, cp) != nullptr;
}

}  // namespace shashoku::text
