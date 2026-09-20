#include "layout/inline_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/utf8.hpp"
#include "layout/east_asian_width.hpp"
#include "layout/engine.hpp"
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

// CSS Text 3 §5.3。`line-break: auto` は「エンジンの既定に従う」= Options の値を使う。
linebreak::Strictness strictness_of(style::LineBreak value, linebreak::Strictness fallback) {
  switch (value) {
    case style::LineBreak::Normal:
      return linebreak::Strictness::Normal;
    case style::LineBreak::Loose:
      return linebreak::Strictness::Loose;
    case style::LineBreak::Strict:
      return linebreak::Strictness::Strict;
    case style::LineBreak::Auto:
      break;
  }
  return fallback;
}

// 行の中に流れる <img>。行分割器から見れば分割不能な箱（ItemKind::Atomic）。
struct ImagePiece {
  ImageId id = 0;
  LogicalEdges<float> margin;
  LogicalEdges<float> padding;
  float border = 0;
  float content_inline_size = 0;
  float content_block_size = 0;
  BoxDecoration decoration;

  [[nodiscard]] float border_inline() const {
    return content_inline_size + (2 * border) + padding.inline_start + padding.inline_end;
  }
  [[nodiscard]] float border_block() const {
    return content_block_size + (2 * border) + padding.block_start + padding.block_end;
  }
  [[nodiscard]] float margin_inline() const {
    return border_inline() + margin.inline_start + margin.inline_end;
  }
  [[nodiscard]] float margin_block() const {
    return border_block() + margin.block_start + margin.block_end;
  }
};

// (a) の出力。IFC の中身を木からほどいて 1 本にしたもの。
struct FlatChar {
  enum class Kind : std::uint8_t { Text, ForcedBreak, Image };

  char32_t cp = 0;
  Kind kind = Kind::Text;
  std::size_t style = 0;  // RunStyle の添字
  std::size_t image = kNone;
};

// 空白の畳み込みから見たときの「この位置の文字」。画像は U+FFFC（置換文字）扱いにして、
// 全角ではない = 前後の改行は空白になる（CSS の既定と同じ）。
char32_t effective_cp(const FlatChar& flat) {
  return flat.kind == FlatChar::Kind::Image ? U'￼' : flat.cp;
}

// span の background-color。文字の範囲で覚えておき、(e) で行ごとの矩形にする。
struct BackgroundScope {
  Color color;
  std::size_t begin = 0;  // 文字の範囲 [begin, end)
  std::size_t end = 0;
  // block 方向の範囲。横書きは ascent / descent、縦書きは中心軸から ±(font-size / 2)
  float start_extent = 0;
  float size = 0;
};

// <ruby> の中の「親文字 + <rt>」1 組。親文字は chars の範囲で持つので、
// 色・背景・フォールバックによる断片の分割は普通のテキストと同じに効く。
struct RubyGroup {
  std::size_t base_begin = 0;
  std::size_t base_end = 0;
  std::size_t rt_style = 0;
  std::u32string rt_text;
};

