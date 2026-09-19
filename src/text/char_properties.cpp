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

// UAX #50 VerticalOrientation-18.0.0.txt から R 以外の範囲を抜き出し、
// 同じ値の隣接範囲をまとめたもの（lo の昇順・重なりなし）。載っていない文字は R
// （データファイルの @missing 行が 0000..10FFFF; R）。
constexpr std::array kVerticalOrientationTable = std::to_array<VoRange>({
    {0x000A7, 0x000A7, Vo::Upright},
    {0x000A9, 0x000A9, Vo::Upright},
    {0x000AE, 0x000AE, Vo::Upright},
    {0x000B1, 0x000B1, Vo::Upright},
    {0x000BC, 0x000BE, Vo::Upright},
    {0x000D7, 0x000D7, Vo::Upright},
    {0x000F7, 0x000F7, Vo::Upright},
    {0x002EA, 0x002EB, Vo::Upright},
    {0x01100, 0x011FF, Vo::Upright},
    {0x01401, 0x0167F, Vo::Upright},
    {0x018B0, 0x018FF, Vo::Upright},
    {0x02016, 0x02016, Vo::Upright},
    {0x02018, 0x02019, Vo::TransformedRotated},
    {0x0201C, 0x0201D, Vo::TransformedRotated},
    {0x02020, 0x02021, Vo::Upright},
    {0x02030, 0x02031, Vo::Upright},
    {0x0203B, 0x0203C, Vo::Upright},
    {0x02042, 0x02042, Vo::Upright},
    {0x02047, 0x02049, Vo::Upright},
    {0x02051, 0x02051, Vo::Upright},
    {0x02065, 0x02065, Vo::Upright},
    {0x020DD, 0x020E0, Vo::Upright},
    {0x020E2, 0x020E4, Vo::Upright},
    {0x02100, 0x02101, Vo::Upright},
    {0x02103, 0x02109, Vo::Upright},
    {0x0210F, 0x0210F, Vo::Upright},
    {0x02113, 0x02114, Vo::Upright},
    {0x02116, 0x02117, Vo::Upright},
    {0x0211E, 0x02123, Vo::Upright},
    {0x02125, 0x02125, Vo::Upright},
    {0x02127, 0x02127, Vo::Upright},
    {0x02129, 0x02129, Vo::Upright},
    {0x0212E, 0x0212E, Vo::Upright},
    {0x02135, 0x0213F, Vo::Upright},
    {0x02145, 0x0214A, Vo::Upright},
    {0x0214C, 0x0214D, Vo::Upright},
    {0x0214F, 0x02189, Vo::Upright},
    {0x0218C, 0x0218F, Vo::Upright},
    {0x0221E, 0x0221E, Vo::Upright},
    {0x02234, 0x02235, Vo::Upright},
    {0x02300, 0x02307, Vo::Upright},
    {0x0230C, 0x0231F, Vo::Upright},
    {0x02324, 0x02328, Vo::Upright},
    {0x02329, 0x0232A, Vo::TransformedRotated},
    {0x0232B, 0x0232B, Vo::Upright},
    {0x0237D, 0x0239A, Vo::Upright},
    {0x023BE, 0x023CD, Vo::Upright},
    {0x023CF, 0x023CF, Vo::Upright},
    {0x023D1, 0x023DB, Vo::Upright},
    {0x023E2, 0x02422, Vo::Upright},
    {0x02424, 0x024FF, Vo::Upright},
    {0x025A0, 0x02619, Vo::Upright},
    {0x02620, 0x02767, Vo::Upright},
    {0x02776, 0x02793, Vo::Upright},
    {0x02B12, 0x02B2F, Vo::Upright},
    {0x02B50, 0x02B59, Vo::Upright},
    {0x02B97, 0x02B97, Vo::Upright},
    {0x02BB8, 0x02BD1, Vo::Upright},
    {0x02BD3, 0x02BEB, Vo::Upright},
    {0x02BF0, 0x02BFF, Vo::Upright},
    {0x02E50, 0x02E51, Vo::Upright},
    {0x02E80, 0x03000, Vo::Upright},
    {0x03001, 0x03002, Vo::TransformedUpright},  // 、。
    {0x03003, 0x03007, Vo::Upright},
    {0x03008, 0x03011, Vo::TransformedRotated},  // 〈〉《》「」『』【】
    {0x03012, 0x03013, Vo::Upright},
    {0x03014, 0x0301F, Vo::TransformedRotated},
    {0x03020, 0x0302F, Vo::Upright},
    {0x03030, 0x03030, Vo::TransformedRotated},
    {0x03031, 0x03040, Vo::Upright},
    {0x03041, 0x03041, Vo::TransformedUpright},
    {0x03042, 0x03042, Vo::Upright},
    {0x03043, 0x03043, Vo::TransformedUpright},
    {0x03044, 0x03044, Vo::Upright},
    {0x03045, 0x03045, Vo::TransformedUpright},
    {0x03046, 0x03046, Vo::Upright},
    {0x03047, 0x03047, Vo::TransformedUpright},
    {0x03048, 0x03048, Vo::Upright},
    {0x03049, 0x03049, Vo::TransformedUpright},
    {0x0304A, 0x03062, Vo::Upright},
    {0x03063, 0x03063, Vo::TransformedUpright},
    {0x03064, 0x03082, Vo::Upright},
    {0x03083, 0x03083, Vo::TransformedUpright},
    {0x03084, 0x03084, Vo::Upright},
    {0x03085, 0x03085, Vo::TransformedUpright},
    {0x03086, 0x03086, Vo::Upright},
    {0x03087, 0x03087, Vo::TransformedUpright},
    {0x03088, 0x0308D, Vo::Upright},
    {0x0308E, 0x0308E, Vo::TransformedUpright},
    {0x0308F, 0x03094, Vo::Upright},
    {0x03095, 0x03096, Vo::TransformedUpright},
    {0x03097, 0x0309A, Vo::Upright},
    {0x0309B, 0x0309C, Vo::TransformedUpright},
    {0x0309D, 0x0309F, Vo::Upright},
    {0x030A0, 0x030A0, Vo::TransformedRotated},
    {0x030A1, 0x030A1, Vo::TransformedUpright},
    {0x030A2, 0x030A2, Vo::Upright},
    {0x030A3, 0x030A3, Vo::TransformedUpright},
    {0x030A4, 0x030A4, Vo::Upright},
    {0x030A5, 0x030A5, Vo::TransformedUpright},
    {0x030A6, 0x030A6, Vo::Upright},
    {0x030A7, 0x030A7, Vo::TransformedUpright},
    {0x030A8, 0x030A8, Vo::Upright},
    {0x030A9, 0x030A9, Vo::TransformedUpright},
    {0x030AA, 0x030C2, Vo::Upright},
    {0x030C3, 0x030C3, Vo::TransformedUpright},
    {0x030C4, 0x030E2, Vo::Upright},
    {0x030E3, 0x030E3, Vo::TransformedUpright},
    {0x030E4, 0x030E4, Vo::Upright},
    {0x030E5, 0x030E5, Vo::TransformedUpright},
    {0x030E6, 0x030E6, Vo::Upright},
    {0x030E7, 0x030E7, Vo::TransformedUpright},
    {0x030E8, 0x030ED, Vo::Upright},
    {0x030EE, 0x030EE, Vo::TransformedUpright},
    {0x030EF, 0x030F4, Vo::Upright},
    {0x030F5, 0x030F6, Vo::TransformedUpright},
    {0x030F7, 0x030FB, Vo::Upright},
    {0x030FC, 0x030FC, Vo::TransformedRotated},  // ー
    {0x030FD, 0x03126, Vo::Upright},
    {0x03127, 0x03127, Vo::TransformedUpright},
    {0x03128, 0x031B3, Vo::Upright},
    {0x031B4, 0x031B7, Vo::TransformedUpright},
    {0x031B8, 0x031BA, Vo::Upright},
    {0x031BB, 0x031BB, Vo::TransformedUpright},
    {0x031BC, 0x031EF, Vo::Upright},
    {0x031F0, 0x031FF, Vo::TransformedUpright},
    {0x03200, 0x032FE, Vo::Upright},
    {0x032FF, 0x03357, Vo::TransformedUpright},
    {0x03358, 0x0337A, Vo::Upright},
    {0x0337B, 0x0337F, Vo::TransformedUpright},
    {0x03380, 0x0A4CF, Vo::Upright},
    {0x0A960, 0x0A97F, Vo::Upright},
    {0x0AC00, 0x0D7FF, Vo::Upright},
    {0x0E000, 0x0FAFF, Vo::Upright},
    {0x0FE10, 0x0FE1F, Vo::Upright},
    {0x0FE30, 0x0FE48, Vo::Upright},
    {0x0FE50, 0x0FE52, Vo::TransformedUpright},
    {0x0FE53, 0x0FE57, Vo::Upright},
    {0x0FE59, 0x0FE5E, Vo::TransformedRotated},
    {0x0FE5F, 0x0FE62, Vo::Upright},
    {0x0FE67, 0x0FE6F, Vo::Upright},
    {0x0FF01, 0x0FF01, Vo::TransformedUpright},
    {0x0FF02, 0x0FF07, Vo::Upright},
    {0x0FF08, 0x0FF09, Vo::TransformedRotated},  // （）
    {0x0FF0A, 0x0FF0B, Vo::Upright},
    {0x0FF0C, 0x0FF0C, Vo::TransformedUpright},
    {0x0FF0E, 0x0FF0E, Vo::TransformedUpright},
    {0x0FF0F, 0x0FF19, Vo::Upright},
    {0x0FF1A, 0x0FF1B, Vo::TransformedRotated},
    {0x0FF1F, 0x0FF1F, Vo::TransformedUpright},
    {0x0FF20, 0x0FF3A, Vo::Upright},
    {0x0FF3B, 0x0FF3B, Vo::TransformedRotated},
    {0x0FF3C, 0x0FF3C, Vo::Upright},
    {0x0FF3D, 0x0FF3D, Vo::TransformedRotated},
    {0x0FF3E, 0x0FF3E, Vo::Upright},
    {0x0FF3F, 0x0FF3F, Vo::TransformedRotated},
    {0x0FF40, 0x0FF5A, Vo::Upright},
    {0x0FF5B, 0x0FF60, Vo::TransformedRotated},
    {0x0FFE0, 0x0FFE2, Vo::Upright},
    {0x0FFE3, 0x0FFE3, Vo::TransformedRotated},
    {0x0FFE4, 0x0FFE7, Vo::Upright},
    {0x0FFF0, 0x0FFF8, Vo::Upright},
    {0x0FFFC, 0x0FFFD, Vo::Upright},
    {0x10980, 0x1099F, Vo::Upright},
    {0x11580, 0x115FF, Vo::Upright},
    {0x11A00, 0x11ABF, Vo::Upright},
    {0x13000, 0x1467F, Vo::Upright},
    {0x16FE0, 0x191DF, Vo::Upright},
    {0x1AFF0, 0x1B131, Vo::Upright},
    {0x1B132, 0x1B132, Vo::TransformedUpright},
    {0x1B133, 0x1B14F, Vo::Upright},
    {0x1B150, 0x1B152, Vo::TransformedUpright},
    {0x1B153, 0x1B154, Vo::Upright},
    {0x1B155, 0x1B155, Vo::TransformedUpright},
    {0x1B156, 0x1B163, Vo::Upright},
    {0x1B164, 0x1B168, Vo::TransformedUpright},
    {0x1B169, 0x1B2FF, Vo::Upright},
    {0x1CEC0, 0x1CEDC, Vo::Upright},
    {0x1CEE0, 0x1CEF0, Vo::Upright},
    {0x1CEFE, 0x1CFCF, Vo::Upright},
    {0x1D000, 0x1D1FF, Vo::Upright},
    {0x1D250, 0x1D281, Vo::Upright},
    {0x1D2E0, 0x1D37F, Vo::Upright},
    {0x1D800, 0x1DAAF, Vo::Upright},
    {0x1F000, 0x1F1FF, Vo::Upright},
    {0x1F200, 0x1F201, Vo::TransformedUpright},
    {0x1F202, 0x1F7FF, Vo::Upright},
    {0x1F900, 0x1FAFF, Vo::Upright},
    {0x20000, 0x2FFFD, Vo::Upright},
    {0x30000, 0x3FFFD, Vo::Upright},
    {0xF0000, 0xFFFFD, Vo::Upright},
    {0x100000, 0x10FFFD, Vo::Upright},
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
