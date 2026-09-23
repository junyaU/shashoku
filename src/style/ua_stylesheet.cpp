#include "style/ua_stylesheet.hpp"

#include <cstdint>
#include <format>

#include "core/diagnostics.hpp"
#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "style/css_parser.hpp"
#include "style/declaration.hpp"

namespace shashoku::style {

Result<Stylesheet> build_ua_stylesheet() {
  Stylesheet sheet;
  std::uint32_t order = 0;
  // UA スタイルシートは shashoku が持っている定数なので、読めないのは入力の問題ではなく
  // shashoku のバグ（Internal）。A46 の「集めて続行」で診断に入ったものも同じ扱いにする。
  Diagnostics diagnostics{1};
  const Result<void> parsed =
      parse_stylesheet(kUserAgentCss, SourceLocation{}, order, sheet, diagnostics);
  if (!parsed) {
    return fail(ErrorKind::Internal,
                std::format("the built-in user agent stylesheet failed to parse: {}",
                            parsed.error().message));
  }
  if (diagnostics.has_errors()) {
    return fail(ErrorKind::Internal,
                std::format("the built-in user agent stylesheet failed to parse: {}",
                            diagnostics.errors().front().message));
  }
  return sheet;
}

}  // namespace shashoku::style
