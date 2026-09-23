#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "shashoku/source_location.hpp"

namespace shashoku {

// 「続行できる問題」の種類。fail loudly の原則（DESIGN.md §3-6）で、エラーにせず警告として
// 返して描画は続行するもの。利用者は `RenderOptions::warnings_as_errors` でエラーに格上げできる
// （ARCHITECTURE.md A46）。
enum class WarningKind : std::uint8_t {
  MissingGlyph,  // どのフォントにもグリフがないコードポイントがあった（豆腐。A31 / A43）
  ContentOverflow,  // 箱（行・置換要素・ブロック）が出力の紙面の外に出ていて、その部分が切れる（A46）
};

struct Warning {
  WarningKind kind = WarningKind::MissingGlyph;
  // 人が読むための説明。位置が分かっていれば末尾に付く（RenderError と同じ " at L:C"）。
  // 例: "no font has a glyph for U+1F600 at 3:14"
  //     "content overflows the canvas by 42.5px (bottom) at 12:3"
  std::string detail;
  // MissingGlyph のときの該当コードポイント（他の種類では 0）
  char32_t codepoint = 0;
  // MissingGlyph: その文字を含むテキストノードの**先頭**の位置（ARCHITECTURE.md A31 / issue #9）。
  // 文字単位の桁ではない: 文字参照（`&#x1F600;`）や空白の畳み込みを遡らないと正確に
  // 出せないので、正確に出せない桁を出すより「どのノードか」に留めてある。
  // ContentOverflow: はみ出した箱を作った要素の位置（最も外側の該当要素）。
  std::optional<SourceLocation> location;
  // ContentOverflow のとき、紙面の外に出た量の最大（CSS px、正の値）。他の種類では 0
  float overflow_px = 0.0F;

  bool operator==(const Warning&) const = default;
};

// "missing-glyph" / "content-overflow" のようなケバブケースの識別子
std::string_view to_string(WarningKind kind) noexcept;

}  // namespace shashoku
