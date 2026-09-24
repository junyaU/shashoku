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
  // `font-family` で要求したフォントがどれも読み込まれておらず、
  // 要求を満たさないフォントで描いた（A57）。
  // 「満たした」= 並びのどれかが読み込んだフォントの family 名に一致した、または `sans-serif` /
  // `system-ui` / `ui-sans-serif` があった（既定のフォールバックがサンセリフ）。
  // `serif` / `monospace` などの他の総称は解釈できないので、それしか無ければ満たしていない
  FontNotFound,
};

// ContentOverflow で、最大の超過量を出した紙面の辺（物理。縦書きでも Bottom は物理の下）。
// 「下に出た（背が高すぎる）」と「右に出た（幅が広すぎる）」は直し方が違うので、機械が分けて読めるように持つ。
enum class OverflowEdge : std::uint8_t { None, Top, Right, Bottom, Left };

struct Warning {
  WarningKind kind = WarningKind::MissingGlyph;
  // 人が読むための説明。位置が分かっていれば末尾に付く（RenderError と同じ " at L:C"）。
  // 例: "no font has a glyph for U+1F600 at 3:14"
  //     "content overflows the canvas by 42.5px (bottom) at 12:3"
  //     "no requested font family is loaded (`Hiragino Mincho ProN`, `serif`);
  //      text uses `Noto Sans JP` instead at 3:14"
  std::string detail;
  // MissingGlyph のときの該当コードポイント（他の種類では 0）
  char32_t codepoint = 0;
  // MissingGlyph: その文字を含むテキストノードの**先頭**の位置（ARCHITECTURE.md A31 / issue #9）。
  // 文字単位の桁ではない: 文字参照（`&#x1F600;`）や空白の畳み込みを遡らないと正確に
  // 出せないので、正確に出せない桁を出すより「どのノードか」に留めてある。
  // ContentOverflow: はみ出した箱を作った要素の位置（最も外側の該当要素）。
  // FontNotFound: その font-family の並びを使うテキストノードのうち、
  // 入力順で最初のものの**先頭**の位置（宣言の位置ではない。並びごとに 1 件。A57）。
  std::optional<SourceLocation> location;
  // ContentOverflow のとき、紙面の外に出た量の最大（CSS px、正の値）。他の種類では 0
  float overflow_px = 0.0F;
  // ContentOverflow のとき、その超過量を出した辺。他の種類では None
  OverflowEdge overflow_edge = OverflowEdge::None;

  bool operator==(const Warning&) const = default;
};

// "missing-glyph" / "content-overflow" / "font-not-found" のようなケバブケースの識別子
std::string_view to_string(WarningKind kind) noexcept;

// "top" / "right" / "bottom" / "left"。None は ""
std::string_view to_string(OverflowEdge edge) noexcept;

}  // namespace shashoku
