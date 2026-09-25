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

// 表（= UCD）をそのまま引く。tailoring は table_class() の外（tailor()）で行う。
[[nodiscard]] BreakClass table_class(char32_t cp) {
  const auto* it =
      std::upper_bound(kClassTable.begin(), kClassTable.end(), cp,
                       [](char32_t value, const ClassRange& range) { return value < range.lo; });
  if (it == kClassTable.begin()) {
    return BreakClass::Al;
  }
  --it;
  return cp <= it->hi ? it->cls : BreakClass::Al;
}

// shashoku の tailoring（UAX #14 §6 は実装がクラスを調整することを認めている）。
// **表（break_class_table.inc）は編集せず、上書きはここ 1 か所にまとめる。**
//
// (1) U+3000 IDEOGRAPHIC SPACE: LineBreak.txt は **BA** だが、shashoku は **SP** として扱う
//     （ARCHITECTURE.md A59）。UAX #14 の SP は U+0020 だけを含むクラスだが、
//     「行末で詰められる空白」「空白越しの規則（LB14〜LB18）の対象」という**振る舞い**は、
//     日本語の全角スペースにもそのまま当てはまる。効くのは次の 2 つ:
//       * 行末に来た U+3000 は幅に数えず content_end から落ちる（§3.4 (3)）= **ぶら下がる**。
//         CSS Text 3 §4.1.3「Phase II: Trimming and Positioning」の 4 が、行末に残った
//         white space・**other space separators**（Unicode の general category Zs から
//         U+0020 と U+00A0 を除いたもの。U+3000 はここに入る）を、`white-space` が
//         normal / nowrap なら**無条件でぶら下げる**と定めている
//       * LB14「OP SP* ×」などの空白越しの規則に乗る。「（　あ」の後ろで割れなくなるので、
//         **始め括弧が行末に残らない**（JIS X 4051 / JLREQ の行末禁則: 始め括弧類は行末に
//         置かない。JLREQ の該当節番号は未確認）
//     BA のままだと、どちらも効かない（BA は「後ろで割ってよい普通の文字」でしかない）。
//     **他の Zs は tailoring しない**: U+2000〜U+200A（BA）はぶら下げの対象だが和文では
//     使われず、U+202F NARROW NO-BREAK SPACE は分割禁止の空白（GL）なので、SP にすると
//     後ろで割れてしまう（UAX #14 の規則を壊す方向の変更になる）。
[[nodiscard]] BreakClass tailor(char32_t cp, BreakClass cls) {
  constexpr char32_t kIdeographicSpace = 0x3000;
  return cp == kIdeographicSpace ? BreakClass::Sp : cls;
}

}  // namespace

BreakClass break_class_of(char32_t cp) { return tailor(cp, table_class(cp)); }

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
