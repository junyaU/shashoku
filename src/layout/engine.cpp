#include "layout/engine.hpp"

#include <algorithm>
#include <span>
#include <variant>
#include <vector>

namespace shashoku::layout {
namespace {

using style::Dimension;
using style::StyledNode;

bool is_ascii_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

void translate_line(LineBox& line, float delta_inline, float delta_block) {
  line.rect.inline_start += delta_inline;
  line.rect.block_start += delta_block;
  line.baseline += delta_block;
  for (InlineFragment& fragment : line.fragments) {
    if (auto* text = std::get_if<TextFragment>(&fragment)) {
      text->inline_start += delta_inline;
      text->baseline += delta_block;
      for (PositionedGlyph& glyph : text->glyphs) {
        glyph.inline_position += delta_inline;
      }
      continue;
    }
    if (auto* background = std::get_if<InlineBackground>(&fragment)) {
      background->rect.inline_start += delta_inline;
      background->rect.block_start += delta_block;
      continue;
    }
    auto& image = std::get<ImageFragment>(fragment);
    image.rect.inline_start += delta_inline;
    image.rect.block_start += delta_block;
    image.content_rect.inline_start += delta_inline;
    image.content_rect.block_start += delta_block;
  }
}

}  // namespace

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

// CSS 2.1 §9.2.1.1: 空白しか含まない無名ブロックは生成しない。
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

float collapse_margins(float a, float b) {
  return std::max(std::max(a, 0.0F), std::max(b, 0.0F)) +
         std::min(std::min(a, 0.0F), std::min(b, 0.0F));
}

BlockInput input_of(const StyledNode& node) {
  const bool image = node.tag == "img";
  return BlockInput{.tag = node.tag,
                    .style = &node.style,
                    .children = image ? std::span<const StyledNode>{} : node.children,
                    .location = node.location,
                    .anonymous = false,
                    .replaced = image ? &node : nullptr};
}

void translate(BlockBox& box, float delta_inline, float delta_block) {
  box.rect.inline_start += delta_inline;
  box.rect.block_start += delta_block;
  if (auto* lines = std::get_if<std::vector<LineBox>>(&box.children)) {
    for (LineBox& line : *lines) {
      translate_line(line, delta_inline, delta_block);
    }
    return;
  }
  for (BlockBox& child : std::get<std::vector<BlockBox>>(box.children)) {
    translate(child, delta_inline, delta_block);
  }
}

LogicalEdges<float> LayoutEngine::resolve_margin(const style::ComputedStyle& style,
                                                 float percent_basis) const {
  const LogicalEdges<Dimension> margin = map_.edges(style.margin);
  return LogicalEdges<float>{.inline_start = resolve_length(margin.inline_start, percent_basis),
                             .inline_end = resolve_length(margin.inline_end, percent_basis),
                             .block_start = resolve_length(margin.block_start, percent_basis),
                             .block_end = resolve_length(margin.block_end, percent_basis)};
}

LogicalEdges<bool> LayoutEngine::margin_is_auto(const style::ComputedStyle& style) const {
  const LogicalEdges<Dimension> margin = map_.edges(style.margin);
  return LogicalEdges<bool>{.inline_start = margin.inline_start.is_auto(),
                            .inline_end = margin.inline_end.is_auto(),
                            .block_start = margin.block_start.is_auto(),
                            .block_end = margin.block_end.is_auto()};
}

Result<BoxSizing> LayoutEngine::resolve_box(const style::ComputedStyle& style,
                                            float containing_inline_size,
                                            const SourceLocation& location,
                                            std::optional<float> override_inline,
                                            std::optional<float> override_block) const {
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

  if (!override_inline && inline_size.is_auto()) {
    // auto の幅は残り全部。auto のマージンは 0 になる
    sizing.content_inline_size = std::max(available - extra - start - end, 0.0F);
  } else {
    sizing.content_inline_size = override_inline
                                     ? std::max(*override_inline, 0.0F)
                                     : std::max(resolve_length(inline_size, available), 0.0F);
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

  if (override_block) {
    sizing.content_block_size = std::max(*override_block, 0.0F);
    return sizing;
  }
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

}  // namespace shashoku::layout
