#include "layout/layout.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "layout/engine.hpp"
#include "layout/flex_layout.hpp"
#include "layout/inline_layout.hpp"

namespace shashoku::layout {
namespace {

using style::StyledNode;

bool is_positive_finite(float value) {
  // NaN は比較が偽になるので、この 2 つで「正の有限値」を言い切れる（A9: libm を使わない）
  return value > 0 && value < std::numeric_limits<float>::infinity();
}

// ルビはインラインの仕組み（行の中の Atomic）なので、ブロック級の箱にはできない。
Result<void> check_supported(const StyledNode& node) {
  if (node.tag == "ruby" || node.tag == "rt") {
    return fail(ErrorKind::UnsupportedLayout,
                "<" + node.tag + "> is not supported as a block-level box", node.location);
  }
  return {};
}

// A1: writing-mode は文書全体で 1 つ。② が保証するが、破れていたら静かに壊れるので確かめる。
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

Result<std::vector<Pending>> build_children(LayoutEngine& engine, const BlockInput& input,
                                            float content_inline_start, float content_inline_size) {
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
    // 置換要素（<img>）は width / height プロパティの外でサイズが決まる
    std::optional<float> override_inline;
    std::optional<float> override_block;
    if (child.tag == "img") {
      Result<ResolvedImage> image = engine.resolve_image(child, content_inline_size);
      if (!image) {
        return std::unexpected(image.error());
      }
      override_inline = image->inline_size;
      override_block = image->block_size;
    }
    Result<BoxSizing> sizing = engine.resolve_box(child.style, content_inline_size, child.location,
                                                  override_inline, override_block);
    if (!sizing) {
      return std::unexpected(sizing.error());
    }
    const float child_inline_start = content_inline_start + sizing->margin.inline_start +
                                     sizing->border + sizing->padding.inline_start;
    pending.push_back(Pending{
        .input = input_of(child), .sizing = *sizing, .content_inline_start = child_inline_start});
  }
  return pending;
}

Result<Stacked> stack_children(LayoutEngine& engine, std::vector<Pending>& pending,
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
        engine.layout_block(child.input, child.sizing, child.content_inline_start, cursor);
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

}  // namespace

Result<BlockBox> LayoutEngine::layout_block(const BlockInput& input, const BoxSizing& sizing,
                                            float content_inline_start, float block_start) {
  ++counters().layout_block;
  if (input.replaced != nullptr) {
    return layout_image_box(input, sizing, content_inline_start, block_start);
  }
  // 無名ボックス（テキストの連続を包んだもの）は自分のスタイルを持たない。display は
  // 親から借りているだけなので、flex コンテナとして扱ってはいけない（CSS の無名ボックスは
  // 常にブロックコンテナ）。
  if (!input.anonymous && input.style->display == style::Display::Flex) {
    return layout_flex(*this, input, sizing, content_inline_start, block_start);
  }

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
        build_children(*this, input, content_inline_start, sizing.content_inline_size);
    if (!pending) {
      return std::unexpected(pending.error());
    }
    Result<Stacked> stacked = stack_children(*this, *pending, content_block_start);
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
    Result<std::vector<LineBox>> lines = layout_inline(inline_input, *this);
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

Result<BoxTree> layout(const style::StyledNode& root, const Options& options,
                       text::TextMeasurer& measurer, const ImageLookup& images,
                       Counters* counters) {
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
  if (mode == WritingMode::VerticalRl && !options.viewport_height) {
    // 縦書きでは字送りが縦なので、行の長さ（= 利用可能な inline サイズ）が viewport の
    // 高さで決まる。高さが未指定だと行を割る幅が決まらない
    return fail(ErrorKind::InvalidOption,
                "vertical-rl writing mode needs an explicit viewport height (the inline axis "
                "runs top to bottom, so the line length comes from the height)");
  }

  // ルートの包含ブロックは viewport（A1: 横書きは inline = 幅、縦書きは inline = 高さ）
  const float viewport_inline_size =
      mode == WritingMode::VerticalRl ? *options.viewport_height : options.viewport_width;
  Counters discarded;  // 呼び出し側が数えないときの捨て場（null 判定を 1 か所で済ませる）
  LayoutEngine engine(options, measurer, images, mode, counters != nullptr ? *counters : discarded);
  Result<BoxSizing> sizing = engine.resolve_box(root.style, viewport_inline_size, root.location);
  if (!sizing) {
    return std::unexpected(sizing.error());
  }
  BlockInput input = input_of(root);
  if (input.tag.empty()) {
    input.tag = "#root";
  }
  const float content_inline_start =
      sizing->margin.inline_start + sizing->border + sizing->padding.inline_start;
  Result<BlockBox> box =
      engine.layout_block(input, *sizing, content_inline_start, sizing->margin.block_start);
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
