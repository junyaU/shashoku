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
  return std::unexpected(Error{kind, std::move(message), location});
}

}  // namespace shashoku
