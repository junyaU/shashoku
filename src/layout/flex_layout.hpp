#pragma once

#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/engine.hpp"

namespace shashoku::layout {

// 単一行の flexbox（ARCHITECTURE.md §3.8 / CSS Flexbox Level 1 §9）。
// flex-wrap は対応外なので、アイテムは必ず 1 行に並ぶ（収まらなければはみ出す）。
Result<BlockBox> layout_flex(LayoutEngine& engine, const BlockInput& input, const BoxSizing& sizing,
                             float content_inline_start, float block_start);

// flex コンテナの content-box の固有 inline サイズ。
// row は「アイテムの和 + gap」、column は「アイテムの最大」。
Result<Intrinsic> flex_intrinsic(LayoutEngine& engine, const BlockInput& input,
                                 float percent_basis);

}  // namespace shashoku::layout
