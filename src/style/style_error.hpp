#pragma once

#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "shashoku/source_location.hpp"

// style の中でだけ使う診断の道具（ARCHITECTURE.md A46 / A48）。
//
//   * hint 付きのエラーを作る（`core/result.hpp` の `fail()` は hint を取らない。
//     core は全モジュールの共有物なので、A46 のために増やさず style の中に置く）
//   * 「集めて続行できる種類か」の判定を 1 か所に集める

namespace shashoku::style {

// 「代わりにどう書くか」を `RenderError::hint` に入れて返す。message には混ぜない（A46）。
inline Error error_with_hint(ErrorKind kind, std::string message,
                             std::optional<SourceLocation> location, std::string hint) {
  return Error{.kind = kind,
               .message = std::move(message),
               .location = location,
               .hint = std::move(hint),
               .warning = std::nullopt};
}

inline std::unexpected<Error> fail_with_hint(ErrorKind kind, std::string message,
                                             std::optional<SourceLocation> location,
                                             std::string hint) {
  return std::unexpected(error_with_hint(kind, std::move(message), location, std::move(hint)));
}

// 集めて続行してよい種類（A46 の「② style が集めるもの」）。ここに無いもの
// （`LimitExceeded` / `Internal` / `OutOfMemory`）は、解決を続けても意味が無いか
// shashoku 側のバグなので、その場で `unexpected` で止める。
inline bool is_recoverable(ErrorKind kind) noexcept {
  switch (kind) {
    case ErrorKind::CssParse:
    case ErrorKind::UnsupportedProperty:
    case ErrorKind::UnsupportedValue:
    case ErrorKind::UnsupportedLayout:
    // 対応外のタグを名指しするセレクタ（A55）。要素側の `<body>` と同じ識別子で報告して、
    // AI が同じ往復で両方を直せるようにする（`ErrorKind` は増やさない）
    case ErrorKind::UnsupportedTag:
      return true;
    default:
      return false;
  }
}

}  // namespace shashoku::style
