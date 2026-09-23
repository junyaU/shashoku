#pragma once

#include <vector>

#include "layout/box_tree.hpp"

// ③ レイアウトの出口の検査その 2: 紙面からのはみ出し（ARCHITECTURE.md A46 / §3.8）。
namespace shashoku::layout {

// 丸めとして無視する差（CSS px）。これ**を超えて**出ていれば 1 件になる。
// 半 px は縁のアンチエイリアス 1 本ぶんで、目で見て「切れている」とは言えない。
inline constexpr float kOverflowTolerancePx = 0.5F;

// BoxTree を 1 回だけ前順に辿り、出力の矩形（幅 `viewport_width`、高さは `viewport_height` が
// あるときだけ）の外に出ている箱を集める。木は書き換えない（絵には影響しない）。
//
// なぜ段の出口で見るか: 「切れているかどうか」は組み上がった座標を紙面と突き合わせて初めて
// 分かる（A46 の検証で、固定高さからあふれた PNG が警告なし・exit 0 で出ていた）。
// 個々の配置の中で判定すると、あとから配置を足したときに見落とす。
//
// 判定の詳細（対象の箱・最も外側だけ数える理由・単位）は box_tree.hpp の `ContentOverflow`。
// 費用は O(N)（paint の走査 1 回ぶん）で、比較しかしない（A9 の許可リスト内）。
[[nodiscard]] std::vector<ContentOverflow> collect_overflows(const BoxTree& tree);

}  // namespace shashoku::layout
