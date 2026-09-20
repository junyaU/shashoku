#include "layout/inline_paragraph.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/utf8.hpp"

namespace shashoku::layout {
namespace {

// (b)(c) を組み立てる。段落 1 つにつき 1 インスタンス。
class ParagraphBuilder {
 public:
  ParagraphBuilder(PreparedParagraph& out, const std::vector<std::size_t>& ruby_at,
                   const std::vector<RubyGroup>& groups, LayoutEngine& engine)
      : out_(&out), ruby_at_(&ruby_at), groups_(&groups), engine_(&engine) {}

  void build();

 private:
  void build_ruby_item(std::size_t group_index);
  // [begin, end) を 1 回でシェーピングする。返すのは runs の添字。
  std::size_t shape_run(std::size_t begin, std::size_t end);

  PreparedParagraph* out_;
  const std::vector<std::size_t>* ruby_at_;
  const std::vector<RubyGroup>* groups_;
  LayoutEngine* engine_;
};

std::size_t ParagraphBuilder::shape_run(std::size_t begin, std::size_t end) {
  std::u32string text;
  text.reserve(end - begin);
  for (std::size_t i = begin; i < end; ++i) {
    text.push_back(out_->chars[i].cp);
  }
  const std::size_t shaping = out_->styles.shaping_index(out_->chars[begin].style);
  out_->runs.push_back(ShapedRun{.shaping = shaping,
                                 .shaped = engine_->shape(text, out_->styles.shaping_at(shaping))});
  return out_->runs.size() - 1;
}

void ParagraphBuilder::build_ruby_item(std::size_t group_index) {
  const RubyGroup& group = (*groups_)[group_index];
  RubyPiece piece;
  piece.base_style = out_->chars[group.base_begin].style;

  std::size_t i = group.base_begin;
  while (i < group.base_end) {
    const std::size_t style_id = out_->chars[i].style;
    std::size_t end = i;
    while (end < group.base_end && out_->chars[end].style == style_id) {
      ++end;
    }
    const std::size_t run = shape_run(i, end);
    float advance = 0;
    for (const text::ShapedCluster& cluster : out_->runs[run].shaped.clusters) {
      advance += cluster.advance + out_->letter_spacing(style_id);
    }
    piece.base.push_back(BaseSegment{.run = run,
                                     .glyph_begin = 0,
                                     .glyph_end = out_->runs[run].shaped.glyphs.size(),
                                     .style = style_id,
                                     .char_begin = i,
                                     .char_end = end,
                                     .advance = advance});
    piece.base_width += advance;
    i = end;
  }

  // ルビ文字。letter-spacing はルビには掛けない
  const std::size_t rt_style = group.rt_style;
  out_->runs.push_back(
      ShapedRun{.shaping = out_->styles.shaping_index(rt_style),
                .shaped = engine_->shape(group.rt_text, out_->styles.shaping(rt_style))});
  piece.rt_run = out_->runs.size() - 1;
  piece.rt_style = rt_style;
  for (const text::ShapedCluster& cluster : out_->runs.back().shaped.clusters) {
    piece.rt_width += cluster.advance;
  }
  piece.rt_text = encode_utf8(group.rt_text);
  piece.base_ascent = out_->metrics_of(piece.base_style).ascent;
  piece.base_font_size = out_->font_size(piece.base_style);
  piece.rt_ascent = out_->metrics_of(rt_style).ascent;
  piece.rt_descent = out_->metrics_of(rt_style).descent;
  piece.rt_font_size = out_->font_size(rt_style);

  const float advance = std::max(piece.base_width, piece.rt_width);
  const float em = piece.base_font_size;
  out_->items.push_back(linebreak::Item{.kind = linebreak::ItemKind::Atomic,
                                        .cp = out_->chars[group.base_begin].cp,
                                        .advance = advance,
                                        .em = em,
                                        .no_break_before = false});
  out_->sources.push_back(ItemSource{.run = kNone,
                                     .glyph_begin = 0,
                                     .glyph_end = 0,
                                     .char_begin = group.base_begin,
                                     .char_end = group.base_end,
                                     .style = piece.base_style,
                                     .image = kNone,
                                     .ruby = out_->rubies.size()});
  out_->rubies.push_back(std::move(piece));
}

void ParagraphBuilder::build() {
  std::size_t i = 0;
  while (i < out_->chars.size()) {
    if ((*ruby_at_)[i] != kNone) {
      const std::size_t group = (*ruby_at_)[i];
      build_ruby_item(group);
      i = (*groups_)[group].base_end;
      continue;
    }
    const FlatChar& flat = out_->chars[i];
    if (flat.kind != FlatChar::Kind::Text) {
      const bool image = flat.kind == FlatChar::Kind::Image;
      out_->items.push_back(linebreak::Item{
          .kind = image ? linebreak::ItemKind::Atomic : linebreak::ItemKind::ForcedBreak,
          .cp = flat.cp,
          .advance = image ? out_->images[flat.image].margin_inline() : 0,
          .em = out_->font_size(flat.style),
          .no_break_before = false});
      out_->sources.push_back(ItemSource{.run = kNone,
                                         .glyph_begin = 0,
                                         .glyph_end = 0,
                                         .char_begin = i,
                                         .char_end = i + 1,
                                         .style = flat.style,
                                         .image = flat.image,
                                         .ruby = kNone});
      ++i;
      continue;
    }
    // (b) スタイルが同じ連続区間を 1 回でシェーピングする（A6: 行ごとに測り直さない）
    const std::size_t style_id = flat.style;
    std::size_t end = i;
    while (end < out_->chars.size() && out_->chars[end].kind == FlatChar::Kind::Text &&
           out_->chars[end].style == style_id && (end == i || (*ruby_at_)[end] == kNone)) {
      ++end;
    }
    const std::size_t run = shape_run(i, end);
    // (c) クラスタ → Item。letter-spacing は送りに足す
    for (const text::ShapedCluster& cluster : out_->runs[run].shaped.clusters) {
      const std::size_t at = i + cluster.text_begin;
      out_->items.push_back(
          linebreak::Item{.kind = linebreak::ItemKind::Text,
                          .cp = at < end ? out_->chars[at].cp : 0,
                          .advance = cluster.advance + out_->letter_spacing(style_id),
                          .em = out_->font_size(style_id),
                          .no_break_before = false});
      out_->sources.push_back(ItemSource{.run = run,
                                         .glyph_begin = cluster.glyph_begin,
                                         .glyph_end = cluster.glyph_end,
                                         .char_begin = at,
                                         .char_end = i + cluster.text_end,
                                         .style = style_id,
                                         .image = kNone,
                                         .ruby = kNone});
    }
    i = end;
  }
}

}  // namespace

std::string PreparedParagraph::text_of(std::size_t char_begin, std::size_t char_end) const {
  std::string out;
  for (std::size_t i = char_begin; i < char_end && i < chars.size(); ++i) {
    append_utf8(out, chars[i].cp);
  }
  return out;
}

Result<PreparedParagraph> prepare_paragraph(const InlineInput& input, LayoutEngine& engine) {
  ++engine.counters().inline_prepare;
  Result<Collected> collected = collect_inline(input, engine);
  if (!collected) {
    return std::unexpected(collected.error());
  }

  PreparedParagraph out;
  out.chars = std::move(collected->chars);
  out.styles = std::move(collected->styles);
  out.scopes = std::move(collected->scopes);
  out.images = std::move(collected->images);

  // 支柱も文字と同じ表に入れる（行の高さの計算が 1 本道になる）。
  out.strut_style = out.styles.intern(*input.block_style, engine.map().direction());
  out.metrics.reserve(out.styles.shaping_count());
  for (std::size_t i = 0; i < out.styles.shaping_count(); ++i) {
    out.metrics.push_back(engine.metrics(out.styles.shaping_at(i)));
  }

  ParagraphBuilder(out, collected->ruby_at, collected->rubies, engine).build();
  return out;
}

}  // namespace shashoku::layout
