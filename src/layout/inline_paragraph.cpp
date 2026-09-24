#include "layout/inline_paragraph.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/utf8.hpp"
#include "layout/layout_cache.hpp"

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

  [[nodiscard]] Result<void> build();

 private:
  [[nodiscard]] Result<void> build_ruby_item(std::size_t group_index);
  void build_atomic_item(std::size_t at);
  // (c'') ルビの掛け（JLREQ 3.3.8）。隣のアイテムを見るので、アイテムを全部作ってから決める。
  void resolve_ruby_overhang();
  // (c') 空のインラインボックスを行に割り当てる（#23 / #30）。行の幅に依らないのでここで決める。
  void resolve_empty_boxes();
  [[nodiscard]] Result<std::size_t> build_text_items(std::size_t begin);
  // [begin, end) を 1 回でシェーピングする。返すのは runs の添字。
  [[nodiscard]] Result<std::size_t> shape_run(std::size_t begin, std::size_t end);
  // (b') 豆腐を拾う（A31 / issue #9）。Shaper は何も溜めないので、どの文字が豆腐かは
  // クラスタの missing から読み、位置は文字ごとの属性の表（A27 の位置の層）から引く。
  void record_missing(const text::ShapedText& shaped, std::size_t char_begin);
  // <rt> のルビ文字は chars に無い（RubyGroup::rt_text の中）ので別に拾う。
  void record_missing_ruby(const text::ShapedText& shaped, const std::u32string& rt_text,
                           std::size_t rt_style);
  // (b'') font-family の要求を満たせなかったことを拾う（A57）。豆腐と同じで Shaper は
  // 何も溜めないので、事実（ShapedText::family_request_unmet）を見て並びと位置を記録する。
  void record_font_fallback(const text::ShapedText& shaped, std::size_t style);
  // (c) このアイテムに効く行分割ポリシー（issue #2 / A28）。`style` はアイテムの代表の文字
  // （クラスタ先頭 / <img> / <br> / ルビ組の親文字の先頭）が属する要素の計算値の添字。
  [[nodiscard]] linebreak::Item policy_of(linebreak::Item item, std::size_t style) const {
    const BreakingStyle& breaking = out_->styles.breaking(style);
    item.strictness = resolve_strictness(breaking.line_break, default_strictness_);
    item.wrap = resolve_wrap(breaking.overflow_wrap);
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

Result<std::size_t> ParagraphBuilder::shape_run(std::size_t begin, std::size_t end) {
  std::u32string text;
  text.reserve(end - begin);
  for (std::size_t i = begin; i < end; ++i) {
    text.push_back(out_->chars[i].cp);
  }
  const std::size_t shaping = out_->styles.shaping_index(out_->chars[begin].style);
  Result<text::ShapedText> shaped = engine_->shape(text, out_->styles.shaping_at(shaping));
  if (!shaped) {
    return std::unexpected(shaped.error());
  }
  out_->runs.push_back(ShapedRun{.shaping = shaping, .shaped = std::move(*shaped)});
  record_missing(out_->runs.back().shaped, begin);
  record_font_fallback(out_->runs.back().shaped, out_->chars[begin].style);
  return out_->runs.size() - 1;
}

// 豆腐のクラスタは「先頭の文字 1 個」を代表にする。Shaper は結合文字・異体字セレクタ・
// ZWJ の後ろを先頭の文字と同じクラスタにまとめる（A2）ので、代表はその基底文字になる。
void ParagraphBuilder::record_missing(const text::ShapedText& shaped, std::size_t char_begin) {
  for (const text::ShapedCluster& cluster : shaped.clusters) {
    if (!cluster.missing) {
      continue;
    }
    const std::size_t at = char_begin + cluster.text_begin;
    if (at >= out_->chars.size()) {
      continue;  // 起きないはずだが、クラスタの範囲を信用して添字を外に出さない
    }
    engine_->record_missing_glyph(out_->chars[at].cp, out_->styles.location(out_->chars[at].style),
                                  cluster.missing_reason);
  }
}

void ParagraphBuilder::record_missing_ruby(const text::ShapedText& shaped,
                                           const std::u32string& rt_text, std::size_t rt_style) {
  const SourceLocation& location = out_->styles.location(rt_style);
  for (const text::ShapedCluster& cluster : shaped.clusters) {
    if (cluster.missing && cluster.text_begin < rt_text.size()) {
      engine_->record_missing_glyph(rt_text[cluster.text_begin], location, cluster.missing_reason);
    }
  }
}

// 記録は `font-family` の並びごとに 1 件（位置は入力順で最初のテキストノードの先頭）なので、
// 重複除去も位置の取り方も LayoutEngine 側に置いてある（A57）。
void ParagraphBuilder::record_font_fallback(const text::ShapedText& shaped, std::size_t style) {
  if (!shaped.family_request_unmet) {
    return;
  }
  engine_->record_font_fallback(out_->styles.shaping(style).font_family,
                                out_->styles.location(style));
}

Result<void> ParagraphBuilder::build_ruby_item(std::size_t group_index) {
  const RubyGroup& group = (*groups_)[group_index];
  RubyPiece piece;
  piece.base_style = out_->chars[group.base_begin].style;

  piece.base_begin = out_->ruby_clusters.size();
  std::size_t i = group.base_begin;
  while (i < group.base_end) {
    // 親文字も普通のテキストと同じで、シェーピングはシェーピング属性が同じ連続で 1 回（A27）
    const std::size_t shaping = out_->styles.shaping_index(out_->chars[i].style);
    std::size_t end = i;
    while (end < group.base_end && out_->styles.shaping_index(out_->chars[end].style) == shaping) {
      ++end;
    }
    const Result<std::size_t> shaped = shape_run(i, end);
    if (!shaped) {
      return std::unexpected(shaped.error());
    }
    const std::size_t run = *shaped;
    // 親文字は**クラスタごと**に記録する（#16）。通常テキストのアイテムと同じ中身なので、
    // 配置も同じ道を通り、letter-spacing が計測と配置で食い違わない。装飾の境界で
    // 区間にまとめないのは、断片を切るのは FragmentWriter の仕事だから（A27）
    for (const text::ShapedCluster& cluster : out_->runs[run].shaped.clusters) {
      const std::size_t at = i + cluster.text_begin;
      const std::size_t style_id = at < end ? out_->chars[at].style : out_->chars[i].style;
      const float advance = cluster.advance + out_->letter_spacing(style_id);
      out_->ruby_clusters.push_back(
          RubyCluster{.source = ItemSource{.run = run,
                                           .glyph_begin = cluster.glyph_begin,
                                           .glyph_end = cluster.glyph_end,
                                           .char_begin = at,
                                           .char_end = i + cluster.text_end,
                                           .style = style_id,
                                           .image = kNone,
                                           .ruby = kNone},
                      .advance = advance});
      piece.base_width += advance;
      // 幾何は全クラスタの最大から（#17）。代表の文字 1 つでは 2 文字目以降が落ちる
      piece.max_base_ascent = std::max(piece.max_base_ascent, out_->metrics_of(style_id).ascent);
      piece.max_base_font_size = std::max(piece.max_base_font_size, out_->font_size(style_id));
    }
    i = end;
  }
  piece.base_end = out_->ruby_clusters.size();

  // ルビ文字。letter-spacing はルビには掛けない
  const std::size_t rt_style = group.rt_style;
  Result<text::ShapedText> rt_shaped =
      engine_->shape(group.rt_text, out_->styles.shaping(rt_style));
  if (!rt_shaped) {
    return std::unexpected(rt_shaped.error());
  }
  out_->runs.push_back(
      ShapedRun{.shaping = out_->styles.shaping_index(rt_style), .shaped = std::move(*rt_shaped)});
  record_missing_ruby(out_->runs.back().shaped, group.rt_text, rt_style);
  record_font_fallback(out_->runs.back().shaped, rt_style);
  piece.rt_run = out_->runs.size() - 1;
  piece.rt_style = rt_style;
  for (const text::ShapedCluster& cluster : out_->runs.back().shaped.clusters) {
    piece.rt_width += cluster.advance;
  }
  piece.rt_text = encode_utf8(group.rt_text);
  // クラスタが 1 つも無い（すべて送り 0 に吸収された）ときの下限として代表の文字も見る
  piece.max_base_ascent =
      std::max(piece.max_base_ascent, out_->metrics_of(piece.base_style).ascent);
  piece.max_base_font_size = std::max(piece.max_base_font_size, out_->font_size(piece.base_style));
  piece.rt_ascent = out_->metrics_of(rt_style).ascent;
  piece.rt_descent = out_->metrics_of(rt_style).descent;
  piece.rt_font_size = out_->font_size(rt_style);

  const float advance = std::max(piece.base_width, piece.rt_width);
  // em は行分割器の約物のアキに使う値（幾何ではない）なので、代表の文字のまま（A28）
  const float em = out_->font_size(piece.base_style);
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
  return {};
}

// <img> と <br> は 1 文字で 1 アイテム。ポリシーはその要素自身の計算値（A28）。
void ParagraphBuilder::build_atomic_item(std::size_t at) {
  const FlatChar& flat = out_->chars[at];
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
                                     .char_begin = at,
                                     .char_end = at + 1,
                                     .style = flat.style,
                                     .image = flat.image,
                                     .ruby = kNone});
}

