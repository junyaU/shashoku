#include "style/ua_stylesheet.hpp"

#include <cstdint>
#include <format>

#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "style/css_parser.hpp"
#include "style/declaration.hpp"

namespace shashoku::style {

Result<Stylesheet> build_ua_stylesheet() {
  Stylesheet sheet;
  std::uint32_t order = 0;
  const Result<void> parsed = parse_stylesheet(kUserAgentCss, SourceLocation{}, order, sheet);
  if (!parsed) {
    return fail(ErrorKind::Internal,
                std::format("the built-in user agent stylesheet failed to parse: {}",
                            parsed.error().message));
  }
  return sheet;
}

}  // namespace shashoku::style
