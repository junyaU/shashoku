#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "shashoku/error.hpp"

namespace shashoku {

// 「続行できる問題」の種類。fail loudly の原則（DESIGN.md §3-6）の唯一の例外が豆腐で、
// エラーにせず警告として返し、□ を描いて続行する。
enum class WarningKind : std::uint8_t {
  MissingGlyph,  // どのフォントにもグリフがないコードポイントがあった（豆腐）
};

struct Warning {
  WarningKind kind = WarningKind::MissingGlyph;
  // 人が読むための説明。位置が分かっていれば末尾に付く（RenderError と同じ " at L:C"）。
  // 例: "no font has a glyph for U+1F600 at 3:14"
  std::string detail;
  // MissingGlyph のときの該当コードポイント（種類によっては 0）
  char32_t codepoint = 0;
  // その文字を含むテキストノードの**先頭**の位置（ARCHITECTURE.md A31 / issue #9）。
  // 文字単位の桁ではない: 文字参照（`&#x1F600;`）や空白の畳み込みを遡らないと正確に
  // 出せないので、正確に出せない桁を出すより「どのノードか」に留めてある。
  std::optional<SourceLocation> location;

  bool operator==(const Warning&) const = default;
};

// "missing-glyph" のようなケバブケースの識別子
std::string_view to_string(WarningKind kind) noexcept;

}  // namespace shashoku
