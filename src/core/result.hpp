#pragma once

#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "shashoku/error.hpp"

namespace shashoku {

// 内部の全モジュールが使うエラー型は公開 API の RenderError と同一にする
// （段をまたぐたびにエラー型を変換しないため）。
using Error = RenderError;

template <class T>
using Result = std::expected<T, Error>;

// return fail(ErrorKind::UnsupportedTag, "...", node.location);
inline std::unexpected<Error> fail(ErrorKind kind, std::string message,
                                   std::optional<SourceLocation> location = std::nullopt) {
  // A46 で RenderError に hint / warning が増えたので、指示付き初期化で書く
  // （位置指定のままだと -Wmissing-field-initializers に掛かる）。
  return std::unexpected(Error{.kind = kind,
                               .message = std::move(message),
                               .location = location,
                               .hint = {},
                               .warning = std::nullopt});
}

// 「代わりにどう書くか」を `RenderError::hint` に入れて作る（A46。message には混ぜない）。
// A48 では ② style だけが使うので `style/style_error.hpp` に置いたが、A55 の追記で ① html も
// 対応外タグの hint に使うようになったので core に移した（1 モジュールのためではなくなった）。
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

}  // namespace shashoku