// (b) **シェーピング属性**が同じ連続区間を 1 回でシェーピングし、(c) クラスタごとに
// Item を作る（A6: 行ごとに測り直さない / A27: 色・letter-spacing・line-height の境界では
// 切らない。切るとその位置のカーニングと合字が消える。issue #8）。返すのは区間の終わり。
Result<std::size_t> ParagraphBuilder::build_text_items(std::size_t begin) {
  const std::size_t shaping = out_->styles.shaping_index(out_->chars[begin].style);
  std::size_t end = begin;
  while (end < out_->chars.size() && out_->chars[end].kind == FlatChar::Kind::Text &&
         out_->styles.shaping_index(out_->chars[end].style) == shaping &&
         (end == begin || (*ruby_at_)[end] == kNone)) {
    ++end;
  }
  const Result<std::size_t> shaped = shape_run(begin, end);
  if (!shaped) {
    return std::unexpected(shaped.error());
  }
  // 装飾・行高と行分割ポリシーは**クラスタ先頭の文字**のものを対応付ける（A27 / A28）。
  // letter-spacing は送りに足す
  for (const text::ShapedCluster& cluster : out_->runs[*shaped].shaped.clusters) {
    const std::size_t at = begin + cluster.text_begin;
    const std::size_t style_id = at < end ? out_->chars[at].style : out_->chars[begin].style;
    out_->items.push_back(
        policy_of(linebreak::Item{.kind = linebreak::ItemKind::Text,
                                  .cp = at < end ? out_->chars[at].cp : 0,
                                  .advance = cluster.advance + out_->letter_spacing(style_id),
                                  .em = out_->font_size(style_id),
                                  .no_break_before = false},
                  style_id));
    out_->sources.push_back(ItemSource{.run = *shaped,
                                       .glyph_begin = cluster.glyph_begin,
                                       .glyph_end = cluster.glyph_end,
                                       .char_begin = at,
                                       .char_end = begin + cluster.text_end,
                                       .style = style_id,
                                       .image = kNone,
                                       .ruby = kNone});
  }
  return end;
}

