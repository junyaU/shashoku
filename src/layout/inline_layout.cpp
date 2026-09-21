#include "layout/inline_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "layout/engine.hpp"
#include "layout/inline_collect.hpp"
#include "layout/inline_paragraph.hpp"
#include "layout/inline_style.hpp"
#include "layout/logical.hpp"
#include "linebreak/line_breaker.hpp"

// インライン整形文脈の (d)(e)（ARCHITECTURE.md §3.8）: 行分割器を回し、行ボックスを積む。
// 準備済み段落（inline_paragraph.hpp）を読むだけで、シェーピングはやり直さない。
namespace shashoku::layout {
namespace {

// (d) 行分割器の設定 = **段落の既定値**。Options を土台に、このブロックの CSS で上書きする。
// アイテムごとのポリシー（A23 / A28）を持たない Item にだけ効く。いまは (c) がすべての
// Item に値を入れているので実際には使われないが、契約としての既定値なので残す
// （約物のアキ・あふれ処理など、strictness / break_anywhere 以外の設定はここだけにある）。
linebreak::Config line_break_config(const InlineInput& input, const LayoutEngine& engine) {
  linebreak::Config config = engine.options().line_break;
  config.strictness = resolve_strictness(input.block_style->line_break, config.strictness);
  config.break_anywhere =
      config.break_anywhere || resolve_break_anywhere(input.block_style->overflow_wrap);
  return config;
}

// (e) 行内でアイテムが占めた inline の範囲（インライン背景の矩形に使う）。
struct Placement {
  float inline_start = 0;
  float inline_end = 0;
};

// 断片を分ける単位。フォールバックでフォントが変わる箇所・sideways が変わる箇所・
// 装飾（色）が変わる箇所で分かれる。**グリフの位置は 1 回のシェーピング結果で決まっていて、
// ここで断片を切っても動かない**（issue #8）。
// 見るのは「見た目に効く層」（シェーピング + 装飾）だけ: 行分割ポリシーのように見た目に
// 効かない層で断片を切ると、DrawGlyphs が無意味に細切れになる（A27 の用途の表）。
struct FragmentKey {
  std::size_t shaping = kNone;
  std::size_t decoration = kNone;
  FontId font = 0;
  bool sideways = false;

  bool operator==(const FragmentKey&) const = default;
};

// ベースライン（縦書きでは中心軸）から block-start 側 / block-end 側への広がり。
struct Extent {
  float above = 0;
  float below = 0;
};

// inline 方向の寄せ。
struct Alignment {
  float offset = 0;         // 行全体をずらす量
  float justify_share = 0;  // 分割可能位置 1 か所あたりに入れる空き
};

// 行の断片を積んでいく。グリフを置きながら、フォント・sideways・装飾が変わったら
// 新しい TextFragment に切り替える。ベースラインの違うもの（ルビ）は close() で区切る。
class FragmentWriter {
 public:
  FragmentWriter(std::vector<InlineFragment>& content, const PreparedParagraph& paragraph)
      : content_(&content), paragraph_(&paragraph) {}

  void close() { open_ = kNone; }

  // shaped の [begin, end) のグリフを pen から順に置く（pen はグリフの送りで進む）。
  void add(const text::ShapedText& shaped, std::size_t begin, std::size_t end, std::size_t style_id,
           float baseline, const std::string& text, float& pen) {
    for (std::size_t g = begin; g < end; ++g) {
      const text::ShapedGlyph& glyph = shaped.glyphs[g];
      const FragmentKey key{.shaping = paragraph_->styles.shaping_index(style_id),
                            .decoration = paragraph_->visual_key(style_id),
                            .font = glyph.font,
                            .sideways = glyph.sideways};
      if (open_ == kNone || key != key_) {
        key_ = key;
        // 位置は**断片の先頭のグリフ**のもの（A31）。位置では断片を切らないので、
        // 1 つの断片が複数のノードにまたがることがある
        content_->emplace_back(TextFragment{.font = glyph.font,
                                            .font_size = paragraph_->font_size(style_id),
                                            .color = paragraph_->color(style_id),
                                            .sideways = glyph.sideways,
                                            .baseline = baseline,
                                            .inline_start = pen,
                                            .inline_size = 0,
                                            .glyphs = {},
                                            .text = {},
                                            .location = paragraph_->location_of(style_id)});
        open_ = content_->size() - 1;
      }
      auto& fragment = std::get<TextFragment>((*content_)[open_]);
      if (g == begin) {
        fragment.text += text;
      }
      fragment.glyphs.push_back(PositionedGlyph{.glyph_id = glyph.glyph_id,
                                                .inline_position = pen,
                                                .x_offset = glyph.x_offset,
                                                .y_offset = glyph.y_offset});
      pen += glyph.advance;
    }
  }

