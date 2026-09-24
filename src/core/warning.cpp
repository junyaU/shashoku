#include "shashoku/warning.hpp"

#include <string_view>

namespace shashoku {

std::string_view to_string(WarningKind kind) noexcept {
  switch (kind) {
    case WarningKind::MissingGlyph:
      return "missing-glyph";
    case WarningKind::ContentOverflow:
      return "content-overflow";
    case WarningKind::FontNotFound:
      return "font-not-found";
  }
  return "unknown";
}

// 物理の辺（A50）。診断 JSON の `edge` はこの綴りをそのまま出す（機械が読む識別子）。
// None は「辺が無い」なので空文字列で、JSON では null になる。
std::string_view to_string(OverflowEdge edge) noexcept {
  switch (edge) {
    case OverflowEdge::None:
      return "";
    case OverflowEdge::Top:
      return "top";
    case OverflowEdge::Right:
      return "right";
    case OverflowEdge::Bottom:
      return "bottom";
    case OverflowEdge::Left:
      return "left";
  }
  // 列挙にない値（キャストで作られた場合）。ここに来ること自体が shashoku のバグ。
  return "";
}

}  // namespace shashoku
