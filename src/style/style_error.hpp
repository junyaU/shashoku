#pragma once

#include "shashoku/error.hpp"

// style の中でだけ使う診断の道具（ARCHITECTURE.md A46 / A48 / A55）。
//
// hint 付きのエラーを作る `error_with_hint()` / `fail_with_hint()` は **A55 の追記で
// `core/result.hpp` に移した**（① html も対応外タグの hint に使うので、「1 モジュールのために
// core を太らせない」という A48 の理由が当てはまらなくなった）。ここに残すのは
// 「集めて続行できる種類か」の判定だけ。

namespace shashoku::style {

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
    // AI が同じ往復で両方を直せるようにする（`ErrorKind` は増やさない）。
    case ErrorKind::UnsupportedTag:
      return true;
    default:
      return false;
  }
}

}  // namespace shashoku::style
