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

#include "core/diagnostics.hpp"
#include "core/number_text.hpp"
#include "core/result.hpp"
#include "html/dom.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"
#include "style/css_chars.hpp"
#include "style/css_keywords.hpp"
#include "style/css_parser.hpp"
#include "style/css_tokens.hpp"
#include "style/declaration.hpp"
#include "style/style_error.hpp"
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

// 計算値の長さを検査するための文脈（ARCHITECTURE.md A36）。
// style の出口では「font-size を除くすべての長さが有限で、絶対値が max_px 以内」。
//
// `em` の乗算（`1e38em x 16px`）でも、px で直接書いた値（`3e38px`）でも、同じ上限で止める。
// 検査に使うのは比較と isfinite だけなので A9（浮動小数点の決定性）の許可リスト内。
struct LengthGuard {
  float max_px = kMaxLengthPx;
  PropertyId property = PropertyId::Width;
  SourceLocation location;
  float em_base = 0;
};

// 失敗したときだけ文字列を作る（長さの解決は要素ごとに十数回通る）。
std::unexpected<Error> length_error(float value, const SpecLength& spec, const LengthGuard& guard) {
  const std::string_view name = to_css(guard.property);
  if (!std::isfinite(value)) {
    // float で表せない値。どう掛けてそうなったかを添える（`1e+38em x 16 px`）。
    std::string computation = spec.em
                                  ? std::format("{}em x font-size {} px", number_text(spec.value),
                                                number_text(guard.em_base))
                                  : std::format("{}px", number_text(spec.value));
    return fail(ErrorKind::LimitExceeded,
                std::format("`{}` computes to {} ({}), which a float cannot represent; lengths "
                            "must be finite and at most {} px "
                            "(raise RenderLimits::length_px to allow larger ones)",
                            name, number_text(value), computation, number_text(guard.max_px)),
                guard.location);
  }
  return fail(ErrorKind::LimitExceeded,
              std::format("`{}` computes to {} px, which exceeds the limit of {} px "
                          "(raise RenderLimits::length_px to allow it)",
                          name, number_text(value), number_text(guard.max_px)),
              guard.location);
}

// 上限の判定は 1 つの比較で済ませる: NaN も inf も範囲外もまとめて false になる。
bool within(float value, float max_px) { return value >= -max_px && value <= max_px; }

Result<float> resolve_length(const SpecLength& length, float em_base, const LengthGuard& guard) {
  const float value = length.em ? length.value * em_base : length.value;
  if (!within(value, guard.max_px)) {
    return length_error(value, length, guard);
  }
  return value;
}

Result<Dimension> resolve_dimension(const SpecDimension& dimension, float em_base,
                                    const LengthGuard& guard) {
  switch (dimension.kind) {
    case SpecDimension::Kind::Auto:
      return Dimension::auto_();
    case SpecDimension::Kind::Length: {
      Result<float> value = resolve_length(dimension.length, em_base, guard);
      if (!value) {
        return std::unexpected(value.error());
      }
      return Dimension::px(*value);
    }
    case SpecDimension::Kind::Percent:
      // `%` は包含ブロックが要るので layout まで解決できない（A5）。
      // 上限の検査も layout の出口で行う（#19 の第 2 段階）。
      return Dimension::percent(dimension.percent);
  }
  return Dimension::auto_();
}

