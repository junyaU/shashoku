#include "layout/inline_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/utf8.hpp"
#include "layout/east_asian_width.hpp"
#include "layout/logical.hpp"
#include "linebreak/line_breaker.hpp"

namespace shashoku::layout {
namespace {

using style::ComputedStyle;
using style::StyledNode;

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// CSS Text 3 §4.1.1 の collapsible white space（white-space: normal 固定）。
bool is_collapsible_space(char32_t cp) {
  return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\f';
}

// CSS Text 3 §4.1.3 の segment break（ソース中の改行）。A14 の対象。
bool is_segment_break(char32_t cp) { return cp == U'\n' || cp == U'\r'; }

// 断片の見た目と行の高さに効くスタイルだけを抜き出したもの。
// これが等しい連続した文字は 1 回の shape() にまとめ、1 つの TextFragment になる。
struct RunStyle {
  std::vector<std::string> font_family;
  int font_weight = 400;
  float font_size = 16;
  Color color = kBlack;
  float letter_spacing = 0;
  style::LineHeight line_height;

  bool operator==(const RunStyle&) const = default;
};

RunStyle run_style_of(const ComputedStyle& style) {
  return RunStyle{.font_family = style.font_family,
                  .font_weight = style.font_weight,
                  .font_size = style.font_size,
                  .color = style.color,
                  .letter_spacing = style.letter_spacing,
                  .line_height = style.line_height};
}

text::TextStyle text_style_of(const RunStyle& run, const LogicalMap& map) {
  return text::TextStyle{.font_family = run.font_family,
                         .font_weight = run.font_weight,
                         .font_size = run.font_size,
                         .direction = map.direction()};
}

// CSS Text 3 §5.3。ComputedStyle の Auto は Strict として扱う（computed_style.hpp）。
linebreak::Strictness strictness_of(style::LineBreak value) {
  switch (value) {
    case style::LineBreak::Normal:
      return linebreak::Strictness::Normal;
    case style::LineBreak::Loose:
      return linebreak::Strictness::Loose;
    case style::LineBreak::Auto:
    case style::LineBreak::Strict:
      break;
  }
  return linebreak::Strictness::Strict;
}

// (a) の出力。IFC の中身を木からほどいて 1 本にしたもの。
struct FlatChar {
  char32_t cp = 0;
  bool forced_break = false;  // <br>（cp は使わない）
  std::size_t style = 0;      // RunStyle の添字
};

// span の background-color。文字の範囲で覚えておき、(e) で行ごとの矩形にする。
struct BackgroundScope {
  Color color;
  std::size_t begin = 0;  // 文字の範囲 [begin, end)
  std::size_t end = 0;
  float ascent = 0;  // block 方向の範囲はその span 自身のフォントメトリクス
  float descent = 0;
};

struct Collected {
  std::vector<FlatChar> chars;
  std::vector<RunStyle> styles;
  std::vector<BackgroundScope> scopes;  // 外側の span が先（描画順）
};

std::size_t style_index(Collected& out, const ComputedStyle& style) {
  RunStyle run = run_style_of(style);
  for (std::size_t i = 0; i < out.styles.size(); ++i) {
    if (out.styles[i] == run) {
      return i;
    }
  }
  out.styles.push_back(std::move(run));
  return out.styles.size() - 1;
}

Result<void> collect(std::span<const StyledNode> nodes, text::TextMeasurer& measurer,
                     const LogicalMap& map, Collected& out);

// インライン box（span など）1 つ。第 2 段で <img>、第 3 段でルビが増えるのもここ。
// NOLINTNEXTLINE(misc-no-recursion): インライン box の入れ子。深さは html パーサが 256 に制限する
Result<void> collect_element(const StyledNode& node, text::TextMeasurer& measurer,
                             const LogicalMap& map, Collected& out) {
  if (node.style.display != style::Display::Inline) {
    return fail(ErrorKind::UnsupportedLayout,
                "block-level box inside an inline box is not supported (<" + node.tag + ">)",
                node.location);
  }
  if (node.tag == "br") {
    out.chars.push_back(
        FlatChar{.cp = U'\n', .forced_break = true, .style = style_index(out, node.style)});
    return {};
  }
  if (node.tag == "img") {
    return fail(ErrorKind::UnsupportedLayout, "<img> layout is not implemented yet", node.location);
  }
  if (node.tag == "ruby" || node.tag == "rt") {
    return fail(ErrorKind::UnsupportedLayout, "<" + node.tag + "> layout is not implemented yet",
                node.location);
  }
  // background-color があれば、行ごとの背景を出すために文字の範囲を覚える
  std::size_t scope = kNone;
  if (!node.style.background_color.transparent()) {
    const text::FontMetrics metrics =
        measurer.metrics(text_style_of(run_style_of(node.style), map));
    out.scopes.push_back(BackgroundScope{.color = node.style.background_color,
                                         .begin = out.chars.size(),
                                         .end = 0,
                                         .ascent = metrics.ascent,
                                         .descent = metrics.descent});
    scope = out.scopes.size() - 1;
  }
  if (const Result<void> result = collect(node.children, measurer, map, out); !result) {
    return result;
  }
  if (scope != kNone) {
    out.scopes[scope].end = out.chars.size();
  }
  return {};
}

// (a) 木を辿って文字列にほどく。空白の畳み込みはまだしない（ノード境界をまたいで
// 判断する必要があるので、平らにしてから 1 回で処理する）。
// NOLINTNEXTLINE(misc-no-recursion): インライン box の入れ子。深さは html パーサが 256 に制限する
Result<void> collect(std::span<const StyledNode> nodes, text::TextMeasurer& measurer,
                     const LogicalMap& map, Collected& out) {
  for (const StyledNode& node : nodes) {
    if (node.type == StyledNode::Type::Text) {
      const Result<std::u32string> text = decode_utf8(node.text);
      if (!text) {
        return std::unexpected(text.error());
      }
      const std::size_t style_id = style_index(out, node.style);
      for (const char32_t cp : *text) {
        out.chars.push_back(FlatChar{.cp = cp, .forced_break = false, .style = style_id});
      }
      continue;
    }
    if (node.style.display == style::Display::None) {
      continue;  // ② が落としているはずだが、来たら無視する
    }
    if (const Result<void> result = collect_element(node, measurer, map, out); !result) {
      return result;
    }
  }
  return {};
}

struct Collapsed {
  std::vector<FlatChar> chars;
  std::vector<std::size_t> source;  // chars[i] の畳み込み前の添字（狭義単調増加）
};

// (a) 空白の畳み込み（white-space: normal）。
//   * 空白・タブ・改行の連続は空白 1 個に潰す
//   * IFC の先頭と <br> の直後の空白は捨てる。<br> の直前の空白も捨てる（行末になるので）
//   * A14: 改行を含む並びで、前後の文字がどちらも全角なら空白を残さず消す
//   * IFC の末尾の空白は残す（行分割器が content_end で行末の空白として落とす）
// 状態はテキストノードや span の境界をまたいで引き継ぐ（平らにしてから処理するので自明）。
Collapsed collapse_whitespace(const std::vector<FlatChar>& input) {
  Collapsed out;
  out.chars.reserve(input.size());
  out.source.reserve(input.size());
  char32_t last = 0;  // 直前に出力した文字（0 = IFC の先頭 / <br> の直後）
  std::size_t i = 0;
  while (i < input.size()) {
    const FlatChar& current = input[i];
    if (current.forced_break || !is_collapsible_space(current.cp)) {
      out.chars.push_back(current);
      out.source.push_back(i);
      last = current.forced_break ? 0 : current.cp;
      ++i;
      continue;
    }
    std::size_t end = i;
    bool has_break = false;
    while (end < input.size() && !input[end].forced_break && is_collapsible_space(input[end].cp)) {
      has_break = has_break || is_segment_break(input[end].cp);
      ++end;
    }
    const bool next_is_break = end < input.size() && input[end].forced_break;
    const char32_t next = end < input.size() && !next_is_break ? input[end].cp : 0;

    bool drop = last == 0 || next_is_break;
    if (!drop && has_break && next != 0 && is_fullwidth(last) && is_fullwidth(next)) {
      drop = true;  // A14: 和文どうしに挟まれたソース改行は消す
    }
    if (!drop) {
      out.chars.push_back(FlatChar{.cp = U' ', .forced_break = false, .style = current.style});
      out.source.push_back(i);
      last = U' ';
    }
    i = end;
  }
  return out;
}

// (c) linebreak::Item 1 個の出どころ。
struct ItemSource {
  std::size_t run = kNone;  // ForcedBreak は kNone
  std::size_t glyph_begin = 0;
  std::size_t glyph_end = 0;
  std::size_t char_begin = 0;  // 畳み込み後の文字の範囲
  std::size_t char_end = 0;
  std::size_t style = 0;
};

// (b) 1 回の shape() の結果。
struct ShapedRun {
  std::size_t style = 0;
  text::ShapedText shaped;
};

// (e) 行内でアイテムが占めた inline の範囲（インライン背景の矩形に使う）。
struct Placement {
  float inline_start = 0;
  float inline_end = 0;
};

// 断片を分ける単位。フォールバックでフォントが変わる箇所・sideways が変わる箇所で分かれる。
struct FragmentKey {
  std::size_t style = kNone;
  FontId font = 0;
  bool sideways = false;

