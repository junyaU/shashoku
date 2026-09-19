#include "linebreak/break_class.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace shashoku::linebreak {
namespace {

// 分割クラス表。LineBreak.txt 18.0.0（Unicode 18.0）から機械的に起こし、
// 隣り合う同クラスの範囲を併合してある。Al は既定値なので表に載せない。
//
// UAX #14 のクラスのうち、ARCHITECTURE.md §3.4 (1) が挙げていないものの寄せ先:
//   AI SG XX SA CB HL     -> Al   （LB1「未知は AL」。SA は Mn/Mc も含めて AL に寄せる）
//   HH                    -> Hy   （UAX #14 18.0 で HY から分かれた明示ハイフン。
//                                   LB12a / LB20a / LB21 での振る舞いが HY と同じ）
//   JL JV JT H2 H3        -> Id   （LB27「ハングル音節は ID」）
//   AK AP AS VF VI        -> Al   （ブラーフミー系。LB28a は実装しない = 割らない側に倒す）
struct ClassRange {
  char32_t lo;
  char32_t hi;
  BreakClass cls;
};

constexpr std::array kClassTable = std::to_array<ClassRange>({
#include "linebreak/break_class_table.inc"
});

// 小さな集合を線形に引く（要素数が高々数十なので二分探索より単純で速い）。
[[nodiscard]] bool contains(std::initializer_list<char32_t> set, char32_t cp) {
  return std::ranges::find(set, cp) != set.end();
}

}  // namespace

BreakClass break_class_of(char32_t cp) {
  const auto* it =
      std::upper_bound(kClassTable.begin(), kClassTable.end(), cp,
                       [](char32_t value, const ClassRange& range) { return value < range.lo; });
  if (it == kClassTable.begin()) {
    return BreakClass::Al;
  }
  --it;
  return cp <= it->hi ? it->cls : BreakClass::Al;
}

bool is_east_asian_bracket(char32_t cp) {
  // Line_Break が OP / CP かつ East_Asian_Width が F / W / H のコードポイント
  // （EastAsianWidth.txt 18.0.0 から抽出）。CP 側は該当なし。
  return contains({0x2329, 0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0x3016, 0x3018, 0x301A,
                   0x301D, 0xFE17, 0xFE35, 0xFE37, 0xFE39, 0xFE3B, 0xFE3D, 0xFE3F, 0xFE41, 0xFE43,
                   0xFE47, 0xFE59, 0xFE5B, 0xFE5D, 0xFF08, 0xFF3B, 0xFF5B, 0xFF5F, 0xFF62},
                  cp);
}

bool is_loose_centered_punctuation(char32_t cp) {
  return contains({0x203C, 0x2047, 0x2048, 0x2049, 0x30FB, 0xFF01, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF65},
                  cp);
}

bool is_iteration_mark(char32_t cp) {
  return contains({0x3005, 0x303B, 0x309D, 0x309E, 0x30FD, 0x30FE}, cp);
}

bool is_wide_numeric_affix(char32_t cp) {
  // PO: ° ‰ ′ ″ ‵ ℃ ℉ ﹪ ％ ￠ / PR: ¤ ± € № ﹩ ＄ ￡ ￥ ￦
  return contains({0x00A4, 0x00B0, 0x00B1, 0x2030, 0x2032, 0x2033, 0x2035, 0x2103, 0x2109, 0x2116,
                   0x20AC, 0xFE69, 0xFE6A, 0xFF04, 0xFF05, 0xFFE0, 0xFFE1, 0xFFE5, 0xFFE6},
                  cp);
}

bool is_cjk_hyphen_like(char32_t cp) { return cp == 0x301C || cp == 0x30A0; }

bool is_loose_hyphen(char32_t cp) { return cp == 0x2010 || cp == 0x2013; }

}  // namespace shashoku::linebreak