  // 開いている断片の inline 範囲を end まで広げる（letter-spacing と Spacing のぶん）。
  void extend_to(float end) {
    if (open_ == kNone) {
      return;
    }
    auto& fragment = std::get<TextFragment>((*content_)[open_]);
    fragment.inline_size = end - fragment.inline_start;
  }

 private:
  std::vector<InlineFragment>* content_;
  const PreparedParagraph* paragraph_;
  FragmentKey key_;
  std::size_t open_ = kNone;
};

// 準備済み段落を版面の幅に流し込み、行ボックスを積む。
class InlineFormatter {
 public:
  InlineFormatter(const InlineInput& input, LayoutEngine& engine,
                  const PreparedParagraph& paragraph)
      : input_(&input),
        engine_(&engine),
        paragraph_(&paragraph),
        vertical_(engine.map().vertical()) {}

  std::vector<LineBox> run();

 private:
  void extend_line_height(std::size_t style_id, Extent& extent) const;
  [[nodiscard]] float ruby_above(const RubyPiece& piece) const;
  [[nodiscard]] float ruby_baseline(const RubyPiece& piece, float baseline) const;
  [[nodiscard]] Extent measure_line(const linebreak::Line& line);
  [[nodiscard]] Alignment align_line(const linebreak::Line& line, bool is_last) const;
  void place_image(const ImagePiece& image, float item_start, float baseline,
                   std::vector<InlineFragment>& content) const;
  // 1 クラスタぶんの配置。通常テキストのアイテムとルビ組の親文字で共有する（#16）。
  Placement place_cluster(const ItemSource& source, float advance, float baseline,
                          FragmentWriter& writer, float& pen) const;
  void place_ruby(const RubyPiece& piece, float item_start, float advance, float baseline,
                  FragmentWriter& writer);
  void place_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                  const Alignment& alignment, float baseline, std::vector<InlineFragment>& content);
  // 行 [line.begin, line.content_end) の中で、文字位置 char_index 以降から始まる最初のアイテム。
  // アイテムの char_begin は狭義単調増加なので二分探索できる。
  [[nodiscard]] std::size_t first_item_at(const linebreak::Line& line,
                                          std::size_t char_index) const;
  // ルビ組（アイテム item）の内部で、文字位置 char_index を含む / その手前のクラスタの
  // inline 位置。組は 1 アイテムなので、アイテム単位の二分探索では組の内部で始まる /
  // 終わる背景スコープを取りこぼす（#16）。クラスタの char_begin / char_end も
  // 狭義単調増加なので、組の中でも二分探索できる。
  [[nodiscard]] float ruby_cluster_start(std::size_t item, std::size_t char_index) const;
  [[nodiscard]] float ruby_cluster_end(std::size_t item, std::size_t char_index) const;
  [[nodiscard]] std::vector<InlineBackground> build_backgrounds(const linebreak::Line& line,
                                                                float baseline);
  [[nodiscard]] LineBox build_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                   bool is_last, float block_start);

  const InlineInput* input_;
  LayoutEngine* engine_;
  const PreparedParagraph* paragraph_;
  bool vertical_ = false;

  std::vector<bool> opportunities_;  // text-align: justify のときだけ埋める