struct Collected {
  std::vector<FlatChar> chars;
  std::vector<RunStyle> styles;
  std::vector<BackgroundScope> scopes;  // 外側の span が先（描画順）
  std::vector<ImagePiece> images;
  std::vector<RubyGroup> rubies;
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

void push_text(Collected& out, std::u32string_view text, std::size_t style_id) {
  for (const char32_t cp : text) {
    out.chars.push_back(
        FlatChar{.cp = cp, .kind = FlatChar::Kind::Text, .style = style_id, .image = kNone});
  }
}

// ルビ文字は折り返さないので、空白は 1 個に潰して前後を落としてから使う。
std::u32string collapse_ruby_text(std::u32string_view text) {
  std::u32string out;
  bool pending = false;
  for (const char32_t cp : text) {
    if (is_collapsible_space(cp)) {
      pending = !out.empty();
      continue;
    }
    if (pending) {
      out.push_back(U' ');
      pending = false;
    }
    out.push_back(cp);
  }
  return out;
}

Result<void> collect(std::span<const StyledNode> nodes, LayoutEngine& engine, float percent_basis,
                     Collected& out);
Result<void> collect_element(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                             Collected& out);

Result<void> collect_image(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                           Collected& out) {
  const Result<ResolvedImage> image = engine.resolve_image(node, percent_basis);
  if (!image) {
    return std::unexpected(image.error());
  }
  out.images.push_back(ImagePiece{
      .id = image->id,
      .margin = engine.resolve_margin(node.style, percent_basis),
      .padding = engine.map().edges(node.style.padding),
      .border = std::max(node.style.border_width, 0.0F),
      .content_inline_size = image->inline_size,
      .content_block_size = image->block_size,
      .decoration = BoxDecoration{.background_color = node.style.background_color,
                                  .border_width = std::max(node.style.border_width, 0.0F),
                                  .border_color = node.style.border_color,
                                  .border_radius = std::max(node.style.border_radius, 0.0F)}});
  out.chars.push_back(FlatChar{.cp = U'￼',
                               .kind = FlatChar::Kind::Image,
                               .style = style_index(out, node.style),
                               .image = out.images.size() - 1});
  return {};
}

// <rt> の中身（テキストだけ）を読む。
Result<std::u32string> read_ruby_text(const StyledNode& node) {
  std::u32string out;
  for (const StyledNode& child : node.children) {
    if (child.type != StyledNode::Type::Text) {
      return fail(ErrorKind::UnsupportedLayout,
                  "<rt> may only contain text (found <" + child.tag + ">)", child.location);
    }
    const Result<std::u32string> text = decode_utf8(child.text);
    if (!text) {
      return std::unexpected(text.error());
    }
    out += *text;
  }
  return collapse_ruby_text(out);
}

// <ruby>: 「<rt> 以外の連続（親文字）」+「直後の <rt>」を 1 組にする。
// <rt> が続かない親文字はルビなしの普通のテキストとしてそのまま残る。
Result<void> collect_ruby(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                          Collected& out) {
  std::size_t base_begin = out.chars.size();
  for (const StyledNode& child : node.children) {
    if (child.type == StyledNode::Type::Text) {
      const Result<std::u32string> text = decode_utf8(child.text);
      if (!text) {
        return std::unexpected(text.error());
      }
      push_text(out, *text, style_index(out, child.style));
      continue;
    }
    if (child.style.display == style::Display::None) {
      continue;
    }
    if (child.tag == "rt") {
      if (out.chars.size() == base_begin) {
        return fail(ErrorKind::UnsupportedLayout, "<rt> needs base text before it inside <ruby>",
                    child.location);
      }
      Result<std::u32string> ruby = read_ruby_text(child);
      if (!ruby) {
        return std::unexpected(ruby.error());
      }
      out.rubies.push_back(RubyGroup{.base_begin = base_begin,
                                     .base_end = out.chars.size(),
                                     .rt_style = style_index(out, child.style),
                                     .rt_text = std::move(*ruby)});
      base_begin = out.chars.size();
      continue;
    }
    if (child.tag == "ruby" || child.tag == "img" || child.tag == "br") {
      return fail(ErrorKind::UnsupportedLayout,
                  "<" + child.tag + "> is not supported inside <ruby>", child.location);
    }
    if (const Result<void> result = collect_element(child, engine, percent_basis, out); !result) {
      return result;
    }
  }
  return {};
}

// インライン box（span など）1 つ。
Result<void> collect_element(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                             Collected& out) {
  if (node.style.display != style::Display::Inline) {
    return fail(ErrorKind::UnsupportedLayout,
                "block-level box inside an inline box is not supported (<" + node.tag + ">)",
                node.location);
  }
  if (node.tag == "br") {
    out.chars.push_back(FlatChar{.cp = U'\n',
                                 .kind = FlatChar::Kind::ForcedBreak,
                                 .style = style_index(out, node.style),
                                 .image = kNone});
    return {};
  }
  if (node.tag == "img") {
    return collect_image(node, engine, percent_basis, out);
  }
  if (node.tag == "ruby") {
    return collect_ruby(node, engine, percent_basis, out);
  }
  if (node.tag == "rt") {
    return fail(ErrorKind::UnsupportedLayout, "<rt> is only allowed inside <ruby>", node.location);
  }
  // background-color があれば、行ごとの背景を出すために文字の範囲を覚える
  std::size_t scope = kNone;
  if (!node.style.background_color.transparent()) {
    const RunStyle run = run_style_of(node.style);
    const text::FontMetrics metrics = engine.metrics(text_style_of(run, engine.map()));
    const bool vertical = engine.map().vertical();
    out.scopes.push_back(
        BackgroundScope{.color = node.style.background_color,
                        .begin = out.chars.size(),
                        .end = 0,
                        .start_extent = vertical ? run.font_size / 2 : metrics.ascent,
                        .size = vertical ? run.font_size : metrics.ascent + metrics.descent});
    scope = out.scopes.size() - 1;
  }
  if (const Result<void> result = collect(node.children, engine, percent_basis, out); !result) {
    return result;
  }
  if (scope != kNone) {
    out.scopes[scope].end = out.chars.size();
  }
  return {};
}

// (a) 木を辿って 1 本にほどく。空白の畳み込みはまだしない（ノード境界をまたいで
// 判断する必要があるので、平らにしてから 1 回で処理する）。
Result<void> collect(std::span<const StyledNode> nodes, LayoutEngine& engine, float percent_basis,
                     Collected& out) {
  for (const StyledNode& node : nodes) {
    if (node.type == StyledNode::Type::Text) {
      const Result<std::u32string> text = decode_utf8(node.text);
      if (!text) {
        return std::unexpected(text.error());
      }
      push_text(out, *text, style_index(out, node.style));
      continue;
    }
    if (node.style.display == style::Display::None) {
      continue;  // ② が落としているはずだが、来たら無視する
    }
    if (const Result<void> result = collect_element(node, engine, percent_basis, out); !result) {
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
    const bool space = current.kind == FlatChar::Kind::Text && is_collapsible_space(current.cp);
    if (!space) {
      out.chars.push_back(current);
      out.source.push_back(i);
      last = current.kind == FlatChar::Kind::ForcedBreak ? 0 : effective_cp(current);
      ++i;
      continue;
    }
    std::size_t end = i;
    bool has_break = false;
    while (end < input.size() && input[end].kind == FlatChar::Kind::Text &&
           is_collapsible_space(input[end].cp)) {
      has_break = has_break || is_segment_break(input[end].cp);
      ++end;
    }
    const bool next_is_break = end < input.size() && input[end].kind == FlatChar::Kind::ForcedBreak;
    const char32_t next = end < input.size() && !next_is_break ? effective_cp(input[end]) : 0;

    bool drop = last == 0 || next_is_break;
    if (!drop && has_break && next != 0 && is_fullwidth(last) && is_fullwidth(next)) {
      drop = true;  // A14: 和文どうしに挟まれたソース改行は消す
    }
    if (!drop) {
      out.chars.push_back(FlatChar{
          .cp = U' ', .kind = FlatChar::Kind::Text, .style = current.style, .image = kNone});
      out.source.push_back(i);
      last = U' ';
    }
    i = end;
  }
  return out;
}

// (b) 1 回の shape() の結果。
struct ShapedRun {
  std::size_t style = 0;
  text::ShapedText shaped;
};

// ルビ組の親文字の 1 区間（スタイルが同じ連続）。
struct BaseSegment {
  std::size_t run = kNone;
  std::size_t glyph_begin = 0;
  std::size_t glyph_end = 0;
  std::size_t style = 0;
  std::size_t char_begin = 0;
  std::size_t char_end = 0;
  float advance = 0;  // クラスタの送りの合計（letter-spacing 込み）
};

// ルビ 1 組。行分割器から見れば Atomic 1 個で、組の内部では改行しない。
struct RubyPiece {
  std::vector<BaseSegment> base;
  float base_width = 0;
  std::size_t base_style = 0;
  float base_ascent = 0;
  float base_font_size = 0;

  std::size_t rt_run = kNone;
  std::size_t rt_style = 0;
  float rt_width = 0;
  float rt_ascent = 0;
  float rt_descent = 0;
  float rt_font_size = 0;
  std::string rt_text;
};

// (c) linebreak::Item 1 個の出どころ。
struct ItemSource {
  std::size_t run = kNone;  // ForcedBreak・<img>・ルビ組は kNone
  std::size_t glyph_begin = 0;
  std::size_t glyph_end = 0;
  std::size_t char_begin = 0;  // 畳み込み後の文字の範囲
  std::size_t char_end = 0;
  std::size_t style = 0;
  std::size_t image = kNone;
  std::size_t ruby = kNone;
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

// 行の断片を積んでいく。グリフを置きながら、フォント・sideways・スタイルが変わったら
// 新しい TextFragment に切り替える。ベースラインの違うもの（ルビ）は close() で区切る。
class FragmentWriter {
 public:
  FragmentWriter(std::vector<InlineFragment>& content, const std::vector<RunStyle>& styles)
      : content_(&content), styles_(&styles) {}

  void close() { open_ = kNone; }

  // shaped の [begin, end) のグリフを pen から順に置く（pen はグリフの送りで進む）。
  void add(const text::ShapedText& shaped, std::size_t begin, std::size_t end, std::size_t style_id,
           float baseline, const std::string& text, float& pen) {
    for (std::size_t g = begin; g < end; ++g) {
      const text::ShapedGlyph& glyph = shaped.glyphs[g];
      const FragmentKey key{.style = style_id, .font = glyph.font, .sideways = glyph.sideways};
      if (open_ == kNone || key != key_) {
        key_ = key;
        content_->emplace_back(TextFragment{.font = glyph.font,
                                            .font_size = (*styles_)[style_id].font_size,
                                            .color = (*styles_)[style_id].color,
                                            .sideways = glyph.sideways,
                                            .baseline = baseline,
                                            .inline_start = pen,
                                            .inline_size = 0,
                                            .glyphs = {},
                                            .text = {}});
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
  const std::vector<RunStyle>* styles_;
  FragmentKey key_;
  std::size_t open_ = kNone;
};

class InlineFormatter {
 public:
  InlineFormatter(const InlineInput& input, LayoutEngine& engine)
      : input_(&input), engine_(&engine), vertical_(engine.map().vertical()) {}

  Result<std::vector<LineBox>> run();
  Result<Intrinsic> intrinsic();

 private:
  Result<void> prepare();
  void build_items();
  void build_ruby_item(std::size_t group_index);
  [[nodiscard]] linebreak::Config config() const;
  void extend_line_height(std::size_t index, const text::FontMetrics& metrics,
                          Extent& extent) const;
  [[nodiscard]] float ruby_above(const RubyPiece& piece) const;
  [[nodiscard]] float ruby_baseline(const RubyPiece& piece, float baseline) const;
  [[nodiscard]] Extent measure_line(const linebreak::Line& line);
  [[nodiscard]] Alignment align_line(const linebreak::Line& line, bool is_last) const;
  void place_image(const ImagePiece& image, float item_start, float baseline,
                   std::vector<InlineFragment>& content) const;
  void place_ruby(const RubyPiece& piece, float item_start, float advance, float baseline,
                  FragmentWriter& writer) const;
  void place_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                  const Alignment& alignment, float baseline, std::vector<InlineFragment>& content);
  // 行 [line.begin, line.content_end) の中で、文字位置 char_index 以降から始まる最初のアイテム。
  // アイテムの char_begin は狭義単調増加なので二分探索できる。
  [[nodiscard]] std::size_t first_item_at(const linebreak::Line& line,
                                          std::size_t char_index) const;
  [[nodiscard]] std::vector<InlineBackground> build_backgrounds(const linebreak::Line& line,
                                                                float baseline);
  [[nodiscard]] LineBox build_line(const linebreak::Line& line, const linebreak::Breaks& breaks,
                                   bool is_last, float block_start);
  [[nodiscard]] std::string text_of(std::size_t char_begin, std::size_t char_end) const;

  const InlineInput* input_;
  LayoutEngine* engine_;
  bool vertical_ = false;

  std::vector<FlatChar> chars_;
  std::vector<RunStyle> styles_;
  std::vector<text::FontMetrics> metrics_;
  std::vector<BackgroundScope> scopes_;
  std::vector<ImagePiece> images_;
  std::vector<RubyGroup> groups_;
  std::vector<std::size_t> ruby_at_;  // 文字の位置 → そこから始まるルビ組
  RunStyle strut_;
  text::FontMetrics strut_metrics_;

  std::vector<ShapedRun> runs_;
  std::vector<RubyPiece> rubies_;
  std::vector<linebreak::Item> items_;
  std::vector<ItemSource> sources_;
  std::vector<bool> opportunities_;  // text-align: justify のときだけ埋める

  // 行の構築で使い回す作業バッファ。行ごとに確保すると段落全体で O(N×L) になる（#4）ので、
  // run() で 1 回だけ確保する。行をまたいで残る値は読まない（下の約束を守ること）。
  //   placement_  : place_line() が書いた [line.begin, line.content_end) だけを読む
  //   style_stamp_: measure_line() の「この行でもう見たスタイル」。世代印なので消さなくてよい
  std::vector<Placement> placement_;
  std::vector<std::uint64_t> style_stamp_;
  std::uint64_t stamp_ = 0;
  // 背景スコープは begin の昇順（collect_element が外側から push する）。行が進むのに
  // 合わせて「いまの行と交差するスコープ」だけを持つ。
  std::size_t scope_cursor_ = 0;
  std::vector<std::size_t> active_scopes_;
};

void InlineFormatter::build_ruby_item(std::size_t group_index) {
  const RubyGroup& group = groups_[group_index];
  RubyPiece piece;
  piece.base_style = chars_[group.base_begin].style;

  std::size_t i = group.base_begin;
  while (i < group.base_end) {
    const std::size_t style_id = chars_[i].style;
    std::size_t end = i;
    std::u32string text;
    while (end < group.base_end && chars_[end].style == style_id) {
      text.push_back(chars_[end].cp);
      ++end;
    }
    runs_.push_back(
        ShapedRun{.style = style_id,
                  .shaped = engine_->shape(text, text_style_of(styles_[style_id], engine_->map()))});
    const ShapedRun& run = runs_.back();
    float advance = 0;
    for (const text::ShapedCluster& cluster : run.shaped.clusters) {
      advance += cluster.advance + styles_[style_id].letter_spacing;
    }
    piece.base.push_back(BaseSegment{.run = runs_.size() - 1,
                                     .glyph_begin = 0,
                                     .glyph_end = run.shaped.glyphs.size(),
                                     .style = style_id,
                                     .char_begin = i,
                                     .char_end = end,
                                     .advance = advance});
    piece.base_width += advance;
    i = end;
  }

  // ルビ文字。letter-spacing はルビには掛けない
  const std::size_t rt_style = group.rt_style;
  runs_.push_back(ShapedRun{
      .style = rt_style,
      .shaped = engine_->shape(group.rt_text, text_style_of(styles_[rt_style], engine_->map()))});
  piece.rt_run = runs_.size() - 1;
  piece.rt_style = rt_style;
  for (const text::ShapedCluster& cluster : runs_.back().shaped.clusters) {
    piece.rt_width += cluster.advance;
  }
  piece.rt_text = encode_utf8(group.rt_text);
  piece.base_ascent = metrics_[piece.base_style].ascent;
  piece.base_font_size = styles_[piece.base_style].font_size;
  piece.rt_ascent = metrics_[rt_style].ascent;
  piece.rt_descent = metrics_[rt_style].descent;
  piece.rt_font_size = styles_[rt_style].font_size;

  const float advance = std::max(piece.base_width, piece.rt_width);
  const float em = piece.base_font_size;
  items_.push_back(linebreak::Item{.kind = linebreak::ItemKind::Atomic,
                                   .cp = chars_[group.base_begin].cp,
                                   .advance = advance,
                                   .em = em,
                                   .no_break_before = false});
  sources_.push_back(ItemSource{.run = kNone,
                                .glyph_begin = 0,
                                .glyph_end = 0,
                                .char_begin = group.base_begin,
                                .char_end = group.base_end,
                                .style = piece.base_style,
                                .image = kNone,
                                .ruby = rubies_.size()});
  rubies_.push_back(std::move(piece));
}

void InlineFormatter::build_items() {
  std::size_t i = 0;
  while (i < chars_.size()) {
    if (ruby_at_[i] != kNone) {
      const std::size_t group = ruby_at_[i];
      build_ruby_item(group);
      i = groups_[group].base_end;
      continue;
    }
    const FlatChar& flat = chars_[i];
    if (flat.kind != FlatChar::Kind::Text) {
      const bool image = flat.kind == FlatChar::Kind::Image;
      items_.push_back(linebreak::Item{
          .kind = image ? linebreak::ItemKind::Atomic : linebreak::ItemKind::ForcedBreak,
          .cp = flat.cp,
          .advance = image ? images_[flat.image].margin_inline() : 0,
          .em = styles_[flat.style].font_size,
          .no_break_before = false});
      sources_.push_back(ItemSource{.run = kNone,
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
    std::size_t end = i;
    std::u32string text;
    while (end < chars_.size() && chars_[end].kind == FlatChar::Kind::Text &&
           chars_[end].style == flat.style && (end == i || ruby_at_[end] == kNone)) {
      text.push_back(chars_[end].cp);
      ++end;
    }
    const std::size_t style_id = flat.style;
    runs_.push_back(
        ShapedRun{.style = style_id,
                  .shaped = engine_->shape(text, text_style_of(styles_[style_id], engine_->map()))});
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
                                    .style = style_id,
                                    .image = kNone,
                                    .ruby = kNone});
    }
    i = end;
  }
}

std::string InlineFormatter::text_of(std::size_t char_begin, std::size_t char_end) const {
  std::string out;
  for (std::size_t i = char_begin; i < char_end && i < chars_.size(); ++i) {
    append_utf8(out, chars_[i].cp);
  }
  return out;
}

// CSS 2.1 §10.8: line-height と FontMetrics から半行間（half-leading）を出し、
// ベースラインより上（ascent + 半行間）と下（descent + 半行間）を広げる。
// 縦書きでは「ベースライン」は行の中心軸なので、上下に line-height の半分ずつ広げる。
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
float InlineFormatter::ruby_above(const RubyPiece& piece) const {
  if (vertical_) {
    return (piece.base_font_size / 2) + piece.rt_font_size;
  }
  return piece.base_ascent + piece.rt_ascent + piece.rt_descent;
}

// ルビ文字のベースライン（縦書きでは中心軸）の block 位置。
// ルビは親文字の block-start 側（横書きは上、縦書きは紙面の右）に付く。block 座標は
// どちらの書字方向でも block-start 側が小さいので（縦書きは paint が
// x = viewport_width − block で右端から引く）、**引く**のが block-start 側になる。
// ruby_above() が空けているのと同じ側であること（逆にすると隣の行に食い込む）。
float InlineFormatter::ruby_baseline(const RubyPiece& piece, float baseline) const {
  if (vertical_) {
    // 中心軸から、親文字の内容領域の半分 + ルビの半分ぶん block-start 側へ
    return baseline - (piece.base_font_size / 2) - (piece.rt_font_size / 2);
  }
  // 親文字の内容領域の上端から、さらにルビの descent ぶん上
  return baseline - piece.base_ascent - piece.rt_descent;
}

Extent InlineFormatter::measure_line(const linebreak::Line& line) {
  Extent extent;
  extend_line_height(kNone, strut_metrics_, extent);  // 支柱は内容によらず全行に参加する
  ++stamp_;  // この行で「もう見た」印。行ごとに配列を作り直さない（#4）
  for (std::size_t i = line.begin; i < line.content_end; ++i) {
    const ItemSource& source = sources_[i];
    if (source.image != kNone) {
      // 横書き: margin-box の下端がベースラインに乗る（CSS の既定の vertical-align）
      // 縦書き: margin-box を中心軸に中央揃えする
      const float span = images_[source.image].margin_block();
      if (vertical_) {
        extent.above = std::max(extent.above, span / 2);
        extent.below = std::max(extent.below, span / 2);
      } else {
        extent.above = std::max(extent.above, span);
      }
      continue;
    }
    if (source.ruby != kNone) {
      const RubyPiece& piece = rubies_[source.ruby];
      if (style_stamp_[piece.base_style] != stamp_) {
        style_stamp_[piece.base_style] = stamp_;
        extend_line_height(piece.base_style, metrics_[piece.base_style], extent);
      }
      extent.above = std::max(extent.above, ruby_above(piece));
      continue;
    }
    if (style_stamp_[source.style] == stamp_) {
      continue;
    }
    style_stamp_[source.style] = stamp_;
    extend_line_height(source.style, metrics_[source.style], extent);
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

// ルビ組: 親文字とルビの短い方を中央に置く（JLREQ の 1:2:1 の配分まではやらない）。
void InlineFormatter::place_ruby(const RubyPiece& piece, float item_start, float advance,
                                 float baseline, FragmentWriter& writer) const {
  writer.close();
  float pen = item_start + ((advance - piece.base_width) / 2);
  for (const BaseSegment& segment : piece.base) {
    const float start = pen;
    writer.add(runs_[segment.run].shaped, segment.glyph_begin, segment.glyph_end, segment.style,
               baseline, text_of(segment.char_begin, segment.char_end), pen);
    pen = start + segment.advance;  // letter-spacing ぶんを足す
    writer.extend_to(pen);
  }
  writer.close();

  float ruby_pen = item_start + ((advance - piece.rt_width) / 2);
  const text::ShapedText& ruby = runs_[piece.rt_run].shaped;
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
  FragmentWriter writer(content, styles_);
  float pen = input_->content_inline_start + alignment.offset;
  for (std::size_t i = line.begin; i < line.content_end; ++i) {
    if (i > line.begin && alignment.justify_share > 0 && opportunities_[i]) {
      pen += alignment.justify_share;  // 禁則で割れない位置には空きを入れない（A13）
    }
    pen += breaks.spacing[i].before;
    const float item_start = pen;
    const ItemSource& source = sources_[i];

    if (source.image != kNone) {
      writer.close();
      place_image(images_[source.image], item_start, baseline, content);
    } else if (source.ruby != kNone) {
      place_ruby(rubies_[source.ruby], item_start, items_[i].advance, baseline, writer);
    } else if (source.run != kNone) {
      float glyph_pen = item_start;
      writer.add(runs_[source.run].shaped, source.glyph_begin, source.glyph_end, source.style,
                 baseline, text_of(source.char_begin, source.char_end), glyph_pen);
    }

    pen = item_start + items_[i].advance + breaks.spacing[i].after;
    writer.extend_to(pen);
    placement_[i] = Placement{.inline_start = item_start, .inline_end = pen};
  }
}

std::size_t InlineFormatter::first_item_at(const linebreak::Line& line,
                                           std::size_t char_index) const {
  const auto begin = sources_.begin() + static_cast<std::ptrdiff_t>(line.begin);
  const auto end = sources_.begin() + static_cast<std::ptrdiff_t>(line.content_end);
  const auto found = std::ranges::lower_bound(begin, end, char_index, {}, &ItemSource::char_begin);
  return static_cast<std::size_t>(found - sources_.begin());
}

// 行と交差する背景スコープだけを見る（#4）。スコープは begin の昇順なので、行が進むのに
// 合わせて active_scopes_ を更新すれば、1 行あたりの仕事は「その行に出る背景の数」で済む。
std::vector<InlineBackground> InlineFormatter::build_backgrounds(const linebreak::Line& line,
                                                                 float baseline) {
  std::vector<InlineBackground> backgrounds;
  if (line.begin >= line.content_end) {
    return backgrounds;  // 中身のない行（強制改行だけの行）には背景も出ない
  }
  const std::size_t char_begin = sources_[line.begin].char_begin;
  const std::size_t char_end = sources_[line.content_end - 1].char_end;
  // この行の先頭より前で終わったスコープを落とす（行の先頭は行ごとに進むので、落とすのは 1 回ずつ）
  std::erase_if(active_scopes_,
                [&](std::size_t index) { return scopes_[index].end <= char_begin; });
  // この行の終わりより前に始まるスコープを入れる（cursor も行とともに 1 方向に進む）
  while (scope_cursor_ < scopes_.size() && scopes_[scope_cursor_].begin < char_end) {
    if (scopes_[scope_cursor_].end > char_begin) {
      active_scopes_.push_back(scope_cursor_);
    }
    ++scope_cursor_;
  }
  engine_->counters().background_probes += active_scopes_.size();

  for (const std::size_t index : active_scopes_) {  // 外側の span が先（描画順）
    const BackgroundScope& scope = scopes_[index];
    const std::size_t first = first_item_at(line, scope.begin);
    const std::size_t after = first_item_at(line, scope.end);
    if (first >= after) {
      continue;  // 交差はしているが、この行にはこのスコープの文字がない
    }
    const std::size_t last = after - 1;
    backgrounds.push_back(InlineBackground{
        .rect =
            LogicalRect{.inline_start = placement_[first].inline_start,
                        .block_start = baseline - scope.start_extent,
                        .inline_size = placement_[last].inline_end - placement_[first].inline_start,
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

linebreak::Config InlineFormatter::config() const {
  linebreak::Config config = engine_->options().line_break;
  config.strictness = strictness_of(input_->block_style->line_break, config.strictness);
  if (input_->block_style->overflow_wrap != style::OverflowWrap::Normal) {
    config.break_anywhere = true;
  }
  return config;
}

Result<void> InlineFormatter::prepare() {
  ++engine_->counters().inline_prepare;
  Collected collected;
  if (const Result<void> result =
          collect(input_->children, *engine_, input_->content_inline_size, collected);
      !result) {
    return result;
  }
  styles_ = std::move(collected.styles);
  scopes_ = std::move(collected.scopes);
  images_ = std::move(collected.images);
  groups_ = std::move(collected.rubies);

  Collapsed collapsed = collapse_whitespace(collected.chars);
  chars_ = std::move(collapsed.chars);
  // 文字の範囲を覚えているもの（背景スコープ・ルビ組）を畳み込み後の添字に移す
  const auto remap = [&collapsed](std::size_t index) {
    return static_cast<std::size_t>(
        std::lower_bound(collapsed.source.begin(), collapsed.source.end(), index) -
        collapsed.source.begin());
  };
  for (BackgroundScope& scope : scopes_) {
    scope.begin = remap(scope.begin);
    scope.end = remap(scope.end);
  }
  ruby_at_.assign(chars_.size(), kNone);
  for (std::size_t i = 0; i < groups_.size(); ++i) {
    groups_[i].base_begin = remap(groups_[i].base_begin);
    groups_[i].base_end = remap(groups_[i].base_end);
    if (groups_[i].base_begin < groups_[i].base_end) {
      ruby_at_[groups_[i].base_begin] = i;
    }
  }

  strut_ = run_style_of(*input_->block_style);
  strut_metrics_ = engine_->metrics(text_style_of(strut_, engine_->map()));
  metrics_.reserve(styles_.size());
  for (const RunStyle& run_style : styles_) {
    metrics_.push_back(engine_->metrics(text_style_of(run_style, engine_->map())));
  }
  build_items();
  return {};
}

Result<Intrinsic> InlineFormatter::intrinsic() {
  if (const Result<void> prepared = prepare(); !prepared) {
    return std::unexpected(prepared.error());
  }
  Intrinsic out;
  if (items_.empty()) {
    return out;
  }
  const linebreak::LineBreaker breaker(config());
  const linebreak::Breaks breaks = breaker.break_lines(items_, linebreak::kUnbounded);
  for (const linebreak::Line& line : breaks.lines) {
    out.max_content = std::max(out.max_content, line.width);
  }
  out.min_content = breaker.min_content_width(items_);
  return out;
}

Result<std::vector<LineBox>> InlineFormatter::run() {
  if (const Result<void> prepared = prepare(); !prepared) {
    return std::unexpected(prepared.error());
  }
  if (items_.empty()) {
    return std::vector<LineBox>{};  // 空の IFC は行ボックスを作らない（高さ 0）
  }

  // (d) 行分割器。Config は Options を土台に、このブロックの CSS で上書きする
  const linebreak::LineBreaker breaker(config());
  const linebreak::Breaks breaks = breaker.break_lines(items_, input_->content_inline_size);
  if (input_->block_style->text_align == style::TextAlign::Justify) {
    opportunities_ = breaker.break_opportunities(items_);
  } else {
    opportunities_.assign(items_.size(), false);
  }
  // 行の構築の作業バッファは、行ごとではなく段落で 1 回だけ確保する（#4）
  placement_.assign(items_.size(), Placement{});
  style_stamp_.assign(styles_.size(), 0);
  engine_->counters().line_scratch += items_.size() + styles_.size();

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
  return InlineFormatter(input, engine).run();
}

Result<Intrinsic> inline_intrinsic(const InlineInput& input, LayoutEngine& engine) {
  return InlineFormatter(input, engine).intrinsic();
}

}  // namespace shashoku::layout
