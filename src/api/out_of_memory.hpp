#pragma once

#include <expected>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include "shashoku/error.hpp"

namespace shashoku::detail {

// メモリ不足（ARCHITECTURE.md A26）。
//
// 「例外を投げない・捕まえない」（ARCHITECTURE.md §2）の**唯一の例外規定**。
// 公開関数の境界でだけ std::bad_alloc / std::length_error を捕まえ、OutOfMemory にして返す。
// 内部では従来どおり例外を使わず、失敗は Result<T> で返す。`catch` があってよいのは
// このヘッダだけで、render() / dump() / LoadedFonts::prepare() / LoadedImages::prepare() が
// 使う。
//
// これは最善努力である: Linux の既定のオーバーコミットでは、確保そのものは成功して
// あとから OOM killer に殺されるので bad_alloc が来ないことがある。メモリの保証は
// RenderLimits の側で行う（limits.hpp）。
template <class T, class Body>
std::expected<T, RenderError> catch_out_of_memory(Body&& body) {
  try {
    return std::forward<Body>(body)();
  } catch (const std::bad_alloc&) {
    return std::unexpected(RenderError{.kind = ErrorKind::OutOfMemory,
                                       .message = "out of memory (std::bad_alloc)",
                                       .location = std::nullopt});
  } catch (const std::length_error&) {
    return std::unexpected(
        RenderError{.kind = ErrorKind::OutOfMemory,
                    .message = "out of memory (a container exceeded its maximum size)",
                    .location = std::nullopt});
  }
}

}  // namespace shashoku::detail