  // 行の構築で使い回す作業バッファ。行ごとに確保すると段落全体で O(N×L) になる（#4）ので、
  // run() で 1 回だけ確保する。行をまたいで残る値は読まない（下の約束を守ること。A22）。
  //   placement_  : place_line() が書いた [line.begin, line.content_end) だけを読む
  //   ruby_placement_: place_ruby() が書いた「この行にあるルビ組」のクラスタだけを読む
  //   style_stamp_: measure_line() の「この行でもう見たスタイル」。世代印なので消さなくてよい
  std::vector<Placement> placement_;
  std::vector<Placement> ruby_placement_;
  std::vector<std::uint64_t> style_stamp_;
  std::uint64_t stamp_ = 0;
  // 背景スコープは begin の昇順（collect_element が外側から push する）。行が進むのに
  // 合わせて「いまの行と交差するスコープ」だけを持つ。
  std::size_t scope_cursor_ = 0;
  std::vector<std::size_t> active_scopes_;
};

// CSS 2.1 §10.8: line-height と FontMetrics から半行間（half-leading）を出し、
// ベースラインより上（ascent + 半行間）と下（descent + 半行間）を広げる。
// 縦書きでは「ベースライン」は行の中心軸なので、上下に line-height の半分ずつ広げる。
void InlineFormatter::extend_line_height(std::size_t style_id, Extent& extent) const {
  const text::FontMetrics& metrics = paragraph_->metrics_of(style_id);
  const style::LineHeight& style = paragraph_->line_height(style_id);
  float line_height = 0;
  switch (style.kind) {
    case style::LineHeight::Kind::Normal:
      line_height = metrics.ascent + metrics.descent + metrics.line_gap;
      break;
    case style::LineHeight::Kind::Number:
      line_height = style.value * paragraph_->font_size(style_id);
      break;
    case style::LineHeight::Kind::Px:
      line_height = style.value;
      break;
  }
  if (vertical_) {
    const float half = line_height / 2;
    extent.above = std::max(extent.above, half);
    extent.below = std::max(extent.below, half);
    return;
  }
  const float half_leading = (line_height - (metrics.ascent + metrics.descent)) / 2;
  extent.above = std::max(extent.above, metrics.ascent + half_leading);
  extent.below = std::max(extent.below, metrics.descent + half_leading);
}

// ルビ組がベースライン（中心軸）より block-start 側に広がる量。
// 親文字の寸法は**全クラスタの最大**（#17）。代表の文字（A28）は行分割ポリシー専用で、
// そこから幾何を取ると 2 文字目以降の font-size が絵から落ちる。
float InlineFormatter::ruby_above(const RubyPiece& piece) const {
  if (vertical_) {
    return (piece.max_base_font_size / 2) + piece.rt_font_size;
  }
  return piece.max_base_ascent + piece.rt_ascent + piece.rt_descent;
}

// ルビ文字のベースライン（縦書きでは中心軸）の block 位置。
// ルビは親文字の block-start 側（横書きは上、縦書きは紙面の右）に付く。block 座標は
// どちらの書字方向でも block-start 側が小さいので（縦書きは paint が
// x = viewport_width − block で右端から引く）、**引く**のが block-start 側になる。
// ruby_above() が空けているのと同じ側であること（逆にすると隣の行に食い込む）。
float InlineFormatter::ruby_baseline(const RubyPiece& piece, float baseline) const {
  if (vertical_) {
    // 中心軸から、親文字の内容領域の半分 + ルビの半分ぶん block-start 側へ
    return baseline - (piece.max_base_font_size / 2) - (piece.rt_font_size / 2);
  }
  // 親文字の内容領域の上端から、さらにルビの descent ぶん上
  return baseline - piece.max_base_ascent - piece.rt_descent;
}

