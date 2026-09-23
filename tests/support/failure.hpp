#pragma once

#include <gtest/gtest.h>

#include "shashoku/error.hpp"

// A46 で render() / dump() の失敗が `RenderError` 1 件から `RenderFailure`（診断の列）に
// 変わった。既存のテストの多くは「失敗の kind と位置」を見ているので、列の 1 件目を
// 取り出す小さな補助を置く。
//
// いまは html / style が診断を集めない（集めて続行するのは W1 / W2）ので、失敗の
// `errors` は必ず 1 件。集めるようになったら、テストは「何件出たか」も見るようになる。

namespace shashoku::test {

inline RenderError first_error(const RenderFailure& failure) {
  // 空の RenderFailure は契約違反（errors は 1 件以上）。落として原因を見せる。
  if (failure.errors.empty()) {
    ADD_FAILURE() << "RenderFailure の errors が空（A46 では 1 件以上のはず）";
    return RenderError{};
  }
  return failure.errors.front();
}

}  // namespace shashoku::test