  bool operator==(const FragmentKey&) const = default;
};

// ベースラインから上下への広がり。
struct Extent {
  float above = 0;
  float below = 0;
};

// inline 方向の寄せ。
struct Alignment {
  float offset = 0;         // 行全体をずらす量
  float justify_share = 0;  // 分割可能位置 1 か所あたりに入れる空き
};

class InlineFormatter {
 public:
  InlineFormatter(const InlineInput& input, const Options& options, text::TextMeasurer& measurer,
                  WritingMode mode)
      : input_(&input), options_(&options), measurer_(&measurer), map_(mode) {}

  Result<std::vector<LineBox>> run();

 private:
  void build_items();
  void extend_line_height(std::size_t index, const text::FontMetrics& metrics,
                          Extent& extent) const;
  [[nodiscard]] Extent measure_line(const linebreak::Line& line) const;
  [[nodiscard]] Alignment align_line(const linebreak::Line& line, bool is_last) const;
  void place_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                  const Alignment& alignment, float baseline, std::vector<TextFragment>& texts,
                  std::vector<Placement>& placement) const;
  [[nodiscard]] std::vector<InlineBackground> build_backgrounds(
      const linebreak::Line& line, const std::vector<Placement>& placement, float baseline) const;
  [[nodiscard]] LineBox build_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                   bool is_last, float block_start) const;
  [[nodiscard]] std::string item_text(const ItemSource& source) const;

