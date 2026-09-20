#include "layout/test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#include "layout/east_asian_width.hpp"

namespace shashoku::layout::test {
namespace {

bool is_combining(char32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) || cp == 0x3099 || cp == 0x309A ||
         (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

void collect_lines(const BlockBox& box, std::vector<const LineBox*>& out) {
  if (const std::vector<LineBox>* lines = box.lines()) {
    for (const LineBox& line : *lines) {
      out.push_back(&line);
    }
    return;
  }
  for (const BlockBox& child : *box.blocks()) {
    collect_lines(child, out);
  }
}

void collect_blocks(const BlockBox& box, std::vector<BlockRect>& out) {
  out.push_back(BlockRect{.tag = box.tag, .rect = box.rect});
  if (const std::vector<BlockBox>* blocks = box.blocks()) {
    for (const BlockBox& child : *blocks) {
      collect_blocks(child, out);
    }
  }
}

const BlockBox* find_block_in(const BlockBox& box, std::string_view tag, std::size_t& remaining) {
  if (box.tag == tag) {
    if (remaining == 0) {
      return &box;
    }
    --remaining;
  }
  if (const std::vector<BlockBox>* blocks = box.blocks()) {
    for (const BlockBox& child : *blocks) {
      if (const BlockBox* found = find_block_in(child, tag, remaining)) {
        return found;
      }
    }
  }
  return nullptr;
}

// 継承するプロパティ（computed_style.hpp の「継承する」群）だけを親から引き継ぐ。
style::ComputedStyle inherit(const style::ComputedStyle& parent) {
  style::ComputedStyle out;  // それ以外は初期値
  out.color = parent.color;
  out.font_size = parent.font_size;
  out.font_family = parent.font_family;
  out.font_weight = parent.font_weight;
  out.line_height = parent.line_height;
  out.letter_spacing = parent.letter_spacing;
  out.text_align = parent.text_align;
  out.line_break = parent.line_break;
  out.overflow_wrap = parent.overflow_wrap;
  out.writing_mode = parent.writing_mode;
  return out;
}

style::StyledNode build_node(const Tree& tree, const style::ComputedStyle& parent) {
  style::StyledNode node;
  node.type = tree.type;
  node.tag = tree.tag;
  node.text = tree.text;
  node.image_src = tree.image_src;
  node.attr_width = tree.attr_width;
  node.attr_height = tree.attr_height;
  node.style = inherit(parent);
  if (tree.style) {
    tree.style(node.style);
  }
  node.children.reserve(tree.children.size());
  for (const Tree& child : tree.children) {
    node.children.push_back(build_node(child, node.style));
  }
  return node;
}

}  // namespace

float fake_ascent(float font_size) { return font_size * kAscentRatio; }
float fake_descent(float font_size) { return font_size * kDescentRatio; }
float fake_line_height(float font_size) { return fake_ascent(font_size) + fake_descent(font_size); }

text::ShapedText FakeMeasurer::shape(std::u32string_view text, const text::TextStyle& style) {
  ++shape_calls;
  text::ShapedText out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char32_t cp = text[i];
    const bool combining = is_combining(cp) && !out.clusters.empty();
    float advance = is_fullwidth(cp) ? style.font_size : style.font_size / 2;
    if (combining) {
      advance = 0;  // 結合文字・異体字セレクタは直前のクラスタに吸収する（送り 0）
    }
    const bool sideways =
        style.direction == text::Direction::Vertical && sideways_latin_in_vertical && cp < 0x80;
    out.glyphs.push_back(
        text::ShapedGlyph{.font = fallback_chars.find(cp) == std::u32string::npos ? 0U : 1U,
                          .glyph_id = static_cast<GlyphId>(cp & 0xFFFFU),
                          .advance = advance,
                          .x_offset = 0,
                          .y_offset = 0,
                          .sideways = sideways});
    const auto glyph_end = static_cast<std::uint32_t>(out.glyphs.size());
    if (combining) {
      out.clusters.back().text_end = static_cast<std::uint32_t>(i + 1);
      out.clusters.back().glyph_end = glyph_end;
      continue;
    }
    out.clusters.push_back(text::ShapedCluster{.text_begin = static_cast<std::uint32_t>(i),
                                               .text_end = static_cast<std::uint32_t>(i + 1),
                                               .glyph_begin = glyph_end - 1,
                                               .glyph_end = glyph_end,
                                               .advance = advance,
                                               .missing = false});
  }
  return out;
}

text::FontMetrics FakeMeasurer::metrics(const text::TextStyle& style) {
  return text::FontMetrics{.ascent = style.font_size * ascent_ratio,
                           .descent = style.font_size * descent_ratio,
                           .line_gap = 0};
}

Tree text(std::string_view content) {
  Tree out;
  out.type = style::StyledNode::Type::Text;
  out.text = std::string(content);
  return out;
}

Tree block(std::vector<Tree> children, StyleFn style) {
  Tree out;
  out.tag = "div";
  out.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Block;
    if (style) {
      style(computed);
    }
  };
  out.children = std::move(children);
  return out;
}

