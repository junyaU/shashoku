#include "layout/inline_collect.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/utf8.hpp"
#include "layout/east_asian_width.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::layout {
namespace {

using style::StyledNode;

// CSS Text 3 §4.1.1 の collapsible white space（white-space: normal 固定）。
bool is_collapsible_space(char32_t cp) {
  return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\f';
}

// CSS Text 3 §4.1.3 の segment break（ソース中の改行）。A14 の対象。
bool is_segment_break(char32_t cp) { return cp == U'\n' || cp == U'\r'; }

// 空白の畳み込みから見たときの「この位置の文字」。画像は U+FFFC（置換文字）扱いにして、
// 全角ではない = 前後の改行は空白になる（CSS の既定と同じ）。
char32_t effective_cp(const FlatChar& flat) {
  return flat.kind == FlatChar::Kind::Image ? U'￼' : flat.cp;
}

// 「最も外側の `white-space: nowrap` 祖先」の識別子（A58。0 = nowrap の外）。
// CSS Text 3 §5.1 は「2 つのクラスタの最も近い共通の祖先が nowrap なら、その間に
// ソフトな分割機会は無い」と定める。ここでは**最も外側の nowrap 祖先**を識別子で持ち、
// 隣り合うクラスタの識別子が同じ非 0 なら分割機会を消す（= 共通の祖先も nowrap）。
// この簡略化では nowrap の中の `white-space: normal` が分割を再開しない（A58 に明記）。
//
// 識別子は木を前順に辿った順に 1 から振るので決定的。IFC ごとに振り直してよい
// （比べるのは同じ段落の中のクラスタだけ）。
struct NowrapScope {
  std::size_t id = 0;  // この要素の中の文字が属する並び（0 = nowrap の外）
  std::size_t* next = nullptr;  // 次に振る識別子

  // 子孫に渡す識別子。すでに nowrap の中なら外側のものを保つ（**最も外側**を採る）。
  [[nodiscard]] NowrapScope enter(const style::ComputedStyle& style) const {
    if (id != 0 || style.white_space != style::WhiteSpace::Nowrap) {
      return *this;
    }
    const std::size_t assigned = (*next)++;
    return NowrapScope{.id = assigned, .next = next};
  }
};

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
                     NowrapScope nowrap, Collected& out);
Result<void> collect_element(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                             NowrapScope nowrap, Collected& out);

// 文字の属性を登録する。位置の層（A31）には「その文字を含むノードの先頭」を入れる:
// 豆腐の警告と --dump-stage box に出すためだけの層で、見た目にもシェーピングにも効かない。
std::size_t style_index(Collected& out, const StyledNode& node, const LayoutEngine& engine,
                        NowrapScope nowrap) {
  return out.styles.intern(node.style, engine.map().direction(), node.location, nowrap.id);
}

// 文字を 1 つも持たなかったインラインボックスを覚えておく（CSS 2.1 §10.8。issue #23）。
// 子を集める前後で chars.size() が変わらなかったときだけ呼ぶ。char_pos はそのときの
// chars.size() なので、この列は自然に昇順になる（入れ子は内側が先に積まれる）。
void push_empty_box(Collected& out, const StyledNode& node, const LayoutEngine& engine,
                    NowrapScope nowrap, std::size_t char_pos) {
  out.empty_boxes.push_back(EmptyInlineBox{
      .style = style_index(out, node, engine, nowrap), .char_pos = char_pos, .item = kNone});
}

Result<void> collect_image(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                           NowrapScope nowrap, Collected& out) {
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
                               .style = style_index(out, node, engine, nowrap),
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
                          NowrapScope nowrap, Collected& out) {
  std::size_t base_begin = out.chars.size();
  for (const StyledNode& child : node.children) {
    if (child.type == StyledNode::Type::Text) {
      const Result<std::u32string> text = decode_utf8(child.text);
      if (!text) {
        return std::unexpected(text.error());
      }
      push_text(out, *text, style_index(out, child, engine, nowrap.enter(child.style)));
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
      out.rubies.push_back(
          RubyGroup{.base_begin = base_begin,
                    .base_end = out.chars.size(),
                    .rt_style = style_index(out, child, engine, nowrap.enter(child.style)),
                    .rt_text = std::move(*ruby)});
      base_begin = out.chars.size();
      continue;
    }
    if (child.tag == "ruby" || child.tag == "img" || child.tag == "br") {
      return fail(ErrorKind::UnsupportedLayout,
                  "<" + child.tag + "> is not supported inside <ruby>", child.location);
    }
    if (const Result<void> result = collect_element(child, engine, percent_basis, nowrap, out);
        !result) {
      return result;
    }
  }
  return {};
}