// JLREQ 3.3.8: ルビを掛けてよい相手は**平仮名・片仮名**（長音・小書きの仮名を含む。
// cl-15 / cl-16 / cl-10 / cl-11）だけ。漢字等（cl-19）・欧文・数字・約物には掛けない。
// <img> やほかのルビ組（Atomic）・<br> にも掛けない。
// 半角片仮名（U+FF66〜）と仮名の繰返し記号（ゝゞヽヾ）は対象外にしてある: 和文の本文では
// 使わないうえ、掛けてよいかの根拠が JLREQ に無い。
bool accepts_ruby_overhang(const linebreak::Item& item) {
  if (item.kind != linebreak::ItemKind::Text) {
    return false;
  }
  const char32_t cp = item.cp;
  return (cp >= U'ぁ' && cp <= U'ゖ') ||  // 平仮名（ぁ〜ゖ。小書きを含む）
         (cp >= U'ァ' && cp <= U'ヺ') ||  // 片仮名（ァ〜ヺ。小書きを含む）
         cp == U'ー';                     // 長音符
}

// (c'') ルビの掛け（JLREQ 3.3.8）。ルビが親文字より長い組は、はみ出した量 E を前後の仮名に
// 掛けてよい。掛ける量の上限は**ルビ文字サイズの全角**（`<rt>` の 1em）で、前後の両方に
// 掛けられるなら 1:1 に、片側だけならその側に寄せる。**行頭・行末で落とすのは行分割器の仕事**
// （どの行に来るかはここでは決まらない。line_breaker.hpp の Item::overhang_*）。
// 掛けきれずに残った余りは、配置のときに (a) の配分（JLREQ 3.3.6）で組の内部に配る。
void ParagraphBuilder::resolve_ruby_overhang() {
  for (std::size_t i = 0; i < out_->items.size(); ++i) {
    const std::size_t ruby = out_->sources[i].ruby;
    if (ruby == kNone) {
      continue;
    }
    const RubyPiece& piece = out_->rubies[ruby];
    const float excess = piece.rt_width - piece.base_width;
    if (excess <= 0) {
      continue;  // ルビが親文字からはみ出していない組は掛けない
    }
    const float limit = piece.rt_font_size;
    const bool before = i > 0 && accepts_ruby_overhang(out_->items[i - 1]);
    const bool after = i + 1 < out_->items.size() && accepts_ruby_overhang(out_->items[i + 1]);
    if (before && after) {
      out_->items[i].overhang_before = std::min(excess / 2, limit);
      out_->items[i].overhang_after = out_->items[i].overhang_before;
    } else if (before) {
      out_->items[i].overhang_before = std::min(excess, limit);
    } else if (after) {
      out_->items[i].overhang_after = std::min(excess, limit);
    }
  }
}