Tree inline_box(std::vector<Tree> children, StyleFn style) {
  Tree out;
  out.tag = "span";
  out.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Inline;
    if (style) {
      style(computed);
    }
  };
  out.children = std::move(children);
  return out;
}

Tree br() {
  Tree out;
  out.tag = "br";
  return out;
}

Tree ruby(std::vector<Tree> children, StyleFn style) {
  Tree out;
  out.tag = "ruby";
  out.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Inline;
    if (style) {
      style(computed);
    }
  };
  out.children = std::move(children);
  return out;
}

Tree rt(std::string_view content, StyleFn style) {
  Tree out;
  out.tag = "rt";
  out.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Inline;
    computed.font_size = computed.font_size / 2;  // UA スタイル（ARCHITECTURE.md §3.7）
    if (style) {
      style(computed);
    }
  };
  out.children.push_back(text(content));
  return out;
}

Tree flex(std::vector<Tree> children, StyleFn style) {
  Tree out;
  out.tag = "div";
  out.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Flex;
    if (style) {
      style(computed);
    }
  };
  out.children = std::move(children);
  return out;
}

Tree img(std::string_view src, std::optional<float> attr_width, std::optional<float> attr_height,
         StyleFn style) {
  Tree out;
  out.tag = "img";
  out.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Inline;
    if (style) {
      style(computed);
    }
  };
  out.image_src = std::string(src);
  out.attr_width = attr_width;
  out.attr_height = attr_height;
  return out;
}

Tree element(std::string_view tag, style::Display display, std::vector<Tree> children,
             StyleFn style) {
  Tree out;
  out.tag = std::string(tag);
  out.style = [display, style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = display;
    if (style) {
      style(computed);
    }
  };
  out.children = std::move(children);
  return out;
}

style::StyledNode build(std::vector<Tree> children, StyleFn style) {
  Tree root;
  root.tag = "#root";
  root.style = [style = std::move(style)](style::ComputedStyle& computed) {
    computed.display = style::Display::Block;
    if (style) {
      style(computed);
    }
  };
  root.children = std::move(children);
  return build_node(root, style::ComputedStyle{});
}

style::StyledNode build_vertical(std::vector<Tree> children, StyleFn style) {
  return build(std::move(children), [style = std::move(style)](style::ComputedStyle& computed) {
    computed.writing_mode = WritingMode::VerticalRl;
    if (style) {
      style(computed);
    }
  });
}

Options vertical_options(float viewport_width, float viewport_height) {
  Options out = make_options(viewport_width);
  out.viewport_height = viewport_height;
  return out;
}

Options make_options(float viewport_width) {
  Options out;
  out.viewport_width = viewport_width;
  return out;
}