  const InlineInput* input_;
  const Options* options_;
  text::TextMeasurer* measurer_;
  LogicalMap map_;

  std::vector<FlatChar> chars_;
  std::vector<RunStyle> styles_;
  std::vector<text::FontMetrics> metrics_;
  std::vector<BackgroundScope> scopes_;
  RunStyle strut_;
  text::FontMetrics strut_metrics_;

  std::vector<ShapedRun> runs_;
  std::vector<linebreak::Item> items_;
  std::vector<ItemSource> sources_;
  std::vector<bool> opportunities_;  // text-align: justify のときだけ埋める
};

void InlineFormatter::build_items() {
  std::size_t i = 0;
  while (i < chars_.size()) {
    if (chars_[i].forced_break) {
      items_.push_back(linebreak::Item{.kind = linebreak::ItemKind::ForcedBreak,
                                       .cp = U'\n',
                                       .advance = 0,
                                       .em = styles_[chars_[i].style].font_size,
                                       .no_break_before = false});
      sources_.push_back(ItemSource{.run = kNone,
                                    .glyph_begin = 0,
                                    .glyph_end = 0,
                                    .char_begin = i,
                                    .char_end = i + 1,
                                    .style = chars_[i].style});
      ++i;
      continue;
    }
    // (b) スタイルが同じ連続区間を 1 回でシェーピングする（A6: 行ごとに測り直さない）
    std::size_t end = i;
    std::u32string text;
    while (end < chars_.size() && !chars_[end].forced_break &&
           chars_[end].style == chars_[i].style) {
      text.push_back(chars_[end].cp);
      ++end;
    }
    const std::size_t style_id = chars_[i].style;
    runs_.push_back(
        ShapedRun{.style = style_id,
                  .shaped = measurer_->shape(text, text_style_of(styles_[style_id], map_))});
    const std::size_t run = runs_.size() - 1;
    // (c) クラスタ → Item。letter-spacing は送りに足す
    for (const text::ShapedCluster& cluster : runs_[run].shaped.clusters) {
      items_.push_back(
          linebreak::Item{.kind = linebreak::ItemKind::Text,
                          .cp = cluster.text_begin < text.size() ? text[cluster.text_begin] : 0,
                          .advance = cluster.advance + styles_[style_id].letter_spacing,
                          .em = styles_[style_id].font_size,
                          .no_break_before = false});
      sources_.push_back(ItemSource{.run = run,
                                    .glyph_begin = cluster.glyph_begin,
                                    .glyph_end = cluster.glyph_end,
                                    .char_begin = i + cluster.text_begin,
                                    .char_end = i + cluster.text_end,
                                    .style = style_id});
    }
    i = end;
  }
}

