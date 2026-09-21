#pragma once

#include "core/result.hpp"
#include "layout/box_tree.hpp"

// ③ レイアウトの出口の検査（ARCHITECTURE.md A36 の第 2 段階 / issue #19）。
namespace shashoku::layout {

// BoxTree を 1 回だけ前順に辿り、座標・寸法がすべて「有限かつ絶対値 max_geometry_px 以内」
// であることを確かめる。違反は `LimitExceeded` で、**その箱（または断片）の入力位置**を付ける。
//
// なぜ段の出口で見るか: `%` の解決・座標の足し算・flex の比（`factor / factors.scaled` が
// inf/inf）は、どれも style では判定できない（A5: 包含ブロックが要る）。個々の計算の
// 中に検査を散らすのではなく、**段が出す値の性質**として 1 か所で保証する。こうしておくと、
// あとから計算を足したときに検査を書き忘れても不変条件が破れない。
//
// 費用は O(N)（paint の走査 1 回ぶん）。判定は比較だけで、NaN も inf も範囲外も
// 同じ 1 つの比較で落ちる（A9 の許可リスト内）。
Result<void> check_geometry(const BoxTree& tree, float max_geometry_px);

}  // namespace shashoku::layout