Result<LineHeight> resolve_line_height(const SpecLineHeight& value, float em_base,
                                       const LengthGuard& guard) {
  switch (value.kind) {
    case SpecLineHeight::Kind::Normal:
      return LineHeight{.kind = LineHeight::Kind::Normal};
    case SpecLineHeight::Kind::Number:
      // 倍率は倍率のまま継承するので、ここでは値をそのまま持つ。
      // 「倍率 x 自分の font-size」が上限以内かは、カスケードが終わってから見る
      // （継承した倍率 x 子の大きい font-size も捕まえるため）。
      return LineHeight{.kind = LineHeight::Kind::Number, .value = value.number};
    case SpecLineHeight::Kind::Length: {
      Result<float> px = resolve_length(value.length, em_base, guard);
      if (!px) {
        return std::unexpected(px.error());
      }
      return LineHeight{.kind = LineHeight::Kind::Px, .value = *px};
    }
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

// 長さを含むプロパティの適用は失敗しうる（上限の検査。A36）。
Result<void> apply_value(PropertyId property, const SpecifiedValue& value, StyleState& state,
                         float em_base, const LengthGuard& base_guard) {
  ComputedStyle& s = state.computed;
  // このプロパティの宣言に紐づく検査の文脈（メッセージと位置に使う）。
  LengthGuard guard = base_guard;
  guard.property = property;
  guard.em_base = em_base;

  // 長さ 1 つを解決して代入する。失敗したらそのままエラーを返す。
  const auto length = [&](float& out) -> Result<void> {
    Result<float> resolved = resolve_length(take<SpecLength>(value), em_base, guard);
    if (!resolved) {
      return std::unexpected(resolved.error());
    }
    out = *resolved;
    return {};
  };
  const auto dimension = [&](Dimension& out) -> Result<void> {
    Result<Dimension> resolved = resolve_dimension(take<SpecDimension>(value), em_base, guard);
    if (!resolved) {
      return std::unexpected(resolved.error());
    }
    out = *resolved;
    return {};
  };

  switch (property) {
    case PropertyId::Display:
      s.display = take<Display>(value);
      return {};
    case PropertyId::Width:
      return dimension(s.width);
    case PropertyId::Height:
      return dimension(s.height);
    case PropertyId::MarginTop:
      return dimension(s.margin.top);
    case PropertyId::MarginRight:
      return dimension(s.margin.right);
    case PropertyId::MarginBottom:
      return dimension(s.margin.bottom);
    case PropertyId::MarginLeft:
      return dimension(s.margin.left);
    case PropertyId::PaddingTop:
      return length(s.padding.top);
    case PropertyId::PaddingRight:
      return length(s.padding.right);
    case PropertyId::PaddingBottom:
      return length(s.padding.bottom);
    case PropertyId::PaddingLeft:
      return length(s.padding.left);
    case PropertyId::BorderWidth:
      return length(state.border_width_length);
    case PropertyId::BorderStyle:
      state.border_style = take<BorderStyle>(value);
      return {};
    case PropertyId::BorderColor: {
      const auto color = take<SpecColor>(value);
      state.border_color_is_current = color.current_color;
      s.border_color = color.color;
      return {};
    }
    case PropertyId::BorderRadius:
      return length(s.border_radius);
    case PropertyId::BackgroundColor:
      s.background_color = take<SpecColor>(value).color;
      return {};
    case PropertyId::FlexDirection:
      s.flex_direction = take<FlexDirection>(value);
      return {};
    case PropertyId::JustifyContent:
      s.justify_content = take<JustifyContent>(value);
      return {};
    case PropertyId::AlignItems:
      s.align_items = take<AlignItems>(value);
      return {};
    case PropertyId::RowGap:
      return length(s.row_gap);
    case PropertyId::ColumnGap:
      return length(s.column_gap);
    case PropertyId::FlexGrow:
      s.flex_grow = take<SpecNumber>(value).value;
      return {};
    case PropertyId::FlexShrink:
      s.flex_shrink = take<SpecNumber>(value).value;
      return {};
    case PropertyId::FlexBasis:
      return dimension(s.flex_basis);
    case PropertyId::Color:
      s.color = take<SpecColor>(value).color;
      return {};
    case PropertyId::FontSize: {
      // font-size だけは length_px で縛らない（A36）。A25 の font_size_device_px が
      // scale 込みでより厳しく見ており、そちらは要素の位置つきで報告する。ここで二重に
      // 検査すると、同じ入力のエラーの位置が宣言の側に移ってしまう。非有限になった場合は
      // cascade() が要素の位置で止める（em の基準が壊れたまま先へ進めないため）。
      const auto spec = take<SpecLength>(value);
      s.font_size = spec.em ? spec.value * em_base : spec.value;
      return {};
    }
    case PropertyId::FontFamily:
      s.font_family = take<SpecFontFamily>(value).names;
      return {};
    case PropertyId::FontWeight:
      s.font_weight = take<SpecWeight>(value).value;
      return {};
    case PropertyId::LineHeight: {
      Result<LineHeight> resolved =
          resolve_line_height(take<SpecLineHeight>(value), em_base, guard);
      if (!resolved) {
        return std::unexpected(resolved.error());
      }
      s.line_height = *resolved;
      return {};
    }
    case PropertyId::LetterSpacing:
      return length(s.letter_spacing);
    case PropertyId::TextAlign:
      s.text_align = take<TextAlign>(value);
      return {};
    case PropertyId::LineBreak:
      s.line_break = take<LineBreak>(value);
      return {};
    case PropertyId::OverflowWrap:
      s.overflow_wrap = take<OverflowWrap>(value);
      return {};
    case PropertyId::WritingMode:
      s.writing_mode = take<WritingMode>(value);
      return {};
  }
  return {};
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

Result<void> apply_one(const Declaration& declaration, Origin origin, const StyleState& parent,
                       StyleState& state, float em_base, float max_length_px) {
  record_author_declaration(declaration, origin, state);
  switch (declaration.global) {
    case GlobalKeyword::Inherit:
      // 親の計算値はすでに検査済みなので、そのまま引き継いでよい。
      apply_inherit(declaration.property, parent, state);
      return {};
    case GlobalKeyword::Initial:
      apply_initial(declaration.property, state);
      return {};
    case GlobalKeyword::None: {
      const LengthGuard guard{.max_px = max_length_px,
                              .property = declaration.property,
                              .location = declaration.location,
                              .em_base = em_base};
      return apply_value(declaration.property, declaration.value, state, em_base, guard);
    }
  }
  return {};
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

// 属性の不正（`src` の欠落・`width` / `height` が数値でない）は集めて続行する（A46）。
// 上限の超過（LimitExceeded）だけがその場で止まる。
Result<void> read_image_attributes(const html::Node& node, StyledNode& styled, float max_length_px,
                                   Diagnostics& diagnostics) {
  const html::Attribute* src = node.find_attr("src");
  if (src == nullptr) {
    diagnostics.add_error(error_with_hint(ErrorKind::UnsupportedValue,
                                          "`<img>` requires a `src` attribute", node.location, {}));
  } else {
    styled.image_src = src->value;
  }
  for (const std::string_view name : {"width", "height"}) {
    const html::Attribute* attr = node.find_attr(name);
    if (attr == nullptr) {
      continue;
    }
    const std::optional<float> value = parse_attribute_number(attr->value);
    if (!value) {
      diagnostics.add_error(
          error_with_hint(ErrorKind::UnsupportedValue,
                          std::format("`<img {}=\"{}\">` is not supported (the attribute must be a "
                                      "non-negative number of px, without a unit)",
                                      name, attr->value),
                          attr->location, {}));
      continue;  // 書かれなかった扱い（layout が本来の寸法を使う）
    }
    // 属性も layout に渡る長さなので、CSS の長さと同じ上限で見る（A36）。
    if (!within(*value, max_length_px)) {
      return fail(ErrorKind::LimitExceeded,
                  std::format("`<img {}=\"{}\">` is {} px, which exceeds the limit of {} px "
                              "(raise RenderLimits::length_px to allow it)",
                              name, attr->value, *value, max_length_px),
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
  Resolver(Diagnostics& diagnostics, std::size_t max_style_rules, float max_length_px)
      : diagnostics_(&diagnostics),
        max_style_rules_(max_style_rules),
        max_length_px_(max_length_px) {}

  Result<void> load(const html::Node& root);
  Result<StyledNode> build(const html::Node& root);

 private:
  // `sink` は診断の行き先。文書の writing-mode を決める先読みでは捨てる Diagnostics を渡す
  // （同じ要素を 2 回カスケードするので、そのままだと同じ診断が 2 件になる）。
  [[nodiscard]] Result<StyleState> cascade(const html::Node& node, const StyleState& parent,
                                           Diagnostics& sink) const;
  [[nodiscard]] WritingMode document_writing_mode(const html::Node& root,
                                                  const StyleState& root_state) const;
  Result<void> build_children(const html::Node& node, const StyleState& state,
                              std::vector<StyledNode>& out) const;
  [[nodiscard]] Result<std::optional<StyledNode>> build_element(const html::Node& node,
                                                                const StyleState& parent) const;

  Diagnostics* diagnostics_ = nullptr;
  std::size_t max_style_rules_ = kMaxStyleRules;
  float max_length_px_ = kMaxLengthPx;
  Stylesheet ua_;
  Stylesheet author_;
};

// `<style>` 要素は木のどこにあってもよく、文書全体に効く。複数あれば出現順に連結する。
// NOLINTNEXTLINE(misc-no-recursion): DOM は木なので前順の再帰で辿る
Result<void> collect_author_css(const html::Node& node, std::uint32_t& order, Stylesheet& out,
                                std::size_t max_rules, Diagnostics& diagnostics) {
  if (node.type == html::Node::Type::Element && node.tag == "style") {
    for (const html::Node& child : node.children) {
      if (child.type != html::Node::Type::Text) {
        continue;
      }
      if (Result<void> parsed =
              parse_stylesheet(child.text, child.location, order, out, diagnostics);
          !parsed) {
        return parsed;
      }
      // セレクタの照合は「規則数 x 要素数」なので、規則の数そのものに上限が要る（A25）。
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
    if (Result<void> collected = collect_author_css(child, order, out, max_rules, diagnostics);
        !collected) {
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
  return collect_author_css(root, order, author_, max_style_rules_, *diagnostics_);
}

Result<StyleState> Resolver::cascade(const html::Node& node, const StyleState& parent,
                                     Diagnostics& sink) const {
  std::vector<Declaration> inline_declarations;
  if (const html::Attribute* attr = node.find_attr("style"); attr != nullptr) {
    Result<std::vector<Declaration>> parsed = parse_inline_style(attr->value, attr->location, sink);
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
      if (Result<void> applied = apply_one(*entry.declaration, entry.origin, parent, state,
                                           parent.computed.font_size, max_length_px_);
          !applied) {
        return std::unexpected(applied.error());
      }
    }
  }
  // font-size が非有限なら、他の em はすべて inf / NaN になる。原因は font-size なので、
  // 残りを解決する前にここで止める（A36）。種類と位置は、これまで api の
  // font_size_device_px（A25）が返していたものと同じ = LimitExceeded + 要素の位置。
  if (!std::isfinite(state.computed.font_size)) {
    return fail(ErrorKind::LimitExceeded,
                std::format("font-size computes to {} px, which a float cannot represent; check "
                            "the `em` factors on this element and its ancestors "
                            "(the font-size limit is RenderLimits::font_size_device_px)",
                            number_text(state.computed.font_size)),
                node.location);
  }
  for (const MatchedDeclaration& entry : matched) {
    if (entry.declaration->property != PropertyId::FontSize) {
      if (Result<void> applied = apply_one(*entry.declaration, entry.origin, parent, state,
                                           state.computed.font_size, max_length_px_);
          !applied) {
        return std::unexpected(applied.error());
      }
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

// ---- 出力の不変条件（ARCHITECTURE.md A36）----------------------------------
//
// 宣言を適用する時点の検査（宣言の位置つき）に加えて、カスケードが終わった計算値を
// もう一度まとめて見る。継承で入ってきた値と、プロパティを足したときの掛け忘れを
// ここで捕まえる（検査が「引数の渡し方」ではなく「段の出口の性質」になる）。
//
// ここだけが知っている検査が 1 つある: `line-height` の倍率は倍率のまま継承するので、
// 「倍率 x その要素自身の font-size」は子で初めて上限を超えうる。
Result<void> check_computed_lengths(const ComputedStyle& s, float max_px, SourceLocation location) {
  const auto bad = [&](std::string_view what, float value) -> std::unexpected<Error> {
    return fail(ErrorKind::LimitExceeded,
                std::format("the computed `{}` is {} px, which is not a finite length within the "
                            "limit of {} px (raise RenderLimits::length_px to allow it)",
                            what, number_text(value), number_text(max_px)),
                location);
  };
  const std::array<std::pair<std::string_view, float>, 9> lengths = {{
      {"padding-top", s.padding.top},
      {"padding-right", s.padding.right},
      {"padding-bottom", s.padding.bottom},
      {"padding-left", s.padding.left},
      {"border-width", s.border_width},
      {"border-radius", s.border_radius},
      {"row-gap", s.row_gap},
      {"column-gap", s.column_gap},
      {"letter-spacing", s.letter_spacing},
  }};
  for (const auto& [name, value] : lengths) {
    if (!within(value, max_px)) {
      return bad(name, value);
    }
  }
  const std::array<std::pair<std::string_view, const Dimension*>, 7> dimensions = {{
      {"width", &s.width},
      {"height", &s.height},
      {"margin-top", &s.margin.top},
      {"margin-right", &s.margin.right},
      {"margin-bottom", &s.margin.bottom},
      {"margin-left", &s.margin.left},
      {"flex-basis", &s.flex_basis},
  }};
  for (const auto& [name, dimension] : dimensions) {
    // `%` は layout が解決するので、ここでは見られない（#19 の第 2 段階）。
    if (dimension->kind == Dimension::Kind::Px && !within(dimension->value, max_px)) {
      return bad(name, dimension->value);
    }
  }
  if (s.line_height.kind == LineHeight::Kind::Px && !within(s.line_height.value, max_px)) {
    return bad("line-height", s.line_height.value);
  }
  if (s.line_height.kind == LineHeight::Kind::Number) {
    const float px = s.line_height.value * s.font_size;
    if (!within(px, max_px)) {
      return fail(ErrorKind::LimitExceeded,
                  std::format("`line-height: {}` x font-size {} px computes to {} px, which is not "
                              "a finite length within the limit of {} px "
                              "(raise RenderLimits::length_px to allow it)",
                              number_text(s.line_height.value), number_text(s.font_size),
                              number_text(px), number_text(max_px)),
                  location);
    }
  }
  return {};
}

// 計算値まで見ないと分からない対応外（A46 の「計算値の検査で分かる UnsupportedLayout」）は
// 集めて続行する。止まるのは長さの上限（LimitExceeded）だけ。
Result<void> validate(const html::Node& node, const StyleState& state, const StyleState& parent,
                      float max_length_px, Diagnostics& diagnostics) {
  if (Result<void> lengths = check_computed_lengths(state.computed, max_length_px, node.location);
      !lengths) {
    return lengths;
  }
  if (state.writing_mode_declared && state.computed.writing_mode != parent.computed.writing_mode) {
    diagnostics.add_error(error_with_hint(
        ErrorKind::UnsupportedLayout,
        std::format("`writing-mode: {}` differs from the inherited `{}`; the whole "
                    "document uses one writing mode and only top-level elements may set "
                    "it (ARCHITECTURE.md A1)",
                    to_css(state.computed.writing_mode), to_css(parent.computed.writing_mode)),
        state.writing_mode_location, {}));
  }
  if (state.computed.display == Display::Inline && node.tag != "img" && state.box_property) {
    // A46 の hint (c): `display: block` にすると通るが、文の流れが切れる。
    // どちらを勧めるかを明記する（検証 C の観察）
    diagnostics.add_error(error_with_hint(
        ErrorKind::UnsupportedLayout,
        std::format("`{}` is not supported on an inline element (`display: inline`); only "
                    "`<img>` takes box properties while inline",
                    to_css(*state.box_property)),
        state.box_property_location,
        "drop the declaration; `display: block` would accept it but breaks the surrounding "
        "text flow"));
  }
  return {};
}

// writing-mode は文書全体で 1 つ（A1）。トップレベル要素の指定だけを見て文書の値を決める。
//
// ここはカスケードの**先読み**で、同じ要素を build_element がもう一度カスケードする。
// そのため診断はここでは出さない: カスケードの診断は捨てる Diagnostics に流し、
// 致命エラーも握り潰す（どちらも build_element がもう一度通るときに、正しい順序で出る）。
// 食い違いのエラーだけはここでしか分からないので集める。
WritingMode Resolver::document_writing_mode(const html::Node& root,
                                            const StyleState& root_state) const {
  Diagnostics scratch{diagnostics_->max_entries()};
  std::optional<WritingMode> chosen;
  for (const html::Node& child : root.children) {
    if (child.type != html::Node::Type::Element || child.tag == "style" || child.tag == "rp") {
      continue;
    }
    Result<StyleState> state = cascade(child, root_state, scratch);
    if (!state) {
      continue;  // 致命エラー。報告は build_element に任せる（先に集めた診断を失わない）
    }
    if (!state->writing_mode_declared) {
      continue;
    }
    if (chosen && *chosen != state->computed.writing_mode) {
      // 最初に出た値を文書の writing-mode として続行する（決定的で、続きの診断が出せる）
      diagnostics_->add_error(error_with_hint(
          ErrorKind::UnsupportedLayout,
          std::format("top-level elements disagree about `writing-mode` (`{}` and `{}`); "
                      "the whole document must use one writing mode (ARCHITECTURE.md A1)",
                      to_css(*chosen), to_css(state->computed.writing_mode)),
          state->writing_mode_location, {}));
      continue;
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
  Result<StyleState> state = cascade(node, parent, *diagnostics_);
  if (!state) {
    return std::unexpected(state.error());
  }
  if (Result<void> checked = validate(node, *state, parent, max_length_px_, *diagnostics_);
      !checked) {
    return std::unexpected(checked.error());
  }

  StyledNode styled;
  styled.type = StyledNode::Type::Element;
  styled.tag = node.tag;
  styled.style = state->computed;
  styled.location = node.location;
  if (node.tag == "img") {
    if (Result<void> attrs = read_image_attributes(node, styled, max_length_px_, *diagnostics_);
        !attrs) {
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

  root_state.computed.writing_mode = document_writing_mode(root, root_state);

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

Result<StyledNode> resolve(const html::Node& root, Diagnostics& diagnostics,
                           std::size_t max_style_rules, float max_length_px) {
  if (root.type != html::Node::Type::Element) {
    return fail(ErrorKind::Internal, "style::resolve() expects the synthetic root element",
                root.location);
  }
  Resolver resolver(diagnostics, max_style_rules, max_length_px);
  if (Result<void> loaded = resolver.load(root); !loaded) {
    return std::unexpected(loaded.error());
  }
  return resolver.build(root);
}

}  // namespace shashoku::style