std::string InlineFormatter::item_text(const ItemSource& source) const {
  std::string out;
  for (std::size_t i = source.char_begin; i < source.char_end && i < chars_.size(); ++i) {
    append_utf8(out, chars_[i].cp);
  }
  return out;
}

// CSS 2.1 §10.8: line-height と FontMetrics から半行間（half-leading）を出し、
// ベースラインより上（ascent + 半行間）と下（descent + 半行間）を広げる。
void InlineFormatter::extend_line_height(std::size_t index, const text::FontMetrics& metrics,
                                         Extent& extent) const {
  const RunStyle& run = index == kNone ? strut_ : styles_[index];
  float line_height = 0;
  switch (run.line_height.kind) {
    case style::LineHeight::Kind::Normal:
      line_height = metrics.ascent + metrics.descent + metrics.line_gap;
      break;
    case style::LineHeight::Kind::Number:
      line_height = run.line_height.value * run.font_size;
      break;
    case style::LineHeight::Kind::Px:
      line_height = run.line_height.value;
      break;
  }
  const float half_leading = (line_height - (metrics.ascent + metrics.descent)) / 2;
  extent.above = std::max(extent.above, metrics.ascent + half_leading);
  extent.below = std::max(extent.below, metrics.descent + half_leading);
}

Extent InlineFormatter::measure_line(const linebreak::Line& line) const {
  Extent extent;
  extend_line_height(kNone, strut_metrics_, extent);  // 支柱は内容によらず全行に参加する
  std::vector<bool> seen(styles_.size(), false);
  for (std::size_t i = line.begin; i < line.content_end; ++i) {
    const std::size_t style_id = sources_[i].style;
    if (seen[style_id]) {
      continue;
    }
    seen[style_id] = true;
    extend_line_height(style_id, metrics_[style_id], extent);
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

// (e) グリフを置く。手順は line_breaker.hpp の「描画側の手順」どおり:
//   pen += spacing.before → クラスタのグリフを順に置く → pen = 開始位置 + advance + spacing.after
void InlineFormatter::place_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                 const Alignment& alignment, float baseline,
                                 std::vector<TextFragment>& texts,
                                 std::vector<Placement>& placement) const {
  FragmentKey key;
  bool open = false;
  float pen = input_->content_inline_start + alignment.offset;
  for (std::size_t i = line.begin; i < line.content_end; ++i) {
    if (i > line.begin && alignment.justify_share > 0 && opportunities_[i]) {
      pen += alignment.justify_share;  // 禁則で割れない位置には空きを入れない（A13）
    }
    pen += breaks.spacing[i].before;
    const float item_start = pen;
    const ItemSource& source = sources_[i];
    float glyph_pen = item_start;
    for (std::size_t g = source.glyph_begin; source.run != kNone && g < source.glyph_end; ++g) {
      const text::ShapedGlyph& glyph = runs_[source.run].shaped.glyphs[g];
      const FragmentKey next{.style = source.style, .font = glyph.font, .sideways = glyph.sideways};
      if (!open || next != key) {
        key = next;
        open = true;
        texts.push_back(TextFragment{.font = glyph.font,
                                     .font_size = styles_[source.style].font_size,
                                     .color = styles_[source.style].color,
                                     .sideways = glyph.sideways,
                                     .baseline = baseline,
                                     .inline_start = glyph_pen,
                                     .inline_size = 0,
                                     .glyphs = {},
                                     .text = {}});
      }
      if (g == source.glyph_begin) {
        texts.back().text += item_text(source);
      }
      texts.back().glyphs.push_back(PositionedGlyph{.glyph_id = glyph.glyph_id,
                                                    .inline_position = glyph_pen,
                                                    .x_offset = glyph.x_offset,
                                                    .y_offset = glyph.y_offset});
      glyph_pen += glyph.advance;
    }
    pen = item_start + items_[i].advance + breaks.spacing[i].after;
    if (open) {
      texts.back().inline_size = pen - texts.back().inline_start;
    }
    placement[i] = Placement{.inline_start = item_start, .inline_end = pen};
  }
}

std::vector<InlineBackground> InlineFormatter::build_backgrounds(
    const linebreak::Line& line, const std::vector<Placement>& placement, float baseline) const {
  std::vector<InlineBackground> backgrounds;
  for (const BackgroundScope& scope : scopes_) {
    std::size_t first = kNone;
    std::size_t last = kNone;
    for (std::size_t i = line.begin; i < line.content_end; ++i) {
      const std::size_t at = sources_[i].char_begin;
      if (at >= scope.begin && at < scope.end) {
        first = first == kNone ? i : first;
        last = i;
      }
    }
    if (first == kNone) {
      continue;
    }
    backgrounds.push_back(InlineBackground{
        .rect =
            LogicalRect{.inline_start = placement[first].inline_start,
                        .block_start = baseline - scope.ascent,
                        .inline_size = placement[last].inline_end - placement[first].inline_start,
                        .block_size = scope.ascent + scope.descent},
        .color = scope.color});
  }
  return backgrounds;
}

LineBox InlineFormatter::build_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                    bool is_last, float block_start) const {
  const Extent extent = measure_line(line);
  LineBox box;
  box.rect = LogicalRect{.inline_start = input_->content_inline_start,
                         .block_start = block_start,
                         .inline_size = input_->content_inline_size,
                         .block_size = extent.above + extent.below};
  box.baseline = block_start + extent.above;

  std::vector<TextFragment> texts;
  std::vector<Placement> placement(items_.size());
  place_line(line, breaks, align_line(line, is_last), box.baseline, texts, placement);

  // 描画順: 背景 → 文字
  const std::vector<InlineBackground> backgrounds =
      build_backgrounds(line, placement, box.baseline);
  box.fragments.reserve(backgrounds.size() + texts.size());
  for (const InlineBackground& background : backgrounds) {
    box.fragments.emplace_back(background);  // 自明にコピーできる小さな型
  }
  for (TextFragment& text : texts) {
    box.fragments.emplace_back(std::move(text));
  }
  return box;
}