ImageLookup image_table(std::vector<ImageEntry> entries) {
  return [entries = std::move(entries)](std::string_view src) -> std::optional<ImageInfo> {
    for (const ImageEntry& entry : entries) {
      if (entry.src == src) {
        return ImageInfo{.id = entry.id, .width = entry.width, .height = entry.height};
      }
    }
    return std::nullopt;
  };
}

Result<BoxTree> run_layout(const style::StyledNode& root, const Options& options,
                           text::TextMeasurer& measurer) {
  const ImageLookup images = [](std::string_view) { return std::optional<ImageInfo>{}; };
  return layout(root, options, measurer, images);
}

Result<BoxTree> run_layout(const style::StyledNode& root, const Options& options,
                           text::TextMeasurer& measurer, const ImageLookup& images) {
  return layout(root, options, measurer, images);
}

Result<BoxTree> run_layout(const style::StyledNode& root, float viewport_width,
                           text::TextMeasurer& measurer) {
  return run_layout(root, make_options(viewport_width), measurer);
}

Result<BoxTree> run_layout(const style::StyledNode& root, float viewport_width,
                           text::TextMeasurer& measurer, const ImageLookup& images) {
  return layout(root, make_options(viewport_width), measurer, images);
}

std::vector<const LineBox*> all_lines(const BoxTree& tree) {
  std::vector<const LineBox*> out;
  collect_lines(tree.root, out);
  return out;
}

std::vector<std::string> line_texts(const BlockBox& box) {
  std::vector<const LineBox*> lines;
  collect_lines(box, lines);
  std::vector<std::string> out;
  out.reserve(lines.size());
  for (const LineBox* line : lines) {
    std::string joined;
    for (const TextFragment* fragment : text_fragments(*line)) {
      joined += fragment->text;
    }
    out.push_back(std::move(joined));
  }
  return out;
}

std::vector<std::string> line_texts(const BoxTree& tree) { return line_texts(tree.root); }

std::vector<BlockRect> block_rects(const BoxTree& tree) {
  std::vector<BlockRect> out;
  collect_blocks(tree.root, out);
  return out;
}

const BlockBox* find_block(const BoxTree& tree, std::string_view tag, std::size_t index) {
  std::size_t remaining = index;
  return find_block_in(tree.root, tag, remaining);
}

std::vector<float> glyph_positions(const LineBox& line) {
  std::vector<float> out;
  for (const TextFragment* fragment : text_fragments(line)) {
    for (const PositionedGlyph& glyph : fragment->glyphs) {
      out.push_back(glyph.inline_position);
    }
  }
  return out;
}

std::vector<const TextFragment*> text_fragments(const LineBox& line) {
  std::vector<const TextFragment*> out;
  for (const InlineFragment& fragment : line.fragments) {
    if (const auto* found = std::get_if<TextFragment>(&fragment)) {
      out.push_back(found);
    }
  }
  return out;
}

std::vector<const InlineBackground*> backgrounds(const LineBox& line) {
  std::vector<const InlineBackground*> out;
  for (const InlineFragment& fragment : line.fragments) {
    if (const auto* background = std::get_if<InlineBackground>(&fragment)) {
      out.push_back(background);
    }
  }
  return out;
}

std::vector<const ImageFragment*> image_fragments(const LineBox& line) {
  std::vector<const ImageFragment*> out;
  for (const InlineFragment& fragment : line.fragments) {
    if (const auto* image = std::get_if<ImageFragment>(&fragment)) {
      out.push_back(image);
    }
  }
  return out;
}

std::vector<const ImageFragment*> all_images(const BoxTree& tree) {
  std::vector<const ImageFragment*> out;
  for (const LineBox* line : all_lines(tree)) {
    for (const ImageFragment* image : image_fragments(*line)) {
      out.push_back(image);
    }
  }
  return out;
}

}  // namespace shashoku::layout::test