// インライン box（span など）1 つ。
Result<void> collect_element(const StyledNode& node, LayoutEngine& engine, float percent_basis,
                             NowrapScope parent_nowrap, Collected& out) {
  // この要素と子孫が属する nowrap の並び（A58）。要素の計算値を見るのはここだけにする
  const NowrapScope nowrap = parent_nowrap.enter(node.style);
  if (node.style.display != style::Display::Inline) {
    return fail(ErrorKind::UnsupportedLayout,
                "block-level box inside an inline box is not supported (<" + node.tag + ">)",
                node.location);
  }
  if (node.tag == "br") {
    out.chars.push_back(FlatChar{.cp = U'\n',
                                 .kind = FlatChar::Kind::ForcedBreak,
                                 .style = style_index(out, node, engine, nowrap),
                                 .image = kNone});
    return {};
  }
  if (node.tag == "img") {
    return collect_image(node, engine, percent_basis, nowrap, out);
  }
  if (node.tag == "ruby") {
    const std::size_t begin = out.chars.size();
    if (const Result<void> result = collect_ruby(node, engine, percent_basis, nowrap, out);
        !result) {
      return result;
    }
    if (out.chars.size() == begin) {
      // 中身が空の <ruby> は空の span と同じ
      push_empty_box(out, node, engine, nowrap, begin);
    }
    return {};
  }
  if (node.tag == "rt") {
    return fail(ErrorKind::UnsupportedLayout, "<rt> is only allowed inside <ruby>", node.location);
  }
  const std::size_t begin = out.chars.size();
  // background-color があれば、行ごとの背景を出すために文字の範囲を覚える
  std::size_t scope = kNone;
  if (!node.style.background_color.transparent()) {
    const Result<text::FontMetrics> metrics =
        engine.metrics(shaping_style_of(node.style, engine.map().direction()));
    if (!metrics) {
      return std::unexpected(metrics.error());
    }
    const bool vertical = engine.map().vertical();
    const float font_size = node.style.font_size;
    out.scopes.push_back(
        BackgroundScope{.color = node.style.background_color,
                        .begin = begin,
                        .end = 0,
                        .start_extent = vertical ? font_size / 2 : metrics->ascent,
                        .size = vertical ? font_size : metrics->ascent + metrics->descent});
    scope = out.scopes.size() - 1;
  }
  if (const Result<void> result = collect(node.children, engine, percent_basis, nowrap, out);
      !result) {
    return result;
  }
  if (scope != kNone) {
    out.scopes[scope].end = out.chars.size();
  }
  if (out.chars.size() == begin) {
    push_empty_box(out, node, engine, nowrap, begin);
  }
  return {};
}

// 木を辿って 1 本にほどく。空白の畳み込みはまだしない（ノード境界をまたいで
// 判断する必要があるので、平らにしてから 1 回で処理する）。
Result<void> collect(std::span<const StyledNode> nodes, LayoutEngine& engine, float percent_basis,
                     NowrapScope nowrap, Collected& out) {
  for (const StyledNode& node : nodes) {
    if (node.type == StyledNode::Type::Text) {
      const Result<std::u32string> text = decode_utf8(node.text);
      if (!text) {
        return std::unexpected(text.error());
      }
      // テキストノードは親の計算値を持つ（nowrap も継承済み）ので、並びは親のものそのまま
      push_text(out, *text, style_index(out, node, engine, nowrap));
      continue;
    }
    if (node.style.display == style::Display::None) {
      continue;  // ② が落としているはずだが、来たら無視する
    }
    if (const Result<void> result = collect_element(node, engine, percent_basis, nowrap, out);
        !result) {
      return result;
    }
  }
  return {};
}

struct Collapsed {
  std::vector<FlatChar> chars;
  std::vector<std::size_t> source;  // chars[i] の畳み込み前の添字（狭義単調増加）
};

// 空白の畳み込み（white-space: normal）。
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

}  // namespace

Result<Collected> collect_inline(const InlineInput& input, LayoutEngine& engine) {
  Collected out;
  // IFC を持つブロック自身に nowrap が書かれていれば、段落全体が 1 つの並びになる（A58）。
  // 無名ブロックのときは親ブロックのスタイルなので、継承した nowrap もここで拾える
  std::size_t next_nowrap = 1;
  const NowrapScope root = NowrapScope{.id = 0, .next = &next_nowrap}.enter(*input.block_style);
  if (const Result<void> result =
          collect(input.children, engine, input.content_inline_size, root, out);
      !result) {
    return std::unexpected(result.error());
  }

  Collapsed collapsed = collapse_whitespace(out.chars);
  out.chars = std::move(collapsed.chars);
  // 文字の範囲を覚えているもの（背景スコープ・ルビ組）を畳み込み後の添字に移す
  const auto remap = [&collapsed](std::size_t index) {
    return static_cast<std::size_t>(
        std::lower_bound(collapsed.source.begin(), collapsed.source.end(), index) -
        collapsed.source.begin());
  };
  for (BackgroundScope& scope : out.scopes) {
    scope.begin = remap(scope.begin);
    scope.end = remap(scope.end);
  }
  for (EmptyInlineBox& box : out.empty_boxes) {
    box.char_pos = remap(box.char_pos);
  }
  out.ruby_at.assign(out.chars.size(), kNone);
  for (std::size_t i = 0; i < out.rubies.size(); ++i) {
    out.rubies[i].base_begin = remap(out.rubies[i].base_begin);
    out.rubies[i].base_end = remap(out.rubies[i].base_end);
    if (out.rubies[i].base_begin < out.rubies[i].base_end) {
      out.ruby_at[out.rubies[i].base_begin] = i;
    }
  }
  return out;
}

}  // namespace shashoku::layout