Result<std::vector<LineBox>> InlineFormatter::run() {
  Collected collected;
  if (const Result<void> result = collect(input_->children, *measurer_, map_, collected); !result) {
    return std::unexpected(result.error());
  }
  styles_ = std::move(collected.styles);
  scopes_ = std::move(collected.scopes);

  Collapsed collapsed = collapse_whitespace(collected.chars);
  chars_ = std::move(collapsed.chars);
  // 背景スコープの範囲を畳み込み後の添字に移す
  for (BackgroundScope& scope : scopes_) {
    const auto begin =
        std::lower_bound(collapsed.source.begin(), collapsed.source.end(), scope.begin) -
        collapsed.source.begin();
    const auto end = std::lower_bound(collapsed.source.begin(), collapsed.source.end(), scope.end) -
                     collapsed.source.begin();
    scope.begin = static_cast<std::size_t>(begin);
    scope.end = static_cast<std::size_t>(end);
  }

  strut_ = run_style_of(*input_->block_style);
  strut_metrics_ = measurer_->metrics(text_style_of(strut_, map_));
  metrics_.reserve(styles_.size());
  for (const RunStyle& run_style : styles_) {
    metrics_.push_back(measurer_->metrics(text_style_of(run_style, map_)));
  }

  build_items();
  if (items_.empty()) {
    return std::vector<LineBox>{};  // 空の IFC は行ボックスを作らない（高さ 0）
  }

  // (d) 行分割器。Config は Options を土台に、このブロックの CSS で上書きする
  linebreak::Config config = options_->line_break;
  config.strictness = strictness_of(input_->block_style->line_break);
  if (input_->block_style->overflow_wrap != style::OverflowWrap::Normal) {
    config.break_anywhere = true;
  }
  const linebreak::LineBreaker breaker(config);
  const linebreak::Breaks breaks = breaker.break_lines(items_, input_->content_inline_size);
  if (input_->block_style->text_align == style::TextAlign::Justify) {
    opportunities_ = breaker.break_opportunities(items_);
  } else {
    opportunities_.assign(items_.size(), false);
  }

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

Result<std::vector<LineBox>> layout_inline(const InlineInput& input, const Options& options,
                                           text::TextMeasurer& measurer, WritingMode mode) {
  return InlineFormatter(input, options, measurer, mode).run();
}

}  // namespace shashoku::layout
