#include "shashoku/error.hpp"

#include <string>
#include <string_view>

namespace shashoku {

std::string_view to_string(ErrorKind kind) noexcept {
  switch (kind) {
    case ErrorKind::InvalidUtf8:
      return "invalid-utf8";
    case ErrorKind::HtmlParse:
      return "html-parse";
    case ErrorKind::UnsupportedTag:
      return "unsupported-tag";
    case ErrorKind::UnsupportedAttribute:
      return "unsupported-attribute";
    case ErrorKind::CssParse:
      return "css-parse";
    case ErrorKind::UnsupportedProperty:
      return "unsupported-property";
    case ErrorKind::UnsupportedValue:
      return "unsupported-value";
    case ErrorKind::UnsupportedLayout:
      return "unsupported-layout";
    case ErrorKind::FontLoad:
      return "font-load";
    case ErrorKind::NoFonts:
      return "no-fonts";
    case ErrorKind::ImageDecode:
      return "image-decode";
    case ErrorKind::ImageNotFound:
      return "image-not-found";
    case ErrorKind::InvalidOption:
      return "invalid-option";
    case ErrorKind::LimitExceeded:
      return "limit-exceeded";
    case ErrorKind::OutOfMemory:
      return "out-of-memory";
    case ErrorKind::Internal:
      return "internal";
    case ErrorKind::WarningAsError:
      return "warning-as-error";
  }
  // 列挙にない値（キャストで作られた場合）。ここに来ること自体が shashoku のバグ。
  return "internal";
}

std::string to_string(const RenderError& error) {
  std::string out = "error[";
  out += to_string(error.kind);
  out += ']';
  if (error.location) {
    out += " at ";
    out += std::to_string(error.location->line);
    out += ':';
    out += std::to_string(error.location->column);
  }
  out += ": ";
  out += error.message;
  return out;
}

// 1 行 1 件（ARCHITECTURE.md A46。error.hpp のコメントが契約）。
// 失敗が 1 件だけで hint も警告も無いときは to_string(RenderError) と同じ 1 行になる。
std::string to_string(const RenderFailure& failure) {
  std::string out;
  const auto newline = [&out] {
    if (!out.empty()) {
      out += '\n';
    }
  };
  for (const RenderError& error : failure.errors) {
    newline();
    out += to_string(error);
    if (!error.hint.empty()) {
      newline();
      out += "  hint: ";
      out += error.hint;
    }
  }
  for (const Warning& warning : failure.warnings) {
    newline();
    out += "warning[";
    out += to_string(warning.kind);
    out += "]: ";
    out += warning.detail;
  }
  if (failure.truncated) {
    newline();
    out += "(diagnostics truncated at ";
    out += std::to_string(failure.errors.size() + failure.warnings.size());
    out += ')';
  }
  return out;
}

}  // namespace shashoku
