#pragma once

#include <cstdint>

namespace shashoku {

// 入力 HTML 内の位置。line / column は 1 始まり、column は UTF-8 バイト単位ではなく
// コードポイント単位（エディタの表示と一致させる）。offset は先頭からのバイト数。
// エラーと警告の両方が使う（ARCHITECTURE.md A46 で error.hpp から独立させた）。
struct SourceLocation {
  std::uint32_t offset = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 1;

  bool operator==(const SourceLocation&) const = default;
};

}  // namespace shashoku
