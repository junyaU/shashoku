#include "layout/layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "layout/inline_layout.hpp"
#include "layout/logical.hpp"

namespace shashoku::layout {
namespace {

using style::ComputedStyle;
using style::Dimension;
using style::StyledNode;

bool is_positive_finite(float value) {
  // NaN は比較が偽になるので、この 2 つで「正の有限値」を言い切れる（A9: libm を使わない）
  return value > 0 && value < std::numeric_limits<float>::infinity();
}

// 使用値（CSS の used value）。すべて論理方向（A1）。
struct BoxSizing {
  LogicalEdges<float> margin;
  LogicalEdges<float> padding;
  float border = 0;                         // 4 辺共通（A11）
  float content_inline_size = 0;            //
  std::optional<float> content_block_size;  // block 方向が auto なら nullopt
};

// 1 つのブロックの中身。無名ブロックは対応する StyledNode を持たないのでこの形で渡す。
struct BlockInput {
  std::string tag;
  const ComputedStyle* style = nullptr;  // 塗り・支柱・text-align の出どころ
  std::span<const StyledNode> children;
  SourceLocation location;
  bool anonymous = false;  // 無名ブロックは親の塗りを繰り返さない
};

// A10: 隣り合う兄弟ブロックのマージンの相殺（CSS 2.1 §8.3.1 の隣接兄弟の場合だけ）。
// 正どうしは大きい方、負を含むなら「正の最大 + 負の最小」。
float collapse_margins(float a, float b) {
  return std::max(std::max(a, 0.0F), std::max(b, 0.0F)) +
         std::min(std::min(a, 0.0F), std::min(b, 0.0F));
}

// A5: `%` と `auto` はここで初めて解ける。Auto の扱いは呼び出し側の責任。
float resolve_length(const Dimension& dimension, float percent_basis) {
  switch (dimension.kind) {
    case Dimension::Kind::Px:
      return dimension.value;
    case Dimension::Kind::Percent:
      return dimension.value / 100 * percent_basis;
    case Dimension::Kind::Auto:
      break;
  }
  return 0;
}

enum class ChildKind : std::uint8_t { Skip, Inline, Block };

ChildKind classify(const StyledNode& node) {
  if (node.type == StyledNode::Type::Text) {
    return ChildKind::Inline;
  }
  switch (node.style.display) {
    case style::Display::None:
      return ChildKind::Skip;
    case style::Display::Inline:
      return ChildKind::Inline;
    case style::Display::Block:
    case style::Display::Flex:
      break;
  }
  return ChildKind::Block;
}

bool is_ascii_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// 無名ブロックを作らなくてよい「空白だけのインラインの連続」か
// （CSS 2.1 §9.2.1.1: 空白しか含まない無名ブロックは生成しない）。
// NOLINTNEXTLINE(misc-no-recursion): インライン box の入れ子。深さは html パーサが 256 に制限する
bool is_blank(std::span<const StyledNode> nodes) {
  for (const StyledNode& node : nodes) {
    if (node.type == StyledNode::Type::Text) {
      for (const char byte : node.text) {
        if (!is_ascii_space(byte)) {
          return false;  // UTF-8 の継続バイトは 0x80 以上なので、バイトで見て安全
        }
      }
      continue;
    }
    if (node.style.display == style::Display::None) {
      continue;
    }
    if (node.style.display != style::Display::Inline) {
      return false;  // ブロック級が混ざっていたら空白ではない（エラーは IFC 側で出す）
    }
    if (node.tag == "br" || node.tag == "img" || node.tag == "ruby") {
      return false;
    }
    if (!is_blank(node.children)) {
      return false;
    }
  }
  return true;
}

// 第 1 段で未実装のブロック級の子。fail loudly（DESIGN.md §3-6）。
Result<void> check_supported(const StyledNode& node) {
  if (node.style.display == style::Display::Flex) {
    return fail(ErrorKind::UnsupportedLayout, "flex layout is not implemented yet", node.location);
  }
  if (node.tag == "img") {
    return fail(ErrorKind::UnsupportedLayout, "<img> layout is not implemented yet", node.location);
  }
  if (node.tag == "ruby" || node.tag == "rt") {
    return fail(ErrorKind::UnsupportedLayout, "<" + node.tag + "> layout is not implemented yet",
                node.location);
  }
  return {};
}

// A1: writing-mode は文書全体で 1 つ。② が保証するが、破れていたら静かに壊れるので確かめる。
// NOLINTNEXTLINE(misc-no-recursion): 木の走査。深さは html パーサが 256 に制限する
Result<void> check_writing_mode(const StyledNode& node, WritingMode mode) {
  if (node.style.writing_mode != mode) {
    return fail(ErrorKind::UnsupportedLayout,
                "writing-mode must be the same for the whole document", node.location);
  }
  for (const StyledNode& child : node.children) {
    if (const Result<void> result = check_writing_mode(child, mode); !result) {
      return result;
    }
  }
  return {};
}

class Layouter {
 public:
  Layouter(const Options& options, text::TextMeasurer& measurer, WritingMode mode)
      : options_(&options), measurer_(&measurer), map_(mode), mode_(mode) {}

