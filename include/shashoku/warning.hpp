#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace shashoku {

// 「続行できる問題」の種類。fail loudly の原則（DESIGN.md §3-6）の唯一の例外が豆腐で、
// エラーにせず警告として返し、□ を描いて続行する。
enum class WarningKind : std::uint8_t {
  MissingGlyph,  // どのフォントにもグリフがないコードポイントがあった（豆腐）
};

struct Warning {
  WarningKind kind = WarningKind::MissingGlyph;
  // 人が読むための説明。例: "no font has a glyph for U+1F600"
  std::string detail;
  // MissingGlyph のときの該当コードポイント（種類によっては 0）
  char32_t codepoint = 0;

  bool operator==(const Warning&) const = default;
};

// "missing-glyph" のようなケバブケースの識別子
std::string_view to_string(WarningKind kind) noexcept;

}  // namespace shashoku
