#include "shashoku/warning.hpp"

#include <string_view>

namespace shashoku {

std::string_view to_string(WarningKind kind) noexcept {
  switch (kind) {
    case WarningKind::MissingGlyph:
      return "missing-glyph";
  }
  return "unknown";
}

}  // namespace shashoku