Extent InlineFormatter::measure_line(const linebreak::Line& line) {
  Extent extent;
  ++stamp_;  // この行で「もう見た」印。行ごとに配列を作り直さない（#4）
  // 支柱は内容によらず全行に参加する
  extend_line_height(paragraph_->strut_style, extent);
  style_stamp_[paragraph_->strut_style] = stamp_;
  for (std::size_t i = line.begin; i < line.content_end; ++i) {
    const ItemSource& source = paragraph_->sources[i];
    if (source.image != kNone) {
      // 横書き: margin-box の下端がベースラインに乗る（CSS の既定の vertical-align）
      // 縦書き: margin-box を中心軸に中央揃えする
      const float span = paragraph_->images[source.image].margin_block();
      if (vertical_) {
        extent.above = std::max(extent.above, span / 2);
        extent.below = std::max(extent.below, span / 2);
      } else {
        extent.above = std::max(extent.above, span);
      }
      continue;
    }
    if (source.ruby != kNone) {
      const RubyPiece& piece = paragraph_->rubies[source.ruby];
      // 親文字は通常のインライン内容と同じに行の高さへ参加する（CSS Ruby 1 §2 / CSS 2.1
      // §10.8）。代表の文字 1 つでは 2 文字目以降の font-size / line-height が落ちる（#17）。
      // 同じスタイルのクラスタはメトリクスも同じなので、世代印で 1 回だけ見る
      if (style_stamp_[piece.base_style] != stamp_) {
        style_stamp_[piece.base_style] = stamp_;
        extend_line_height(piece.base_style, extent);
      }
      for (std::size_t c = piece.base_begin; c < piece.base_end; ++c) {
        const std::size_t style_id = paragraph_->ruby_clusters[c].source.style;
        if (style_stamp_[style_id] != stamp_) {
          style_stamp_[style_id] = stamp_;
          extend_line_height(style_id, extent);
        }
      }
      // ルビ（注釈）自身は行の高さに参加しない（CSS Ruby 1 §3.4）。参加するのは
      // 「親文字の外側に置くための張り出し」だけ
      extent.above = std::max(extent.above, ruby_above(piece));
      continue;
    }
    if (style_stamp_[source.style] == stamp_) {
      continue;
    }
    style_stamp_[source.style] = stamp_;
    extend_line_height(source.style, extent);
  }
  return extent;
}

Alignment InlineFormatter::align_line(const linebreak::Line& line, bool is_last) const {
  const float extra = input_->content_inline_size - line.width;
  Alignment alignment;
  switch (input_->block_style->text_align) {
    case style::TextAlign::Start:
    case style::TextAlign::Left:
      break;
    case style::TextAlign::End:
    case style::TextAlign::Right:
      alignment.offset = extra;
      break;
    case style::TextAlign::Center:
      alignment.offset = extra / 2;
      break;
    case style::TextAlign::Justify: {
      // 最終行・強制改行で終わった行・あふれた行は両端揃えにしない（text-align-last: auto）
      if (is_last || line.forced || line.overflows || extra <= 0) {
        break;
      }
      std::size_t gaps = 0;
      for (std::size_t i = line.begin + 1; i < line.content_end; ++i) {
        if (opportunities_[i]) {
          ++gaps;
        }
      }
      if (gaps > 0) {
        alignment.justify_share = extra / static_cast<float>(gaps);
      }
      break;
    }
  }
  return alignment;
}

void InlineFormatter::place_image(const ImagePiece& image, float item_start, float baseline,
                                  std::vector<InlineFragment>& content) const {
  const float inline_start = item_start + image.margin.inline_start;
  const float block_start = vertical_
                                ? baseline - (image.margin_block() / 2) + image.margin.block_start
                                : baseline - image.margin.block_end - image.border_block();
  content.emplace_back(ImageFragment{
      .image = image.id,
      .rect = LogicalRect{.inline_start = inline_start,
                          .block_start = block_start,
                          .inline_size = image.border_inline(),
                          .block_size = image.border_block()},
      .content_rect =
          LogicalRect{.inline_start = inline_start + image.border + image.padding.inline_start,
                      .block_start = block_start + image.border + image.padding.block_start,
                      .inline_size = image.content_inline_size,
                      .block_size = image.content_block_size},
      .decoration = image.decoration});
}

// (e) 1 クラスタぶんの配置。手順は line_breaker.hpp の「描画側の手順」どおり:
// グリフを順に置き、pen は「開始位置 + 送り」に進める。送りは letter-spacing 込みなので、
// 字間は文字と文字の**間**に入る（区間の末尾にまとめて入れない。#16）。
Placement InlineFormatter::place_cluster(const ItemSource& source, float advance, float baseline,
                                         FragmentWriter& writer, float& pen) const {
  const float start = pen;
  if (source.run != kNone) {
    float glyph_pen = start;
    writer.add(paragraph_->runs[source.run].shaped, source.glyph_begin, source.glyph_end,
               source.style, baseline, paragraph_->text_of(source.char_begin, source.char_end),
               glyph_pen);
  }
  pen = start + advance;
  writer.extend_to(pen);
  return Placement{.inline_start = start, .inline_end = pen};
}