// (c') 空のインラインボックス（文字を 1 つも持たない <span> など）が参加する行を決める。
// 規則（issue #23 / #30。CSS 2.1 §10.8 / §10.8.1 と Chrome の実測に一致する）:
//   * `char_pos` を**範囲に含む**アイテムがあれば、そのアイテムの行（#30）。
//     範囲は [char_begin, char_end) で、**ルビ組だけは両端を含む**（下の注）
//   * 無ければ `char_pos` 以降の最初のアイテムの行
//   * それも無ければ直前（= 最後）のアイテムの行
//   * ただし最後のアイテムが強制改行なら**どの行にも参加しない**
//     （`A<br><span></span>` の空 span は次の行を作らないので、参加する行が無い）
//
// 1 つ目が要るのは、ルビ組が**複数の文字を 1 アイテム**にまとめるから（A28）。
// #23 の規則（`char_pos` 以降の最初のアイテム）だけだと、組の内部の位置は組を飛び越えて
// 次のアイテムに付き、ルビの行ではなく**後続の行**が高くなっていた（issue #30）。
// 通常のテキストは 1 クラスタ = 1 アイテムなので、範囲で探しても #23 と同じ行になる。
//
// 注: ルビ組の末尾（`char_pos == char_end`）を組の側に入れるのは、`<ruby>AB<span></span><rt>`
// （組の内部）と `</ruby><span></span>`（組の直後）が (a) の出力では同じ `char_pos` になり、
// 区別できないため。組の内部の指定が黙って次の行に効く方が壊れ方として悪いので、
// 組に寄せた（A42 の追記）。
//
// ここで決めるのは、**行の幅に依らない**から（= メモした準備済み段落で使い回せる。A29）。
// アイテムの char_begin は狭義単調増加なので二分探索でき、`char_pos` を範囲に含みうるのは
// 「char_begin が char_pos 以下の最後の 2 つ」だけなので、後退は定数回で済む。
void ParagraphBuilder::resolve_empty_boxes() {
  const std::vector<ItemSource>& sources = out_->sources;
  for (EmptyInlineBox& box : out_->empty_boxes) {
    // char_begin が char_pos より大きい最初のアイテム（= 範囲の候補の 1 つ後ろ）
    const auto above = std::ranges::upper_bound(sources, box.char_pos, {}, &ItemSource::char_begin);
    const std::size_t at = static_cast<std::size_t>(above - sources.begin());
    box.item = kNone;
    // ルビ組は両端を含む範囲で先に見る（組の末尾は次のアイテムの先頭と同じ位置になる）
    for (std::size_t back = 1; back <= 2 && back <= at; ++back) {
      const ItemSource& source = sources[at - back];
      if (source.ruby != kNone && box.char_pos >= source.char_begin &&
          box.char_pos <= source.char_end) {
        box.item = at - back;
        break;
      }
    }
    if (box.item == kNone && at > 0 && box.char_pos < sources[at - 1].char_end) {
      box.item = at - 1;  // ルビ組でないアイテムは [char_begin, char_end)
    }
    if (box.item == kNone && above != sources.end()) {
      box.item = at;  // char_pos 以降の最初のアイテム（#23）
    }
    if (box.item == kNone && !out_->items.empty() &&
        out_->items.back().kind != linebreak::ItemKind::ForcedBreak) {
      box.item = out_->items.size() - 1;  // 直前（= 最後）のアイテム
    }
  }
}

