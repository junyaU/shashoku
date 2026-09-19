#pragma once

#include <cstdint>

namespace shashoku::text {

// UAX #50 Vertical_Orientation。縦書きで 1 文字をどう置くかを決める
// （ARCHITECTURE.md §3.5）。既定値は Rotated（VerticalOrientation.txt の @missing）。
enum class VerticalOrientation : std::uint8_t {
  Upright,             // U:  そのまま立てる
  TransformedUpright,  // Tu: vert があれば差し替え、無ければ立てる
  TransformedRotated,  // Tr: vert があれば差し替え、無ければ横倒し
  Rotated,             // R:  横倒し（欧文・数字）
};

// https://www.unicode.org/Public/UCD/latest/ucd/VerticalOrientation.txt （18.0.0）
[[nodiscard]] VerticalOrientation vertical_orientation(char32_t cp) noexcept;

// 直前の文字と同じクラスタ・同じ run に入れるべき文字か
// （結合文字・異体字セレクタ・ZWJ / ZWNJ・絵文字修飾子）。
// これらを単独でフォント選択すると、基底文字と別のフォントに割れてしまう。
[[nodiscard]] bool is_cluster_extender(char32_t cp) noexcept;

// ZWJ（U+200D）。直後の文字も同じクラスタに取り込むために使う。
[[nodiscard]] constexpr bool is_zero_width_joiner(char32_t cp) noexcept { return cp == 0x200D; }

}  // namespace shashoku::text
