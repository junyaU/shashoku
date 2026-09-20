#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>

#include "layout/engine.hpp"
#include "layout/flex_layout.hpp"
#include "layout/inline_layout.hpp"

// 固有寸法（min-content / max-content の inline サイズ）。
// flex の flex-basis: auto・自動最小サイズ・shrink-to-fit に要る。
//
// 限界（意図した割り切り）:
//   * block 方向の固有寸法は出さない（要るのは inline 方向だけ）
//   * ここでの計測は本番のレイアウトとは別に shape() を呼ぶ。A6「段落全体で 1 回」は
//     1 回のレイアウトの中での話で、固有寸法を測るための計測は別勘定にしている
namespace shashoku::layout {
namespace {

using style::Dimension;
using style::StyledNode;

// この BlockInput はインライン整形文脈を持つか（= ブロック級の子がいない）。
bool has_block_child(const BlockInput& input) {
  return std::ranges::any_of(
      input.children, [](const StyledNode& child) { return classify(child) == ChildKind::Block; });
}

}  // namespace

Result<Intrinsic> LayoutEngine::content_intrinsic(const BlockInput& input, float percent_basis) {
  ++counters().content_intrinsic;
  if (input.replaced != nullptr) {
    const Result<ResolvedImage> image = resolve_image(*input.replaced, percent_basis);
    if (!image) {
      return std::unexpected(image.error());
    }
    return Intrinsic{.min_content = image->inline_size, .max_content = image->inline_size};
  }
  if (!input.anonymous && input.style->display == style::Display::Flex) {
    return flex_intrinsic(*this, input, percent_basis);  // 無名ボックスは常にブロックコンテナ
  }

  if (!has_block_child(input)) {
    const InlineInput inline_input{.children = input.children,
                                   .block_style = input.style,
                                   .content_inline_start = 0,
                                   .content_inline_size = percent_basis,
                                   .content_block_start = 0};
    return inline_intrinsic(inline_input, *this);
  }

  // ブロックの並び。inline 方向は積み上げないので、子の中の最大がそのまま親の固有寸法。
  Intrinsic out;
  const std::span<const StyledNode> children = input.children;
  std::size_t i = 0;
  while (i < children.size()) {
    const ChildKind kind = classify(children[i]);
    if (kind == ChildKind::Skip) {
      ++i;
      continue;
    }
    Result<Intrinsic> child{Intrinsic{}};
    if (kind == ChildKind::Inline) {
      std::size_t end = i;
      while (end < children.size() && classify(children[end]) != ChildKind::Block) {
        ++end;
      }
      const std::span<const StyledNode> run = children.subspan(i, end - i);
      i = end;
      if (is_blank(run)) {
        continue;
      }
      const InlineInput inline_input{.children = run,
                                     .block_style = input.style,
                                     .content_inline_start = 0,
                                     .content_inline_size = percent_basis,
                                     .content_block_start = 0};
      child = inline_intrinsic(inline_input, *this);
    } else {
      child = outer_intrinsic(children[i], percent_basis);
      ++i;
    }
    if (!child) {
      return std::unexpected(child.error());
    }
    out.min_content = std::max(out.min_content, child->min_content);
    out.max_content = std::max(out.max_content, child->max_content);
  }
  return out;
}

Result<Intrinsic> LayoutEngine::outer_intrinsic(const StyledNode& node, float percent_basis) {
  const style::ComputedStyle& style = node.style;
  const LogicalEdges<float> padding = map_.edges(style.padding);
  const float border = std::max(style.border_width, 0.0F);
  const LogicalEdges<float> margin = resolve_margin(style, percent_basis);
  const float extra = (2 * border) + padding.inline_start + padding.inline_end +
                      margin.inline_start + margin.inline_end;

  Intrinsic inner;
  const Dimension width = map_.inline_size(style);
  // <img> は width / 属性 / 固有寸法の優先順位が別にあるので content_intrinsic に任せる
  if (node.tag != "img" && !width.is_auto()) {
    const float value = std::max(resolve_length(width, percent_basis), 0.0F);
    inner = Intrinsic{.min_content = value, .max_content = value};
  } else {
    Result<Intrinsic> content = content_intrinsic(input_of(node), percent_basis);
    if (!content) {
      return std::unexpected(content.error());
    }
    inner = *content;
  }
  return Intrinsic{.min_content = inner.min_content + extra,
                   .max_content = inner.max_content + extra};
}

}  // namespace shashoku::layout
