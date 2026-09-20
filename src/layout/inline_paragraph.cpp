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
      : out_(&out),
        ruby_at_(&ruby_at),
        groups_(&groups),
        engine_(&engine),
        default_strictness_(engine.options().line_break.strictness) {}

  void build();

 private:
  void build_ruby_item(std::size_t group_index);
  // [begin, end) を 1 回でシェーピングする。返すのは runs の添字。
  std::size_t shape_run(std::size_t begin, std::size_t end);
  // (c) このアイテムに効く行分割ポリシー（issue #2 / A28）。`style` はアイテムの代表の文字
  // （クラスタ先頭 / <img> / <br> / ルビ組の親文字の先頭）が属する要素の計算値の添字。
  [[nodiscard]] linebreak::Item policy_of(linebreak::Item item, std::size_t style) const {
    const BreakingStyle& breaking = out_->styles.breaking(style);
    item.strictness = resolve_strictness(breaking.line_break, default_strictness_);
    item.break_anywhere = resolve_break_anywhere(breaking.overflow_wrap);
    return item;
  }

  PreparedParagraph* out_;
  const std::vector<std::size_t>* ruby_at_;
  const std::vector<RubyGroup>* groups_;
  LayoutEngine* engine_;
  // `line-break: auto` の解決先（A17）。段落ではなくエンジンの既定であることに注意:
  // 段落のブロックが strict でも、span が `auto` ならエンジンの既定に戻る（CSS どおり）。
  linebreak::Strictness default_strictness_;
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
    // 親文字も普通のテキストと同じで、シェーピングはシェーピング属性が同じ連続で 1 回（A27）
    const std::size_t shaping = out_->styles.shaping_index(out_->chars[i].style);
    std::size_t end = i;
    while (end < group.base_end && out_->styles.shaping_index(out_->chars[end].style) == shaping) {
      ++end;
    }
    const std::size_t run = shape_run(i, end);
    // 1 回のシェーピング結果を、装飾が変わる位置（クラスタ境界）で区間に切る
    const std::vector<text::ShapedCluster>& clusters = out_->runs[run].shaped.clusters;
    std::size_t first = 0;
    while (first < clusters.size()) {
      const std::size_t style_id = out_->chars[i + clusters[first].text_begin].style;
      const std::size_t visual = out_->visual_key(style_id);
      float advance = 0;
      std::size_t last = first;
      while (last < clusters.size() &&
             out_->visual_key(out_->chars[i + clusters[last].text_begin].style) == visual) {
        advance += clusters[last].advance + out_->letter_spacing(style_id);
        ++last;
      }
      piece.base.push_back(BaseSegment{.run = run,
                                       .glyph_begin = clusters[first].glyph_begin,
                                       .glyph_end = clusters[last - 1].glyph_end,
                                       .style = style_id,
                                       .char_begin = i + clusters[first].text_begin,
                                       .char_end = i + clusters[last - 1].text_end,
                                       .advance = advance});
      piece.base_width += advance;
      first = last;
    }
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
  // ルビ組は Atomic 1 個（組の内部には分割可能位置がない）。ポリシーは親文字の先頭の文字の
  // ものを使う（A28）。親文字の途中や <rt> の中の指定は、割る場所がないので効かない
  out_->items.push_back(policy_of(linebreak::Item{.kind = linebreak::ItemKind::Atomic,
                                                  .cp = out_->chars[group.base_begin].cp,
                                                  .advance = advance,
                                                  .em = em,
                                                  .no_break_before = false},
                                  piece.base_style));
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
      // <img> と <br> はその要素自身の計算値を使う（A28）
      const bool image = flat.kind == FlatChar::Kind::Image;
      out_->items.push_back(
          policy_of(linebreak::Item{.kind = image ? linebreak::ItemKind::Atomic
                                                  : linebreak::ItemKind::ForcedBreak,
                                    .cp = flat.cp,
                                    .advance = image ? out_->images[flat.image].margin_inline() : 0,
                                    .em = out_->font_size(flat.style),
                                    .no_break_before = false},
                    flat.style));
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
    // (b) **シェーピング属性**が同じ連続区間を 1 回でシェーピングする
    //     （A6: 行ごとに測り直さない / A27: 色・letter-spacing・line-height の境界では
    //      切らない。切るとその位置のカーニングと合字が消える。issue #8）
    const std::size_t shaping = out_->styles.shaping_index(flat.style);
    std::size_t end = i;
    while (end < out_->chars.size() && out_->chars[end].kind == FlatChar::Kind::Text &&
           out_->styles.shaping_index(out_->chars[end].style) == shaping &&
           (end == i || (*ruby_at_)[end] == kNone)) {
      ++end;
    }
    const std::size_t run = shape_run(i, end);
    // (c) クラスタ → Item。装飾・行高と行分割ポリシーは**クラスタ先頭の文字**のものを
    //     対応付ける（A27 / A28）。letter-spacing は送りに足す
    for (const text::ShapedCluster& cluster : out_->runs[run].shaped.clusters) {
      const std::size_t at = i + cluster.text_begin;
      const std::size_t style_id = at < end ? out_->chars[at].style : flat.style;
      out_->items.push_back(
          policy_of(linebreak::Item{.kind = linebreak::ItemKind::Text,
                                    .cp = at < end ? out_->chars[at].cp : 0,
                                    .advance = cluster.advance + out_->letter_spacing(style_id),
                                    .em = out_->font_size(style_id),
                                    .no_break_before = false},
                    style_id));
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
  engine.counters().style_probes += out.styles.probes();
  return out;
}

}  // namespace shashoku::layout