  // CSS 2.1 §10.3.3（inline 方向）と §10.5（block 方向）の使用値を決める。
  [[nodiscard]] Result<BoxSizing> resolve_box(const ComputedStyle& style,
                                              float containing_inline_size,
                                              const SourceLocation& location) const;

  // block_start は border-box の block 開始位置（絶対）。マージンは呼び出し側が消費済み。
  Result<BlockBox> layout_block(const BlockInput& input, const BoxSizing& sizing,
                                float content_inline_start, float block_start);

 private:
  // 子ブロック 1 つぶんの、位置を決める前の状態。
  struct Pending {
    BlockInput input;
    BoxSizing sizing;
    float content_inline_start = 0;
  };
  struct Stacked {
    std::vector<BlockBox> boxes;
    float block_size = 0;
  };

  [[nodiscard]] Result<std::vector<Pending>> build_children(const BlockInput& input,
                                                            float content_inline_start,
                                                            float content_inline_size) const;
  Result<Stacked> stack_children(std::vector<Pending>& pending, float content_block_start);

  const Options* options_;
  text::TextMeasurer* measurer_;
  LogicalMap map_;
  WritingMode mode_;
};

Result<BoxSizing> Layouter::resolve_box(const ComputedStyle& style, float containing_inline_size,
                                        const SourceLocation& location) const {
  BoxSizing sizing;
  sizing.padding = map_.edges(style.padding);
  sizing.border = std::max(style.border_width, 0.0F);

  const LogicalEdges<Dimension> margin = map_.edges(style.margin);
  const float available = containing_inline_size;
  const float extra = 2 * sizing.border + sizing.padding.inline_start + sizing.padding.inline_end;
  const Dimension inline_size = map_.inline_size(style);
  const bool start_auto = margin.inline_start.is_auto();
  const bool end_auto = margin.inline_end.is_auto();
  float start = start_auto ? 0 : resolve_length(margin.inline_start, available);
  float end = end_auto ? 0 : resolve_length(margin.inline_end, available);

  if (inline_size.is_auto()) {
    // auto の幅は残り全部。auto のマージンは 0 になる
    sizing.content_inline_size = std::max(available - extra - start - end, 0.0F);
  } else {
    sizing.content_inline_size = std::max(resolve_length(inline_size, available), 0.0F);
    const float rest = available - extra - sizing.content_inline_size - start - end;
    if (start_auto && end_auto) {
      const float spare = std::max(rest, 0.0F);  // 余りが負なら中央寄せしない
      start = spare / 2;
      end = spare - start;
    } else if (start_auto) {
      start = std::max(rest, 0.0F);
    } else if (end_auto) {
      end = rest;
    } else {
      end += rest;  // 過制約: end 側の margin を無視して差を吸収する（CSS 2.1 §10.3.3）
    }
  }
  sizing.margin = LogicalEdges<float>{.inline_start = start,
                                      .inline_end = end,
                                      .block_start = resolve_length(margin.block_start, available),
                                      .block_end = resolve_length(margin.block_end, available)};

  const Dimension block_size = map_.block_size(style);
  if (!block_size.is_auto()) {
    if (block_size.kind == Dimension::Kind::Percent) {
      // ② が弾いている（computed_style.hpp）。ここに来たら値が対応外
      return fail(ErrorKind::UnsupportedValue, "percentage height is not supported", location);
    }
    sizing.content_block_size = std::max(block_size.value, 0.0F);
  }
  return sizing;
}

Result<std::vector<Layouter::Pending>> Layouter::build_children(const BlockInput& input,
                                                                float content_inline_start,
                                                                float content_inline_size) const {
  std::vector<Pending> pending;
  const std::span<const StyledNode> children = input.children;
  std::size_t i = 0;
  while (i < children.size()) {
    const ChildKind kind = classify(children[i]);
    if (kind == ChildKind::Skip) {
      ++i;
      continue;
    }
    if (kind == ChildKind::Inline) {
      // インラインの連続を無名ブロックで包む（ARCHITECTURE.md §3.8）
      std::size_t end = i;
      while (end < children.size() && classify(children[end]) != ChildKind::Block) {
        ++end;
      }
      const std::span<const StyledNode> run = children.subspan(i, end - i);
      i = end;
      if (is_blank(run)) {
        continue;
      }
      BoxSizing sizing;
      sizing.content_inline_size = content_inline_size;
      pending.push_back(Pending{.input = BlockInput{.tag = "#anonymous",
                                                    .style = input.style,
                                                    .children = run,
                                                    .location = run.front().location,
                                                    .anonymous = true},
                                .sizing = sizing,
                                .content_inline_start = content_inline_start});
      continue;
    }

    const StyledNode& child = children[i];
    ++i;
    if (const Result<void> supported = check_supported(child); !supported) {
      return std::unexpected(supported.error());
    }
    Result<BoxSizing> sizing = resolve_box(child.style, content_inline_size, child.location);
    if (!sizing) {
      return std::unexpected(sizing.error());
    }
    const float child_inline_start = content_inline_start + sizing->margin.inline_start +
                                     sizing->border + sizing->padding.inline_start;
    pending.push_back(Pending{.input = BlockInput{.tag = child.tag,
                                                  .style = &child.style,
                                                  .children = child.children,
                                                  .location = child.location,
                                                  .anonymous = false},
                              .sizing = *sizing,
                              .content_inline_start = child_inline_start});
  }
  return pending;
}

// NOLINTNEXTLINE(misc-no-recursion): 木の走査。深さは html パーサが 256 に制限する
Result<Layouter::Stacked> Layouter::stack_children(std::vector<Pending>& pending,
                                                   float content_block_start) {
  Stacked out;
  out.boxes.reserve(pending.size());
  float cursor = content_block_start;
  std::optional<float> previous_margin_end;
  for (Pending& child : pending) {
    const float gap = previous_margin_end
                          ? collapse_margins(*previous_margin_end, child.sizing.margin.block_start)
                          : child.sizing.margin.block_start;
    cursor += gap;
    Result<BlockBox> box =
        layout_block(child.input, child.sizing, child.content_inline_start, cursor);
    if (!box) {
      return std::unexpected(box.error());
    }
    cursor = box->rect.block_end();
    previous_margin_end = child.sizing.margin.block_end;
    out.boxes.push_back(std::move(*box));
  }
  if (previous_margin_end) {
    cursor += *previous_margin_end;  // 最後の子の margin も親の高さに入る（親子間の相殺はしない）
  }
  out.block_size = std::max(cursor - content_block_start, 0.0F);
  return out;
}

// NOLINTNEXTLINE(misc-no-recursion): 木の走査。深さは html パーサが 256 に制限する
Result<BlockBox> Layouter::layout_block(const BlockInput& input, const BoxSizing& sizing,
                                        float content_inline_start, float block_start) {
  const float content_block_start = block_start + sizing.border + sizing.padding.block_start;

  BlockBox box;
  box.tag = input.tag;
  if (!input.anonymous) {
    box.decoration = BoxDecoration{.background_color = input.style->background_color,
                                   .border_width = sizing.border,
                                   .border_color = input.style->border_color,
                                   .border_radius = std::max(input.style->border_radius, 0.0F)};
  }
  box.padding = sizing.padding;

  bool has_block = false;
  for (const StyledNode& child : input.children) {
    if (classify(child) == ChildKind::Block) {
      has_block = true;
      break;
    }
  }

  float content_block_size = 0;
  if (has_block) {
    Result<std::vector<Pending>> pending =
        build_children(input, content_inline_start, sizing.content_inline_size);
    if (!pending) {
      return std::unexpected(pending.error());
    }
    Result<Stacked> stacked = stack_children(*pending, content_block_start);
    if (!stacked) {
      return std::unexpected(stacked.error());
    }
    content_block_size = stacked->block_size;
    box.children = std::move(stacked->boxes);
  } else {
    // このブロックがインライン整形文脈を持つ
    const InlineInput inline_input{.children = input.children,
                                   .block_style = input.style,
                                   .content_inline_start = content_inline_start,
                                   .content_inline_size = sizing.content_inline_size,
                                   .content_block_start = content_block_start};
    Result<std::vector<LineBox>> lines = layout_inline(inline_input, *options_, *measurer_, mode_);
    if (!lines) {
      return std::unexpected(lines.error());
    }
    if (!lines->empty()) {
      content_block_size = lines->back().rect.block_end() - content_block_start;
    }
    box.children = std::move(*lines);
  }
  if (sizing.content_block_size) {
    content_block_size = *sizing.content_block_size;  // 固定高さ。内容がはみ出してもクリップしない
  }

  box.rect = LogicalRect{
      .inline_start = content_inline_start - sizing.border - sizing.padding.inline_start,
      .block_start = block_start,
      .inline_size = sizing.content_inline_size + 2 * sizing.border + sizing.padding.inline_start +
                     sizing.padding.inline_end,
      .block_size = content_block_size + 2 * sizing.border + sizing.padding.block_start +
                    sizing.padding.block_end};
  return box;
}

}  // namespace

Result<BoxTree> layout(const style::StyledNode& root, const Options& options,
                       text::TextMeasurer& measurer, [[maybe_unused]] const ImageLookup& images) {
  // images は第 2 段（<img>）で使う。第 1 段では <img> 自体がエラーなので引かない
  if (!is_positive_finite(options.viewport_width)) {
    return fail(ErrorKind::InvalidOption, "viewport width must be a positive finite number");
  }
  if (options.viewport_height && !is_positive_finite(*options.viewport_height)) {
    return fail(ErrorKind::InvalidOption, "viewport height must be a positive finite number");
  }

  const WritingMode mode = root.style.writing_mode;
  if (const Result<void> result = check_writing_mode(root, mode); !result) {
    return std::unexpected(result.error());
  }
  if (mode == WritingMode::VerticalRl) {
    return fail(ErrorKind::UnsupportedLayout, "vertical-rl writing mode is not implemented yet",
                root.location);
  }
  if (root.style.display == style::Display::Flex) {
    return fail(ErrorKind::UnsupportedLayout, "flex layout is not implemented yet", root.location);
  }

  // ルートの包含ブロックは viewport（A1: 横書きでは inline = viewport_width）
  const float viewport_inline_size = options.viewport_width;
  Layouter layouter(options, measurer, mode);
  Result<BoxSizing> sizing = layouter.resolve_box(root.style, viewport_inline_size, root.location);
  if (!sizing) {
    return std::unexpected(sizing.error());
  }
  const BlockInput input{.tag = root.tag.empty() ? "#root" : root.tag,
                         .style = &root.style,
                         .children = root.children,
                         .location = root.location,
                         .anonymous = false};
  const float content_inline_start =
      sizing->margin.inline_start + sizing->border + sizing->padding.inline_start;
  Result<BlockBox> box =
      layouter.layout_block(input, *sizing, content_inline_start, sizing->margin.block_start);
  if (!box) {
    return std::unexpected(box.error());
  }

  BoxTree tree;
  tree.writing_mode = mode;
  tree.viewport_width = options.viewport_width;
  tree.viewport_height = options.viewport_height;
  tree.root = std::move(*box);
  return tree;
}

}  // namespace shashoku::layout