// ルビ組: 親文字とルビの短い方を中央に置く（JLREQ の 1:2:1 の配分まではやらない）。
// 組の内部の親文字は、通常のインライン内容と同じ規則で配置する（CSS Ruby 1 §2。#16）。
void InlineFormatter::place_ruby(const RubyPiece& piece, float item_start, float advance,
                                 float baseline, FragmentWriter& writer) {
  writer.close();
  float pen = item_start + ((advance - piece.base_width) / 2);
  for (std::size_t i = piece.base_begin; i < piece.base_end; ++i) {
    const RubyCluster& cluster = paragraph_->ruby_clusters[i];
    ruby_placement_[i] = place_cluster(cluster.source, cluster.advance, baseline, writer, pen);
  }
  writer.close();

  // ルビ文字に letter-spacing は掛けない（A-new / §3.8 のルビ）
  float ruby_pen = item_start + ((advance - piece.rt_width) / 2);
  const text::ShapedText& ruby = paragraph_->runs[piece.rt_run].shaped;
  writer.add(ruby, 0, ruby.glyphs.size(), piece.rt_style, ruby_baseline(piece, baseline),
             piece.rt_text, ruby_pen);
  writer.extend_to(ruby_pen);
  writer.close();
}

// (e) グリフと画像を置く。手順は line_breaker.hpp の「描画側の手順」どおり:
//   pen += spacing.before → クラスタのグリフを順に置く → pen = 開始位置 + advance + spacing.after
void InlineFormatter::place_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                 const Alignment& alignment, float baseline,
                                 std::vector<InlineFragment>& content) {
  FragmentWriter writer(content, *paragraph_);
  float pen = input_->content_inline_start + alignment.offset;
  for (std::size_t i = line.begin; i < line.content_end; ++i) {
    if (i > line.begin && alignment.justify_share > 0 && opportunities_[i]) {
      pen += alignment.justify_share;  // 禁則で割れない位置には空きを入れない（A13）
    }
    pen += breaks.spacing[i].before;
    const float item_start = pen;
    const ItemSource& source = paragraph_->sources[i];

    if (source.image != kNone) {
      writer.close();
      place_image(paragraph_->images[source.image], item_start, baseline, content);
    } else if (source.ruby != kNone) {
      place_ruby(paragraph_->rubies[source.ruby], item_start, paragraph_->items[i].advance,
                 baseline, writer);
    } else {
      float glyph_pen = item_start;
      place_cluster(source, paragraph_->items[i].advance, baseline, writer, glyph_pen);
    }

    pen = item_start + paragraph_->items[i].advance + breaks.spacing[i].after;
    writer.extend_to(pen);
    placement_[i] = Placement{.inline_start = item_start, .inline_end = pen};
  }
}

std::size_t InlineFormatter::first_item_at(const linebreak::Line& line,
                                           std::size_t char_index) const {
  const auto begin = paragraph_->sources.begin() + static_cast<std::ptrdiff_t>(line.begin);
  const auto end = paragraph_->sources.begin() + static_cast<std::ptrdiff_t>(line.content_end);
  const auto found = std::ranges::lower_bound(begin, end, char_index, {}, &ItemSource::char_begin);
  return static_cast<std::size_t>(found - paragraph_->sources.begin());
}

float InlineFormatter::ruby_cluster_start(std::size_t item, std::size_t char_index) const {
  const RubyPiece& piece = paragraph_->rubies[paragraph_->sources[item].ruby];
  const auto first =
      paragraph_->ruby_clusters.begin() + static_cast<std::ptrdiff_t>(piece.base_begin);
  const auto last = paragraph_->ruby_clusters.begin() + static_cast<std::ptrdiff_t>(piece.base_end);
  // char_index を含む（= char_end がそれより後ろの）最初のクラスタ。クラスタの途中から
  // 始まるスコープはクラスタの頭から塗る（A27 の「クラスタ先頭の文字を採る」と同じ）
  const auto found =
      std::ranges::upper_bound(first, last, char_index, {},
                               [](const RubyCluster& cluster) { return cluster.source.char_end; });
  if (found == last) {
    return placement_[item].inline_start;  // 起きないはずだが、組の箱に丸める
  }
  return ruby_placement_[static_cast<std::size_t>(found - paragraph_->ruby_clusters.begin())]
      .inline_start;
}