Result<void> ParagraphBuilder::build() {
  std::size_t i = 0;
  while (i < out_->chars.size()) {
    if ((*ruby_at_)[i] != kNone) {
      const std::size_t group = (*ruby_at_)[i];
      if (const Result<void> built = build_ruby_item(group); !built) {
        return built;
      }
      i = (*groups_)[group].base_end;
      continue;
    }
    if (out_->chars[i].kind != FlatChar::Kind::Text) {
      build_atomic_item(i);
      ++i;
      continue;
    }
    const Result<std::size_t> end = build_text_items(i);
    if (!end) {
      return std::unexpected(end.error());
    }
    i = *end;
  }
  resolve_ruby_overhang();
  resolve_empty_boxes();
  return {};
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
  out.empty_boxes = std::move(collected->empty_boxes);
  out.images = std::move(collected->images);

  // 支柱も文字と同じ表に入れる（行の高さの計算が 1 本道になる）。
  out.strut_style = out.styles.intern(*input.block_style, engine.map().direction(), input.location);
  out.metrics.reserve(out.styles.shaping_count());
  for (std::size_t i = 0; i < out.styles.shaping_count(); ++i) {
    Result<text::FontMetrics> metrics = engine.metrics(out.styles.shaping_at(i));
    if (!metrics) {
      return std::unexpected(metrics.error());
    }
    out.metrics.push_back(*metrics);
  }

  if (const Result<void> built =
          ParagraphBuilder(out, collected->ruby_at, collected->rubies, engine).build();
      !built) {
    return std::unexpected(built.error());
  }
  engine.counters().style_probes += out.styles.probes();
  return out;
}

Result<ParagraphHandle> shared_paragraph(const InlineInput& input, LayoutEngine& engine) {
  const ParagraphKey key{.children = input.children.data(),
                         .block_style = input.block_style,
                         .child_count = input.children.size()};
  if (engine.memo_enabled()) {
    if (const PreparedParagraph* found = engine.cache().paragraph(key)) {
      return ParagraphHandle(*found);
    }
  }
  Result<PreparedParagraph> prepared = prepare_paragraph(input, engine);
  if (!prepared) {
    return std::unexpected(prepared.error());
  }
  // <img> を含む段落だけは行の幅に依る: resolve_image() と `%` のマージンが
  // content_inline_size を基準にするので、幅が違えば中身も違う。共有しない
  if (!engine.memo_enabled() || !prepared->images.empty()) {
    return ParagraphHandle(std::move(*prepared));
  }
  return ParagraphHandle(engine.cache().remember(key, std::move(*prepared)));
}

}  // namespace shashoku::layout
