#pragma once

#include <expected>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
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
//
// 失敗の型は 2 つある（A46）: render() / dump() は RenderFailure（診断の列）、
// LoadedFonts / LoadedImages::prepare() は RenderError（1 件）。どちらでも 1 件の
// OutOfMemory を返すので、包み方だけを `E` で切り替える。
template <class E>
E as_failure(RenderError error) {
  if constexpr (std::is_same_v<E, RenderFailure>) {
    return RenderFailure{.errors = {std::move(error)}, .warnings = {}, .truncated = false};
  } else {
    return error;
  }
}

template <class T, class E = RenderError, class Body>
std::expected<T, E> catch_out_of_memory(Body&& body) {
  try {
    return std::forward<Body>(body)();
  } catch (const std::bad_alloc&) {
    return std::unexpected(as_failure<E>(RenderError{.kind = ErrorKind::OutOfMemory,
                                                     .message = "out of memory (std::bad_alloc)",
                                                     .location = std::nullopt,
                                                     .hint = {},
                                                     .warning = std::nullopt}));
  } catch (const std::length_error&) {
    return std::unexpected(as_failure<E>(
        RenderError{.kind = ErrorKind::OutOfMemory,
                    .message = "out of memory (a container exceeded its maximum size)",
                    .location = std::nullopt,
                    .hint = {},
                    .warning = std::nullopt}));
  }
}

}  // namespace shashoku::detail
