#include "style/resolver.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "core/result.hpp"
#include "html/dom.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"
#include "style/css_chars.hpp"
#include "style/css_keywords.hpp"
#include "style/css_parser.hpp"
#include "style/css_tokens.hpp"
#include "style/declaration.hpp"
#include "style/ua_stylesheet.hpp"

namespace shashoku::style {
namespace {

// 計算値のうち ComputedStyle に出てこないもの（border-style など）と、
// カスケード中にだけ要る記録（作者が書いたボックス系プロパティ、writing-mode の宣言）。
struct StyleState {
  ComputedStyle computed;
  BorderStyle border_style = BorderStyle::None;
  float border_width_length = 3;  // border-width の初期値 medium
  bool border_color_is_current = true;

  // `display: inline` への箱の指定を弾くための記録（ARCHITECTURE.md §3.7 の最後）
  std::optional<PropertyId> box_property;
  SourceLocation box_property_location;

  // writing-mode の規則（A1）を検査するための記録
  bool writing_mode_declared = false;
  SourceLocation writing_mode_location;
};

const ComputedStyle& initial_computed() {
  static const ComputedStyle initial;  // 既定値は契約ヘッダ computed_style.hpp が正
  return initial;
}

// ---- 継承 --------------------------------------------------------------------

// computed_style.hpp で「継承する」とした群だけを親から引き継ぐ。
void inherit_text(ComputedStyle& child, const ComputedStyle& parent) {
  child.color = parent.color;
  child.font_size = parent.font_size;
  child.font_family = parent.font_family;
  child.font_weight = parent.font_weight;
  child.line_height = parent.line_height;
  child.letter_spacing = parent.letter_spacing;
  child.text_align = parent.text_align;
  child.line_break = parent.line_break;
  child.overflow_wrap = parent.overflow_wrap;
  child.writing_mode = parent.writing_mode;
}

// ---- 計算値化 ----------------------------------------------------------------

float resolve_length(const SpecLength& length, float em_base) {
  return length.em ? length.value * em_base : length.value;
}

Dimension resolve_dimension(const SpecDimension& dimension, float em_base) {
  switch (dimension.kind) {
    case SpecDimension::Kind::Auto:
      return Dimension::auto_();
    case SpecDimension::Kind::Length:
      return Dimension::px(resolve_length(dimension.length, em_base));
    case SpecDimension::Kind::Percent:
      return Dimension::percent(dimension.percent);
  }
  return Dimension::auto_();
}

LineHeight resolve_line_height(const SpecLineHeight& value, float em_base) {
  switch (value.kind) {
    case SpecLineHeight::Kind::Normal:
      return LineHeight{.kind = LineHeight::Kind::Normal};
    case SpecLineHeight::Kind::Number:
      return LineHeight{.kind = LineHeight::Kind::Number, .value = value.number};
    case SpecLineHeight::Kind::Length:
      return LineHeight{.kind = LineHeight::Kind::Px,
                        .value = resolve_length(value.length, em_base)};
  }
  return LineHeight{};
}

// 宣言は自分で作っているので型が食い違うのは shashoku 側のバグ。
// 例外は投げない約束なので、取り出せなければ既定値にする（Debug では assert で落とす）。
template <class T>
T take(const SpecifiedValue& value) {
  const T* ptr = std::get_if<T>(&value);
  assert(ptr != nullptr);
  return ptr != nullptr ? *ptr : T{};
}

// ---- 宣言の適用 --------------------------------------------------------------

void apply_inherit(PropertyId property, const StyleState& parent, StyleState& state) {
  ComputedStyle& s = state.computed;
  const ComputedStyle& p = parent.computed;
  switch (property) {
    case PropertyId::Display:
      s.display = p.display;
      return;
    case PropertyId::Width:
      s.width = p.width;
      return;
    case PropertyId::Height:
      s.height = p.height;
      return;
    case PropertyId::MarginTop:
      s.margin.top = p.margin.top;
      return;
    case PropertyId::MarginRight:
      s.margin.right = p.margin.right;
      return;
    case PropertyId::MarginBottom:
      s.margin.bottom = p.margin.bottom;
      return;
    case PropertyId::MarginLeft:
      s.margin.left = p.margin.left;
      return;
    case PropertyId::PaddingTop:
      s.padding.top = p.padding.top;
      return;
    case PropertyId::PaddingRight:
      s.padding.right = p.padding.right;
      return;
    case PropertyId::PaddingBottom:
      s.padding.bottom = p.padding.bottom;
      return;
    case PropertyId::PaddingLeft:
      s.padding.left = p.padding.left;
      return;
    case PropertyId::BorderWidth:
      state.border_width_length = p.border_width;
      return;
    case PropertyId::BorderStyle:
      state.border_style = parent.border_style;
      return;
    case PropertyId::BorderColor:
      s.border_color = p.border_color;
      state.border_color_is_current = false;
      return;
    case PropertyId::BorderRadius:
      s.border_radius = p.border_radius;
      return;
    case PropertyId::BackgroundColor:
      s.background_color = p.background_color;
      return;
    case PropertyId::FlexDirection:
      s.flex_direction = p.flex_direction;
      return;
    case PropertyId::JustifyContent:
      s.justify_content = p.justify_content;
      return;
    case PropertyId::AlignItems:
      s.align_items = p.align_items;
      return;
    case PropertyId::RowGap:
      s.row_gap = p.row_gap;
      return;
    case PropertyId::ColumnGap:
      s.column_gap = p.column_gap;
      return;
    case PropertyId::FlexGrow:
      s.flex_grow = p.flex_grow;
      return;
    case PropertyId::FlexShrink:
      s.flex_shrink = p.flex_shrink;
      return;
    case PropertyId::FlexBasis:
      s.flex_basis = p.flex_basis;
      return;
    case PropertyId::Color:
      s.color = p.color;
      return;
    case PropertyId::FontSize:
      s.font_size = p.font_size;
      return;
    case PropertyId::FontFamily:
      s.font_family = p.font_family;
      return;
    case PropertyId::FontWeight:
      s.font_weight = p.font_weight;
      return;
    case PropertyId::LineHeight:
      s.line_height = p.line_height;
      return;
    case PropertyId::LetterSpacing:
      s.letter_spacing = p.letter_spacing;
      return;
    case PropertyId::TextAlign:
      s.text_align = p.text_align;
      return;
    case PropertyId::LineBreak:
      s.line_break = p.line_break;
      return;
    case PropertyId::OverflowWrap:
      s.overflow_wrap = p.overflow_wrap;
      return;
    case PropertyId::WritingMode:
      s.writing_mode = p.writing_mode;
      return;
  }
}

void apply_initial(PropertyId property, StyleState& state) {
  ComputedStyle& s = state.computed;
  const ComputedStyle& i = initial_computed();
  switch (property) {
    case PropertyId::Display:
      s.display = i.display;
      return;
    case PropertyId::Width:
      s.width = i.width;
      return;
    case PropertyId::Height:
      s.height = i.height;
      return;
    case PropertyId::MarginTop:
      s.margin.top = i.margin.top;
      return;
    case PropertyId::MarginRight:
      s.margin.right = i.margin.right;
      return;
    case PropertyId::MarginBottom:
      s.margin.bottom = i.margin.bottom;
      return;
    case PropertyId::MarginLeft:
      s.margin.left = i.margin.left;
      return;
    case PropertyId::PaddingTop:
      s.padding.top = i.padding.top;
      return;
    case PropertyId::PaddingRight:
      s.padding.right = i.padding.right;
      return;
    case PropertyId::PaddingBottom:
      s.padding.bottom = i.padding.bottom;
      return;
    case PropertyId::PaddingLeft:
      s.padding.left = i.padding.left;
      return;
    case PropertyId::BorderWidth:
      state.border_width_length = 3;  // medium
      return;
    case PropertyId::BorderStyle:
      state.border_style = BorderStyle::None;
      return;
    case PropertyId::BorderColor:
      state.border_color_is_current = true;  // currentColor
      return;
    case PropertyId::BorderRadius:
      s.border_radius = i.border_radius;
      return;
    case PropertyId::BackgroundColor:
      s.background_color = i.background_color;
      return;
    case PropertyId::FlexDirection:
      s.flex_direction = i.flex_direction;
      return;
    case PropertyId::JustifyContent:
      s.justify_content = i.justify_content;
      return;
    case PropertyId::AlignItems:
      s.align_items = i.align_items;
      return;
    case PropertyId::RowGap:
      s.row_gap = i.row_gap;
      return;
    case PropertyId::ColumnGap:
      s.column_gap = i.column_gap;
      return;
    case PropertyId::FlexGrow:
      s.flex_grow = i.flex_grow;
      return;
    case PropertyId::FlexShrink:
      s.flex_shrink = i.flex_shrink;
      return;
    case PropertyId::FlexBasis:
      s.flex_basis = i.flex_basis;
      return;
    case PropertyId::Color:
      s.color = i.color;
      return;
    case PropertyId::FontSize:
      s.font_size = i.font_size;
      return;
    case PropertyId::FontFamily:
      s.font_family = i.font_family;
      return;
    case PropertyId::FontWeight:
      s.font_weight = i.font_weight;
      return;
    case PropertyId::LineHeight:
      s.line_height = i.line_height;
      return;
    case PropertyId::LetterSpacing:
      s.letter_spacing = i.letter_spacing;
      return;
    case PropertyId::TextAlign:
      s.text_align = i.text_align;
      return;
    case PropertyId::LineBreak:
      s.line_break = i.line_break;
      return;
    case PropertyId::OverflowWrap:
      s.overflow_wrap = i.overflow_wrap;
      return;
    case PropertyId::WritingMode:
      s.writing_mode = i.writing_mode;
      return;
  }
}

void apply_value(PropertyId property, const SpecifiedValue& value, StyleState& state,
                 float em_base) {
  ComputedStyle& s = state.computed;
  switch (property) {
    case PropertyId::Display:
      s.display = take<Display>(value);
      return;
    case PropertyId::Width:
      s.width = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::Height:
      s.height = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::MarginTop:
      s.margin.top = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::MarginRight:
      s.margin.right = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::MarginBottom:
      s.margin.bottom = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::MarginLeft:
      s.margin.left = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::PaddingTop:
      s.padding.top = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::PaddingRight:
      s.padding.right = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::PaddingBottom:
      s.padding.bottom = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::PaddingLeft:
      s.padding.left = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::BorderWidth:
      state.border_width_length = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::BorderStyle:
      state.border_style = take<BorderStyle>(value);
      return;
    case PropertyId::BorderColor: {
      const auto color = take<SpecColor>(value);
      state.border_color_is_current = color.current_color;
      s.border_color = color.color;
      return;
    }
    case PropertyId::BorderRadius:
      s.border_radius = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::BackgroundColor:
      s.background_color = take<SpecColor>(value).color;
      return;
    case PropertyId::FlexDirection:
      s.flex_direction = take<FlexDirection>(value);
      return;
    case PropertyId::JustifyContent:
      s.justify_content = take<JustifyContent>(value);
      return;
    case PropertyId::AlignItems:
      s.align_items = take<AlignItems>(value);
      return;
    case PropertyId::RowGap:
      s.row_gap = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::ColumnGap:
      s.column_gap = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::FlexGrow:
      s.flex_grow = take<SpecNumber>(value).value;
      return;
    case PropertyId::FlexShrink:
      s.flex_shrink = take<SpecNumber>(value).value;
      return;
    case PropertyId::FlexBasis:
      s.flex_basis = resolve_dimension(take<SpecDimension>(value), em_base);
      return;
    case PropertyId::Color:
      s.color = take<SpecColor>(value).color;
      return;
    case PropertyId::FontSize:
      s.font_size = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::FontFamily:
      s.font_family = take<SpecFontFamily>(value).names;
      return;
    case PropertyId::FontWeight:
      s.font_weight = take<SpecWeight>(value).value;
      return;
    case PropertyId::LineHeight:
      s.line_height = resolve_line_height(take<SpecLineHeight>(value), em_base);
      return;
    case PropertyId::LetterSpacing:
      s.letter_spacing = resolve_length(take<SpecLength>(value), em_base);
      return;
    case PropertyId::TextAlign:
      s.text_align = take<TextAlign>(value);
      return;
    case PropertyId::LineBreak:
      s.line_break = take<LineBreak>(value);
      return;
    case PropertyId::OverflowWrap:
      s.overflow_wrap = take<OverflowWrap>(value);
      return;
    case PropertyId::WritingMode:
      s.writing_mode = take<WritingMode>(value);
      return;
  }
}

// `display: inline` に指定してはいけない箱のプロパティ（ARCHITECTURE.md §3.7 の最後）。
bool is_box_property(PropertyId property) {
  switch (property) {
    case PropertyId::Width:
    case PropertyId::Height:
    case PropertyId::MarginTop:
    case PropertyId::MarginRight:
    case PropertyId::MarginBottom:
    case PropertyId::MarginLeft:
    case PropertyId::PaddingTop:
    case PropertyId::PaddingRight:
    case PropertyId::PaddingBottom:
    case PropertyId::PaddingLeft:
    case PropertyId::BorderWidth:
    case PropertyId::BorderStyle:
    case PropertyId::BorderColor:
    case PropertyId::BorderRadius:
      return true;
    default:
      return false;
  }
}

// 検査に使う「作者が書いたかどうか」を覚える。UA スタイルシート由来は数えない。
void record_author_declaration(const Declaration& declaration, Origin origin, StyleState& state) {
  if (origin == Origin::UserAgent) {
    return;
  }
  if (declaration.property == PropertyId::WritingMode) {
    // `writing-mode: inherit` は親と同じ値になるだけなので「変更」ではない
    state.writing_mode_declared = declaration.global != GlobalKeyword::Inherit;
    state.writing_mode_location = declaration.location;
    return;
  }
  if (is_box_property(declaration.property) && !state.box_property) {
    state.box_property = declaration.property;
    state.box_property_location = declaration.location;
  }
}

void apply_one(const Declaration& declaration, Origin origin, const StyleState& parent,
               StyleState& state, float em_base) {
  record_author_declaration(declaration, origin, state);
  switch (declaration.global) {
    case GlobalKeyword::Inherit:
      apply_inherit(declaration.property, parent, state);
      return;
    case GlobalKeyword::Initial:
      apply_initial(declaration.property, state);
      return;
    case GlobalKeyword::None:
      apply_value(declaration.property, declaration.value, state, em_base);
      return;
  }
}

// ---- カスケードの並べ替え -----------------------------------------------------

struct MatchedDeclaration {
  Origin origin = Origin::UserAgent;
  Specificity specificity;
  std::uint32_t order = 0;
  const Declaration* declaration = nullptr;
};

bool less_important(const MatchedDeclaration& a, const MatchedDeclaration& b) {
  return std::tie(a.origin, a.specificity, a.order) < std::tie(b.origin, b.specificity, b.order);
}

struct ElementInfo {
  std::string_view tag;
  std::vector<std::string_view> classes;
  std::string_view id;
};

ElementInfo element_info(const html::Node& node) {
  ElementInfo info;
  info.tag = node.tag;
  if (const html::Attribute* id = node.find_attr("id"); id != nullptr) {
    info.id = id->value;
  }
  if (const html::Attribute* klass = node.find_attr("class"); klass != nullptr) {
    std::string_view rest = klass->value;
    while (!rest.empty()) {
      while (!rest.empty() && is_css_space(rest.front())) {
        rest.remove_prefix(1);
      }
      std::size_t end = 0;
      while (end < rest.size() && !is_css_space(rest[end])) {
        ++end;
      }
      if (end > 0) {
        info.classes.push_back(rest.substr(0, end));
      }
      rest.remove_prefix(end);
    }
  }
  return info;
}

bool matches(const Selector& selector, const ElementInfo& info) {
  if (!selector.tag.empty() && selector.tag != info.tag) {
    return false;
  }
  if (!selector.id.empty() && selector.id != info.id) {
    return false;
  }
  return std::ranges::all_of(selector.classes, [&info](const std::string& name) {
    return std::ranges::find(info.classes, name) != info.classes.end();
  });
}

void collect_matches(const Stylesheet& sheet, const ElementInfo& info, Origin origin,
                     std::vector<MatchedDeclaration>& out) {
  for (const Rule& rule : sheet) {
    std::optional<Specificity> best;
    for (const Selector& selector : rule.selectors) {
      if (matches(selector, info) && (!best || *best < selector.specificity)) {
        best = selector.specificity;
      }
    }
    if (!best) {
      continue;
    }
    const Specificity specificity = best.value_or(Specificity{});
    for (const Declaration& declaration : rule.declarations) {
      out.push_back(MatchedDeclaration{.origin = origin,
                                       .specificity = specificity,
                                       .order = rule.order,
                                       .declaration = &declaration});
    }
  }
}

// ---- img の属性 --------------------------------------------------------------

std::optional<float> parse_attribute_number(std::string_view text) {
  const Result<std::vector<ValueToken>> tokens = tokenize_value(text, SourceLocation{});
  if (!tokens || tokens->size() != 1) {
    return std::nullopt;
  }
  const ValueToken& token = tokens->front();
  if (token.kind != ValueToken::Kind::Number || !std::isfinite(static_cast<float>(token.number)) ||
      token.number < 0) {
    return std::nullopt;
  }
  return static_cast<float>(token.number);
}

Result<void> read_image_attributes(const html::Node& node, StyledNode& styled) {
  const html::Attribute* src = node.find_attr("src");
  if (src == nullptr) {
    return fail(ErrorKind::UnsupportedValue, "`<img>` requires a `src` attribute", node.location);
  }
  styled.image_src = src->value;
  for (const std::string_view name : {"width", "height"}) {
    const html::Attribute* attr = node.find_attr(name);
    if (attr == nullptr) {
      continue;
    }
    const std::optional<float> value = parse_attribute_number(attr->value);
    if (!value) {
      return fail(ErrorKind::UnsupportedValue,
                  std::format("`<img {}=\"{}\">` is not supported (the attribute must be a "
                              "non-negative number of px, without a unit)",
                              name, attr->value),
                  attr->location);
    }
    if (name == "width") {
      styled.attr_width = value;
    } else {
      styled.attr_height = value;
    }
  }
  return {};
}

// ---- 木の構築 ----------------------------------------------------------------

class Resolver {
 public:
  explicit Resolver(std::size_t max_style_rules) : max_style_rules_(max_style_rules) {}