float InlineFormatter::ruby_cluster_end(std::size_t item, std::size_t char_index) const {
  const RubyPiece& piece = paragraph_->rubies[paragraph_->sources[item].ruby];
  const auto first =
      paragraph_->ruby_clusters.begin() + static_cast<std::ptrdiff_t>(piece.base_begin);
  const auto last = paragraph_->ruby_clusters.begin() + static_cast<std::ptrdiff_t>(piece.base_end);
  // char_index の手前から始まる最後のクラスタ
  const auto found = std::ranges::lower_bound(
      first, last, char_index, {},
      [](const RubyCluster& cluster) { return cluster.source.char_begin; });
  if (found == first) {
    return placement_[item].inline_end;  // 起きないはずだが、組の箱に丸める
  }
  return ruby_placement_[static_cast<std::size_t>(found - 1 - paragraph_->ruby_clusters.begin())]
      .inline_end;
}

// 行と交差する背景スコープだけを見る（#4）。スコープは begin の昇順なので、行が進むのに
// 合わせて active_scopes_ を更新すれば、1 行あたりの仕事は「その行に出る背景の数」で済む。
std::vector<InlineBackground> InlineFormatter::build_backgrounds(const linebreak::Line& line,
                                                                 float baseline) {
  std::vector<InlineBackground> backgrounds;
  if (line.begin >= line.content_end) {
    return backgrounds;  // 中身のない行（強制改行だけの行）には背景も出ない
  }
  const std::vector<BackgroundScope>& scopes = paragraph_->scopes;
  const std::size_t char_begin = paragraph_->sources[line.begin].char_begin;
  const std::size_t char_end = paragraph_->sources[line.content_end - 1].char_end;
  // この行の先頭より前で終わったスコープを落とす（行の先頭は行ごとに進むので、落とすのは 1 回ずつ）
  std::erase_if(active_scopes_, [&](std::size_t index) { return scopes[index].end <= char_begin; });
  // この行の終わりより前に始まるスコープを入れる（cursor も行とともに 1 方向に進む）
  while (scope_cursor_ < scopes.size() && scopes[scope_cursor_].begin < char_end) {
    if (scopes[scope_cursor_].end > char_begin) {
      active_scopes_.push_back(scope_cursor_);
    }
    ++scope_cursor_;
  }
  engine_->counters().background_probes += active_scopes_.size();

  for (const std::size_t index : active_scopes_) {  // 外側の span が先（描画順）
    const BackgroundScope& scope = scopes[index];
    if (scope.begin >= scope.end) {
      continue;  // 文字を 1 つも含まない span（背景も出ない）
    }
    std::size_t first = first_item_at(line, scope.begin);
    const std::size_t after = first_item_at(line, scope.end);
    // ルビ組は 1 アイテムなので、組の**内部**から始まるスコープは上の探索が組を飛び越す。
    // 親文字は通常のインライン内容と同じ矩形を出す（#16）ので、組まで戻ってクラスタを見る
    const bool starts_inside_ruby = first > line.begin &&
                                    paragraph_->sources[first - 1].ruby != kNone &&
                                    paragraph_->sources[first - 1].char_end > scope.begin;
    if (starts_inside_ruby) {
      --first;
    }
    if (first >= after) {
      continue;  // 交差はしているが、この行にはこのスコープの文字がない
    }
    const std::size_t last = after - 1;
    const float inline_start = starts_inside_ruby ? ruby_cluster_start(first, scope.begin)
                                                  : placement_[first].inline_start;
    const bool ends_inside_ruby =
        paragraph_->sources[last].ruby != kNone && scope.end < paragraph_->sources[last].char_end;
    const float inline_end =
        ends_inside_ruby ? ruby_cluster_end(last, scope.end) : placement_[last].inline_end;
    backgrounds.push_back(
        InlineBackground{.rect = LogicalRect{.inline_start = inline_start,
                                             .block_start = baseline - scope.start_extent,
                                             .inline_size = inline_end - inline_start,
                                             .block_size = scope.size},
                         .color = scope.color});
  }
  return backgrounds;
}

