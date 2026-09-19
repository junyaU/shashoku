#pragma once

#include <span>
#include <vector>

#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/layout.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::layout {

// インライン整形文脈（IFC）1 つぶんの入力。手順は ARCHITECTURE.md §3.8 の (a)〜(e)。
struct InlineInput {
  // IFC に属するインライン級の子。無名ブロックのときは親の子の連続した一部分。
  std::span<const style::StyledNode> children;
  // IFC を持つブロックのスタイル。支柱（strut）・text-align・line-break の出どころ。
  // 無名ブロックのときは親ブロックのスタイル（CSS の無名ボックスの継承）。
  const style::ComputedStyle* block_style = nullptr;
  float content_inline_start = 0;  // 絶対（論理座標）
  float content_inline_size = 0;   // 行分割の利用可能幅
  float content_block_start = 0;   // 絶対（論理座標）
};

// 行ボックスの列を block 方向に積んで返す。内容が空（子なし / 空白だけ）なら空の列。
Result<std::vector<LineBox>> layout_inline(const InlineInput& input, const Options& options,
                                           text::TextMeasurer& measurer, WritingMode mode);

}  // namespace shashoku::layout