  Result<void> load(const html::Node& root);
  Result<StyledNode> build(const html::Node& root);

 private:
  [[nodiscard]] Result<StyleState> cascade(const html::Node& node, const StyleState& parent) const;
  [[nodiscard]] Result<WritingMode> document_writing_mode(const html::Node& root,
                                                          const StyleState& root_state) const;
  Result<void> build_children(const html::Node& node, const StyleState& state,
                              std::vector<StyledNode>& out) const;
  [[nodiscard]] Result<std::optional<StyledNode>> build_element(const html::Node& node,
                                                                const StyleState& parent) const;

  std::size_t max_style_rules_ = kMaxStyleRules;
  Stylesheet ua_;
  Stylesheet author_;
};

// `<style>` 要素は木のどこにあってもよく、文書全体に効く。複数あれば出現順に連結する。
// NOLINTNEXTLINE(misc-no-recursion): DOM は木なので前順の再帰で辿る
Result<void> collect_author_css(const html::Node& node, std::uint32_t& order, Stylesheet& out,
                                std::size_t max_rules) {
  if (node.type == html::Node::Type::Element && node.tag == "style") {
    for (const html::Node& child : node.children) {
      if (child.type != html::Node::Type::Text) {
        continue;
      }
      if (Result<void> parsed = parse_stylesheet(child.text, child.location, order, out); !parsed) {
        return parsed;
      }
      // セレクタの照合は「規則数 x 要素数」なので、規則の数そのものに上限が要る（A21）。
      if (out.size() > max_rules) {
        return fail(ErrorKind::LimitExceeded,
                    std::format("the stylesheet has {} rules, which exceeds the limit of {} "
                                "(raise RenderLimits::style_rules to allow it)",
                                out.size(), max_rules),
                    child.location);
      }
    }
    return {};
  }
  for (const html::Node& child : node.children) {
    if (Result<void> collected = collect_author_css(child, order, out, max_rules); !collected) {
      return collected;
    }
  }
  return {};
}

Result<void> Resolver::load(const html::Node& root) {
  Result<Stylesheet> ua = build_ua_stylesheet();
  if (!ua) {
    return std::unexpected(ua.error());
  }
  ua_ = *std::move(ua);
  std::uint32_t order = 0;
  return collect_author_css(root, order, author_, max_style_rules_);
}

Result<StyleState> Resolver::cascade(const html::Node& node, const StyleState& parent) const {
  std::vector<Declaration> inline_declarations;
  if (const html::Attribute* attr = node.find_attr("style"); attr != nullptr) {
    Result<std::vector<Declaration>> parsed = parse_inline_style(attr->value, attr->location);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    inline_declarations = *std::move(parsed);
  }

  const ElementInfo info = element_info(node);
  std::vector<MatchedDeclaration> matched;
  collect_matches(ua_, info, Origin::UserAgent, matched);
  collect_matches(author_, info, Origin::Author, matched);
  for (const Declaration& declaration : inline_declarations) {
    matched.push_back(MatchedDeclaration{.origin = Origin::Inline,
                                         .specificity = Specificity{},
                                         .order = 0,
                                         .declaration = &declaration});
  }
  std::ranges::stable_sort(matched, less_important);

  StyleState state;
  inherit_text(state.computed, parent.computed);

  // font-size を先に決める: em は自分の font-size で解決するが、font-size 自身の em だけは
  // 親の font-size で解決する（ARCHITECTURE.md §3.7）。
  for (const MatchedDeclaration& entry : matched) {
    if (entry.declaration->property == PropertyId::FontSize) {
      apply_one(*entry.declaration, entry.origin, parent, state, parent.computed.font_size);
    }
  }
  for (const MatchedDeclaration& entry : matched) {
    if (entry.declaration->property != PropertyId::FontSize) {
      apply_one(*entry.declaration, entry.origin, parent, state, state.computed.font_size);
    }
  }

  // border-style: none なら枠線の幅は 0、border-color の currentColor は自分の color
  state.computed.border_width =
      state.border_style == BorderStyle::None ? 0 : state.border_width_length;
  if (state.border_color_is_current) {
    state.computed.border_color = state.computed.color;
  }
  return state;
}

Result<void> validate(const html::Node& node, const StyleState& state, const StyleState& parent) {
  if (state.writing_mode_declared && state.computed.writing_mode != parent.computed.writing_mode) {
    return fail(
        ErrorKind::UnsupportedLayout,
        std::format("`writing-mode: {}` differs from the inherited `{}`; the whole "
                    "document uses one writing mode and only top-level elements may set "
                    "it (ARCHITECTURE.md A1)",
                    to_css(state.computed.writing_mode), to_css(parent.computed.writing_mode)),
        state.writing_mode_location);
  }
  if (state.computed.display == Display::Inline && node.tag != "img" && state.box_property) {
    return fail(ErrorKind::UnsupportedLayout,
                std::format("`{}` is not supported on an inline element (`display: inline`); only "
                            "`<img>` takes box properties while inline",
                            to_css(*state.box_property)),
                state.box_property_location);
  }
  return {};
}

// writing-mode は文書全体で 1 つ（A1）。トップレベル要素の指定だけを見て文書の値を決める。
Result<WritingMode> Resolver::document_writing_mode(const html::Node& root,
                                                    const StyleState& root_state) const {
  std::optional<WritingMode> chosen;
  for (const html::Node& child : root.children) {
    if (child.type != html::Node::Type::Element || child.tag == "style" || child.tag == "rp") {
      continue;
    }
    Result<StyleState> state = cascade(child, root_state);
    if (!state) {
      return std::unexpected(state.error());
    }
    if (!state->writing_mode_declared) {
      continue;
    }
    if (chosen && *chosen != state->computed.writing_mode) {
      return fail(ErrorKind::UnsupportedLayout,
                  std::format("top-level elements disagree about `writing-mode` (`{}` and `{}`); "
                              "the whole document must use one writing mode (ARCHITECTURE.md A1)",
                              to_css(*chosen), to_css(state->computed.writing_mode)),
                  state->writing_mode_location);
    }
    chosen = state->computed.writing_mode;
  }
  return chosen.value_or(WritingMode::HorizontalTb);
}

// Text ノードは親要素の継承プロパティを持ち、箱のプロパティは初期値に戻す（computed_style.hpp）。
StyledNode make_text_node(const html::Node& node, const ComputedStyle& parent) {
  StyledNode styled;
  styled.type = StyledNode::Type::Text;
  styled.text = node.text;
  styled.location = node.location;
  inherit_text(styled.style, parent);
  styled.style.display = Display::Inline;
  styled.style.border_color = styled.style.color;  // currentColor
  return styled;
}

// NOLINTNEXTLINE(misc-no-recursion): DOM は木なので前順の再帰で辿る
Result<void> Resolver::build_children(const html::Node& node, const StyleState& state,
                                      std::vector<StyledNode>& out) const {
  for (const html::Node& child : node.children) {
    if (child.type == html::Node::Type::Text) {
      out.push_back(make_text_node(child, state.computed));
      continue;
    }
    if (child.tag == "style" || child.tag == "rp") {
      continue;  // 木に現れない（computed_style.hpp）。中身は CSS / ルビの代替表記
    }
    Result<std::optional<StyledNode>> built = build_element(child, state);
    if (!built) {
      return std::unexpected(built.error());
    }
    if (*built) {
      out.push_back(**std::move(built));
    }
  }
  return {};
}

// NOLINTNEXTLINE(misc-no-recursion): build_children との相互再帰
Result<std::optional<StyledNode>> Resolver::build_element(const html::Node& node,
                                                          const StyleState& parent) const {
  Result<StyleState> state = cascade(node, parent);
  if (!state) {
    return std::unexpected(state.error());
  }
  if (Result<void> checked = validate(node, *state, parent); !checked) {
    return std::unexpected(checked.error());
  }

  StyledNode styled;
  styled.type = StyledNode::Type::Element;
  styled.tag = node.tag;
  styled.style = state->computed;
  styled.location = node.location;
  if (node.tag == "img") {
    if (Result<void> attrs = read_image_attributes(node, styled); !attrs) {
      return std::unexpected(attrs.error());
    }
  }
  // display: none の部分木も検査はする（黙って落とさない）。木には出さない
  if (Result<void> children = build_children(node, *state, styled.children); !children) {
    return std::unexpected(children.error());
  }
  if (state->computed.display == Display::None) {
    return std::nullopt;
  }
  return styled;
}

Result<StyledNode> Resolver::build(const html::Node& root) {
  // 合成ルートには作者の規則も UA の規則も当たらない。display: block だけが既定と違う
  StyleState root_state;
  root_state.computed.display = Display::Block;
  root_state.computed.border_color = root_state.computed.color;

  Result<WritingMode> writing_mode = document_writing_mode(root, root_state);
  if (!writing_mode) {
    return std::unexpected(writing_mode.error());
  }
  root_state.computed.writing_mode = *writing_mode;

  StyledNode styled;
  styled.type = StyledNode::Type::Element;
  styled.tag = root.tag;
  styled.style = root_state.computed;
  styled.location = root.location;
  if (Result<void> children = build_children(root, root_state, styled.children); !children) {
    return std::unexpected(children.error());
  }
  return styled;
}

}  // namespace

Result<StyledNode> resolve(const html::Node& root, std::size_t max_style_rules) {
  if (root.type != html::Node::Type::Element) {
    return fail(ErrorKind::Internal, "style::resolve() expects the synthetic root element",
                root.location);
  }
  Resolver resolver(max_style_rules);
  if (Result<void> loaded = resolver.load(root); !loaded) {
    return std::unexpected(loaded.error());
  }
  return resolver.build(root);
}

}  // namespace shashoku::style
