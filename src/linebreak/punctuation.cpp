#include "linebreak/punctuation.hpp"

#include <algorithm>
#include <array>

namespace shashoku::linebreak {
namespace {

// 対象は ARCHITECTURE.md §3.4 (4) が挙げる全角の約物だけ。半角の ( ) , . : ; は
// 字送りがもともと字面ぶんしかないので、詰められる空きを持たない。
constexpr std::array<char32_t, 12> kOpen{
    0x300C,  // 「
    0x300E,  // 『
    0xFF08,  // （
    0x3014,  // 〔
    0xFF3B,  // ［
    0xFF5B,  // ｛
    0x3008,  // 〈
    0x300A,  // 《
    0x3010,  // 【
    0x3016,  // 〖
    0x3018,  // 〘
    0x301D,  // 〝
};

constexpr std::array<char32_t, 12> kClose{
    0x300D,  // 」
    0x300F,  // 』
    0xFF09,  // ）
    0x3015,  // 〕
    0xFF3D,  // ］
    0xFF5D,  // ｝
    0x3009,  // 〉
    0x300B,  // 》
    0x3011,  // 】
    0x3017,  // 〗
    0x3019,  // 〙
    0x301F,  // 〟
};

}  // namespace

PunctKind punct_kind(char32_t cp) {
  if (cp == 0x3001 || cp == 0xFF0C) {  // 、 ，
    return PunctKind::Comma;
  }
  if (cp == 0x3002 || cp == 0xFF0E) {  // 。 ．
    return PunctKind::Period;
  }
  if (cp == 0x30FB || cp == 0xFF1A || cp == 0xFF1B) {  // ・ ： ；
    return PunctKind::MiddleDot;
  }
  if (std::ranges::find(kOpen, cp) != kOpen.end()) {
    return PunctKind::Open;
  }
  if (std::ranges::find(kClose, cp) != kClose.end()) {
    return PunctKind::Close;
  }
  return PunctKind::None;
}

bool is_hanging_punctuation(char32_t cp) {
  return cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E;
}

}  // namespace shashoku::linebreak