LineBox InlineFormatter::build_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                    bool is_last, float block_start) {
  const Extent extent = measure_line(line);
  LineBox box;
  box.rect = LogicalRect{.inline_start = input_->content_inline_start,
                         .block_start = block_start,
                         .inline_size = input_->content_inline_size,
                         .block_size = extent.above + extent.below};
  box.baseline = block_start + extent.above;

  std::vector<InlineFragment> content;
  ++engine_->counters().line_boxes;
  place_line(line, breaks, align_line(line, is_last), box.baseline, content);

  // 描画順: 背景 → 文字・画像
  const std::vector<InlineBackground> backgrounds = build_backgrounds(line, box.baseline);
  box.fragments.reserve(backgrounds.size() + content.size());
  for (const InlineBackground& background : backgrounds) {
    box.fragments.emplace_back(background);  // 自明にコピーできる小さな型
  }
  for (InlineFragment& fragment : content) {
    box.fragments.push_back(std::move(fragment));
  }
  return box;
}

std::vector<LineBox> InlineFormatter::run() {
  const std::vector<linebreak::Item>& items = paragraph_->items;
  if (items.empty()) {
    return {};  // 空の IFC は行ボックスを作らない（高さ 0）
  }

  const linebreak::LineBreaker breaker(line_break_config(*input_, *engine_));
  linebreak::Counters& counters = engine_->counters().line_breaker;
  const linebreak::Breaks breaks =
      breaker.break_lines(items, input_->content_inline_size, &counters);
  if (input_->block_style->text_align == style::TextAlign::Justify) {
    opportunities_ = breaker.break_opportunities(items, &counters);
  } else {
    opportunities_.assign(items.size(), false);
  }
  // 行の構築の作業バッファは、行ごとではなく段落で 1 回だけ確保する（#4）
  placement_.assign(items.size(), Placement{});
  ruby_placement_.assign(paragraph_->ruby_clusters.size(), Placement{});
  style_stamp_.assign(paragraph_->styles.size(), 0);
  engine_->counters().line_scratch +=
      items.size() + paragraph_->ruby_clusters.size() + paragraph_->styles.size();

  std::vector<LineBox> lines;
  lines.reserve(breaks.lines.size());
  float block_cursor = input_->content_block_start;
  for (std::size_t i = 0; i < breaks.lines.size(); ++i) {
    lines.push_back(
        build_line(breaks.lines[i], breaks, i + 1 == breaks.lines.size(), block_cursor));
    block_cursor = lines.back().rect.block_end();
  }
  return lines;
}

}  // namespace

Result<std::vector<LineBox>> layout_inline(const InlineInput& input, LayoutEngine& engine) {
  const Result<ParagraphHandle> paragraph = shared_paragraph(input, engine);
  if (!paragraph) {
    return std::unexpected(paragraph.error());
  }
  return InlineFormatter(input, engine, paragraph->get()).run();
}

Result<Intrinsic> inline_intrinsic(const InlineInput& input, LayoutEngine& engine) {
  const Result<ParagraphHandle> handle = shared_paragraph(input, engine);
  if (!handle) {
    return std::unexpected(handle.error());
  }
  const PreparedParagraph& paragraph = handle->get();
  Intrinsic out;
  if (paragraph.items.empty()) {
    return out;
  }
  const linebreak::LineBreaker breaker(line_break_config(input, engine));
  linebreak::Counters& counters = engine.counters().line_breaker;
  const linebreak::Breaks breaks =
      breaker.break_lines(paragraph.items, linebreak::kUnbounded, &counters);
  for (const linebreak::Line& line : breaks.lines) {
    out.max_content = std::max(out.max_content, line.width);
  }
  out.min_content = breaker.min_content_width(paragraph.items, &counters);
  return out;
}

}  // namespace shashoku::layout
