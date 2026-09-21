#include "style/value_parser.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/color.hpp"
#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "style/css_chars.hpp"
#include "style/css_color.hpp"
#include "style/css_tokens.hpp"
#include "style/declaration.hpp"

namespace shashoku::style {
namespace {

// 対応するプロパティ名（ショートハンドを含む）。DESIGN.md §4 + ARCHITECTURE.md §3.7。
enum class PropertyName : std::uint8_t {
  Display,
  Width,
  Height,
  Margin,
  MarginTop,
  MarginRight,
  MarginBottom,
  MarginLeft,
  Padding,
  PaddingTop,
  PaddingRight,
  PaddingBottom,
  PaddingLeft,
  Border,
  BorderWidth,
  BorderStyle,
  BorderColor,
  BorderRadius,
  Background,
  BackgroundColor,
  FlexDirection,
  JustifyContent,
  AlignItems,
  Gap,
  RowGap,
  ColumnGap,
  Flex,
  FlexGrow,
  FlexShrink,
  FlexBasis,
  Color,
  FontSize,
  FontFamily,
  FontWeight,
  LineHeight,
  LetterSpacing,
  TextAlign,
  LineBreak,
  OverflowWrap,
  WritingMode,
};

struct NameEntry {
  std::string_view name;
  PropertyName property;
};

constexpr auto kPropertyNames = std::to_array<NameEntry>({
    {"align-items", PropertyName::AlignItems},
    {"background", PropertyName::Background},
    {"background-color", PropertyName::BackgroundColor},
    {"border", PropertyName::Border},
    {"border-color", PropertyName::BorderColor},
    {"border-radius", PropertyName::BorderRadius},
    {"border-style", PropertyName::BorderStyle},
    {"border-width", PropertyName::BorderWidth},
    {"color", PropertyName::Color},
    {"column-gap", PropertyName::ColumnGap},
    {"display", PropertyName::Display},
    {"flex", PropertyName::Flex},
    {"flex-basis", PropertyName::FlexBasis},
    {"flex-direction", PropertyName::FlexDirection},
    {"flex-grow", PropertyName::FlexGrow},
    {"flex-shrink", PropertyName::FlexShrink},
    {"font-family", PropertyName::FontFamily},
    {"font-size", PropertyName::FontSize},
    {"font-weight", PropertyName::FontWeight},
    {"gap", PropertyName::Gap},
    {"height", PropertyName::Height},
    {"justify-content", PropertyName::JustifyContent},
    {"letter-spacing", PropertyName::LetterSpacing},
    {"line-break", PropertyName::LineBreak},
    {"line-height", PropertyName::LineHeight},
    {"margin", PropertyName::Margin},
    {"margin-bottom", PropertyName::MarginBottom},
    {"margin-left", PropertyName::MarginLeft},
    {"margin-right", PropertyName::MarginRight},
    {"margin-top", PropertyName::MarginTop},
    {"overflow-wrap", PropertyName::OverflowWrap},
    {"padding", PropertyName::Padding},
    {"padding-bottom", PropertyName::PaddingBottom},
    {"padding-left", PropertyName::PaddingLeft},
    {"padding-right", PropertyName::PaddingRight},
    {"padding-top", PropertyName::PaddingTop},
    {"row-gap", PropertyName::RowGap},
    {"text-align", PropertyName::TextAlign},
    {"width", PropertyName::Width},
    {"writing-mode", PropertyName::WritingMode},
});

std::optional<PropertyName> lookup_property(std::string_view name) {
  for (const NameEntry& entry : kPropertyNames) {
    if (entry.name == name) {
      return entry.property;
    }
  }
  return std::nullopt;
}

// ---- longhand への展開表（inherit / initial の配布に使う）--------------------

constexpr std::array<PropertyId, 4> kMarginSides = {PropertyId::MarginTop, PropertyId::MarginRight,
                                                    PropertyId::MarginBottom,
                                                    PropertyId::MarginLeft};
constexpr std::array<PropertyId, 4> kPaddingSides = {
    PropertyId::PaddingTop, PropertyId::PaddingRight, PropertyId::PaddingBottom,
    PropertyId::PaddingLeft};
constexpr std::array<PropertyId, 3> kBorderParts = {
    PropertyId::BorderWidth, PropertyId::BorderStyle, PropertyId::BorderColor};
constexpr std::array<PropertyId, 3> kFlexParts = {PropertyId::FlexGrow, PropertyId::FlexShrink,
                                                  PropertyId::FlexBasis};
constexpr std::array<PropertyId, 2> kGapParts = {PropertyId::RowGap, PropertyId::ColumnGap};

// ---- エラーメッセージ --------------------------------------------------------

struct Ctx {
  std::string_view name;
  std::string_view raw;
  SourceLocation location;
};

std::unexpected<Error> bad_value(const Ctx& ctx, std::string_view detail) {
  return fail(ErrorKind::UnsupportedValue,
              std::format("`{}: {}` is not supported ({})", ctx.name, ctx.raw, detail),
              ctx.location);
}

// float にできない数値（`1e39px` / `1e400px`）。「数値が範囲外」は、em の乗算であふれる
// `1e38em` と同じ `LimitExceeded` に寄せてある（ARCHITECTURE.md A-new）: 利用者から見て
// この 2 つが別の種類なのは説明しづらい。ここは float の表現範囲なので RenderLimits では
// 緩められない（緩められるのは計算値の上限 length_px の方）。
std::unexpected<Error> out_of_range(const Ctx& ctx) {
  return fail(ErrorKind::LimitExceeded,
              std::format("`{}: {}` is out of range (the number cannot be represented as a "
                          "32-bit float; lengths must be finite and within "
                          "RenderLimits::length_px)",
                          ctx.name, ctx.raw),
              ctx.location);
}

constexpr std::string_view kLengthHelp = "supported lengths: <number>px, <number>em, or 0";

// ---- 値の部品 ----------------------------------------------------------------

bool representable(double v) { return std::isfinite(static_cast<float>(v)); }

Result<SpecLength> to_length(const Ctx& ctx, const ValueToken& token, bool allow_negative) {
  if (token.kind == ValueToken::Kind::Number && token.number == 0) {
    return SpecLength{.value = 0, .em = false};
  }
  if (token.kind == ValueToken::Kind::Number) {
    return bad_value(ctx, std::format("lengths other than `0` need a unit ({})", kLengthHelp));
  }
  if (token.kind == ValueToken::Kind::Percentage) {
    return bad_value(ctx, "`%` is only supported for `width` and `flex-basis`");
  }
  if (token.kind != ValueToken::Kind::Dimension) {
    return bad_value(ctx, kLengthHelp);
  }
  if (token.unit != "px" && token.unit != "em") {
    return bad_value(ctx,
                     std::format("`{}` is not a supported unit ({})", token.unit, kLengthHelp));
  }
  if (!representable(token.number)) {
    return out_of_range(ctx);
  }
  if (token.number < 0 && !allow_negative) {
    return bad_value(ctx, "negative lengths are only allowed for `margin` and `letter-spacing`");
  }
  return SpecLength{.value = static_cast<float>(token.number), .em = token.unit == "em"};
}

struct DimensionOptions {
  bool allow_percent = false;
  bool allow_negative = false;
};

Result<SpecDimension> to_dimension(const Ctx& ctx, const ValueToken& token,
                                   DimensionOptions options) {
  if (token.kind == ValueToken::Kind::Ident && ascii_lower(token.text) == "auto") {
    return SpecDimension::make_auto();
  }
  if (token.kind == ValueToken::Kind::Percentage) {
    if (!options.allow_percent) {
      return bad_value(ctx, "`%` is only supported for `width` and `flex-basis`");
    }
    if (!representable(token.number)) {
      return out_of_range(ctx);
    }
    if (token.number < 0) {
      return bad_value(ctx, "negative percentages are not allowed");
    }
    return SpecDimension::make_percent(static_cast<float>(token.number));
  }
  Result<SpecLength> length = to_length(ctx, token, options.allow_negative);
  if (!length) {
    return std::unexpected(length.error());
  }
  return SpecDimension::make_length(*length);
}

template <class E>
struct KeywordEntry {
  std::string_view name;
  E value;
};

template <class E, std::size_t N>
Result<E> single_keyword(const Ctx& ctx, std::span<const ValueToken> tokens,
                         const std::array<KeywordEntry<E>, N>& table, std::string_view supported) {
  if (tokens.size() == 1 && tokens[0].kind == ValueToken::Kind::Ident) {
    const std::string lower = ascii_lower(tokens[0].text);
    for (const KeywordEntry<E>& entry : table) {
      if (entry.name == lower) {
        return entry.value;
      }
    }
  }
  return bad_value(ctx, supported);
}

void emit(std::vector<Declaration>& out, const Ctx& ctx, PropertyId property,
          SpecifiedValue value) {
  out.push_back(Declaration{.property = property,
                            .global = GlobalKeyword::None,
                            .value = std::move(value),
                            .location = ctx.location});
}

// CSS のショートハンドの 1〜4 値を上右下左に配る。
template <class T>
std::array<T, 4> expand_sides(const std::vector<T>& values) {
  switch (values.size()) {
    case 1:
      return {values[0], values[0], values[0], values[0]};
    case 2:
      return {values[0], values[1], values[0], values[1]};
    case 3:
      return {values[0], values[1], values[2], values[1]};
    default:
      return {values[0], values[1], values[2], values[3]};
  }
}

// ---- プロパティごとの値パーサ -------------------------------------------------

Result<void> parse_display(const Ctx& ctx, std::span<const ValueToken> tokens,
                           std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<Display>, 4> kTable = {{
      {"block", Display::Block},
      {"flex", Display::Flex},
      {"inline", Display::Inline},
      {"none", Display::None},
  }};
  Result<Display> value =
      single_keyword(ctx, tokens, kTable, "supported: block, flex, inline, none");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::Display, *value);
  return {};
}

Result<void> parse_flex_direction(const Ctx& ctx, std::span<const ValueToken> tokens,
                                  std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<FlexDirection>, 2> kTable = {{
      {"row", FlexDirection::Row},
      {"column", FlexDirection::Column},
  }};
  Result<FlexDirection> value =
      single_keyword(ctx, tokens, kTable, "supported: row, column (`*-reverse` is not supported)");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::FlexDirection, *value);
  return {};
}

Result<void> parse_justify_content(const Ctx& ctx, std::span<const ValueToken> tokens,
                                   std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<JustifyContent>, 8> kTable = {{
      {"flex-start", JustifyContent::FlexStart},
      {"start", JustifyContent::FlexStart},
      {"flex-end", JustifyContent::FlexEnd},
      {"end", JustifyContent::FlexEnd},
      {"center", JustifyContent::Center},
      {"space-between", JustifyContent::SpaceBetween},
      {"space-around", JustifyContent::SpaceAround},
      {"space-evenly", JustifyContent::SpaceEvenly},
  }};
  Result<JustifyContent> value =
      single_keyword(ctx, tokens, kTable,
                     "supported: flex-start, flex-end, center, space-between, space-around, "
                     "space-evenly (start / end are accepted as aliases)");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::JustifyContent, *value);
  return {};
}

Result<void> parse_align_items(const Ctx& ctx, std::span<const ValueToken> tokens,
                               std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<AlignItems>, 4> kTable = {{
      {"stretch", AlignItems::Stretch},
      {"flex-start", AlignItems::FlexStart},
      {"flex-end", AlignItems::FlexEnd},
      {"center", AlignItems::Center},
  }};
  Result<AlignItems> value =
      single_keyword(ctx, tokens, kTable,
                     "supported: stretch, flex-start, flex-end, center (baseline alignment "
                     "is not supported)");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::AlignItems, *value);
  return {};
}

Result<void> parse_text_align(const Ctx& ctx, std::span<const ValueToken> tokens,
                              std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<TextAlign>, 6> kTable = {{
      {"start", TextAlign::Start},
      {"end", TextAlign::End},
      {"left", TextAlign::Left},
      {"right", TextAlign::Right},
      {"center", TextAlign::Center},
      {"justify", TextAlign::Justify},
  }};
  Result<TextAlign> value =
      single_keyword(ctx, tokens, kTable, "supported: start, end, left, right, center, justify");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::TextAlign, *value);
  return {};
}

Result<void> parse_line_break(const Ctx& ctx, std::span<const ValueToken> tokens,
                              std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<LineBreak>, 4> kTable = {{
      {"auto", LineBreak::Auto},
      {"loose", LineBreak::Loose},
      {"normal", LineBreak::Normal},
      {"strict", LineBreak::Strict},
  }};
  Result<LineBreak> value =
      single_keyword(ctx, tokens, kTable, "supported: auto, loose, normal, strict");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::LineBreak, *value);
  return {};
}

Result<void> parse_overflow_wrap(const Ctx& ctx, std::span<const ValueToken> tokens,
                                 std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<OverflowWrap>, 3> kTable = {{
      {"normal", OverflowWrap::Normal},
      {"anywhere", OverflowWrap::Anywhere},
      {"break-word", OverflowWrap::BreakWord},
  }};
  Result<OverflowWrap> value =
      single_keyword(ctx, tokens, kTable, "supported: normal, anywhere, break-word");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::OverflowWrap, *value);
  return {};
}

Result<void> parse_writing_mode(const Ctx& ctx, std::span<const ValueToken> tokens,
                                std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<WritingMode>, 2> kTable = {{
      {"horizontal-tb", WritingMode::HorizontalTb},
      {"vertical-rl", WritingMode::VerticalRl},
  }};
  Result<WritingMode> value =
      single_keyword(ctx, tokens, kTable, "supported: horizontal-tb, vertical-rl");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::WritingMode, *value);
  return {};
}

Result<void> parse_border_style_value(const Ctx& ctx, std::span<const ValueToken> tokens,
                                      std::vector<Declaration>& out) {
  constexpr std::array<KeywordEntry<BorderStyle>, 2> kTable = {{
      {"none", BorderStyle::None},
      {"solid", BorderStyle::Solid},
  }};
  Result<BorderStyle> value = single_keyword(ctx, tokens, kTable, "supported: solid, none");
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, PropertyId::BorderStyle, *value);
  return {};
}

Result<void> parse_single_dimension(const Ctx& ctx, std::span<const ValueToken> tokens,
                                    PropertyId property, DimensionOptions options,
                                    std::vector<Declaration>& out) {
  if (tokens.size() != 1) {
    return bad_value(ctx, "expected a single length, percentage or `auto`");
  }
  Result<SpecDimension> value = to_dimension(ctx, tokens[0], options);
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, property, *value);
  return {};
}

Result<void> parse_single_length(const Ctx& ctx, std::span<const ValueToken> tokens,
                                 PropertyId property, bool allow_negative,
                                 std::vector<Declaration>& out) {
  if (tokens.size() != 1) {
    return bad_value(ctx, std::format("expected a single length ({})", kLengthHelp));
  }
  Result<SpecLength> value = to_length(ctx, tokens[0], allow_negative);
  if (!value) {
    return std::unexpected(value.error());
  }
  emit(out, ctx, property, *value);
  return {};
}

Result<void> parse_margin(const Ctx& ctx, std::span<const ValueToken> tokens,
                          std::vector<Declaration>& out) {
  if (tokens.empty() || tokens.size() > 4) {
    return bad_value(ctx, "`margin` takes 1 to 4 values, each a length or `auto`");
  }
  std::vector<SpecDimension> values;
  for (const ValueToken& token : tokens) {
    Result<SpecDimension> value = to_dimension(ctx, token, {.allow_negative = true});
    if (!value) {
      return std::unexpected(value.error());
    }
    values.push_back(*value);
  }
  const std::array<SpecDimension, 4> sides = expand_sides(values);
  for (std::size_t i = 0; i < 4; ++i) {
    emit(out, ctx, kMarginSides[i], sides[i]);
  }
  return {};
}

Result<void> parse_padding(const Ctx& ctx, std::span<const ValueToken> tokens,
                           std::vector<Declaration>& out) {
  if (tokens.empty() || tokens.size() > 4) {
    return bad_value(ctx, "`padding` takes 1 to 4 lengths");
  }
  std::vector<SpecLength> values;
  for (const ValueToken& token : tokens) {
    Result<SpecLength> value = to_length(ctx, token, false);
    if (!value) {
      return std::unexpected(value.error());
    }
    values.push_back(*value);
  }
  const std::array<SpecLength, 4> sides = expand_sides(values);
  for (std::size_t i = 0; i < 4; ++i) {
    emit(out, ctx, kPaddingSides[i], sides[i]);
  }
  return {};
}

Result<void> parse_gap(const Ctx& ctx, std::span<const ValueToken> tokens,
                       std::vector<Declaration>& out) {
  if (tokens.empty() || tokens.size() > 2) {
    return bad_value(ctx, "`gap` takes 1 or 2 lengths (row then column)");
  }
  Result<SpecLength> row = to_length(ctx, tokens[0], false);
  if (!row) {
    return std::unexpected(row.error());
  }
  SpecLength column = *row;
  if (tokens.size() == 2) {
    Result<SpecLength> parsed = to_length(ctx, tokens[1], false);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    column = *parsed;
  }
  emit(out, ctx, PropertyId::RowGap, *row);
  emit(out, ctx, PropertyId::ColumnGap, column);
  return {};
}

// `border` の成分を 1 つ読む。読めたら true（どの成分だったかは state が覚える）。
struct BorderParts {
  SpecLength width{.value = 3, .em = false};  // 省略時は medium = 3px
  BorderStyle style = BorderStyle::None;      // 省略時は none（= 幅 0 になる）
  SpecColor color{.current_color = true, .color = kBlack};  // 省略時は currentColor
  bool has_width = false;
  bool has_style = false;
  bool has_color = false;
};

bool take_border_style(BorderParts& parts, std::string_view lower) {
  if (parts.has_style || (lower != "solid" && lower != "none")) {
    return false;
  }
  parts.style = lower == "solid" ? BorderStyle::Solid : BorderStyle::None;
  parts.has_style = true;
  return true;
}

bool take_border_color(BorderParts& parts, const ValueToken& token) {
  if (parts.has_color) {
    return false;
  }
  const std::optional<SpecColor> parsed = parse_color_token(token);
  if (!parsed) {
    return false;
  }
  parts.color = *parsed;
  parts.has_color = true;
  return true;
}

Result<void> parse_border(const Ctx& ctx, std::span<const ValueToken> tokens,
                          std::vector<Declaration>& out) {
  constexpr std::string_view kHelp =
      "`border` takes at most one width (px / em / 0), one style (solid or none) and one color, "
      "in any order; per-side `border-top` etc. are not supported";
  BorderParts parts;
  for (const ValueToken& token : tokens) {
    const std::string lower =
        token.kind == ValueToken::Kind::Ident ? ascii_lower(token.text) : std::string{};
    if (take_border_style(parts, lower) || take_border_color(parts, token)) {
      continue;
    }
    if (parts.has_width || token.kind == ValueToken::Kind::Ident) {
      return bad_value(ctx, kHelp);
    }
    Result<SpecLength> width = to_length(ctx, token, false);
    if (!width) {
      return std::unexpected(width.error());
    }
    parts.width = *width;
    parts.has_width = true;
  }

  emit(out, ctx, PropertyId::BorderWidth, parts.width);
  emit(out, ctx, PropertyId::BorderStyle, parts.style);
  emit(out, ctx, PropertyId::BorderColor, parts.color);
  return {};
}

Result<void> parse_color_property(const Ctx& ctx, std::span<const ValueToken> tokens,
                                  PropertyId property, bool allow_current_color,
                                  std::vector<Declaration>& out) {
  constexpr std::string_view kColorHelp =
      "supported colors: #rgb, #rgba, #rrggbb, #rrggbbaa, rgb(), rgba(), the 148 CSS color names "
      "and transparent";
  if (tokens.size() != 1) {
    return bad_value(ctx, kColorHelp);
  }
  const std::optional<SpecColor> color = parse_color_token(tokens[0]);
  if (!color) {
    return bad_value(ctx, kColorHelp);
  }
  if (color->current_color && !allow_current_color) {
    return bad_value(ctx, "`currentColor` is only supported for `border-color`");
  }
  emit(out, ctx, property, *color);
  return {};
}

Result<void> parse_font_family(const Ctx& ctx, std::span<const ValueToken> tokens,
                               std::vector<Declaration>& out) {
  constexpr std::string_view kHelp =
      "expected a comma-separated list of family names, quoted or unquoted";
  SpecFontFamily family;
  std::string current;
  bool saw_token = false;
  for (const ValueToken& token : tokens) {
    if (token.kind == ValueToken::Kind::Comma) {
      if (current.empty()) {
        return bad_value(ctx, kHelp);
      }
      family.names.push_back(std::move(current));
      current.clear();
      saw_token = false;
      continue;
    }
    if (token.kind == ValueToken::Kind::String) {
      if (saw_token) {
        return bad_value(ctx, kHelp);
      }
      current = token.text;
      saw_token = true;
      continue;
    }
    if (token.kind != ValueToken::Kind::Ident) {
      return bad_value(ctx, kHelp);
    }
    // 引用符なしの名前は空白区切りの識別子をつないで 1 つの名前にする
    if (!current.empty()) {
      current += ' ';
    }
    current += token.text;
    saw_token = true;
  }
  if (current.empty()) {
    return bad_value(ctx, kHelp);
  }
  family.names.push_back(std::move(current));
  emit(out, ctx, PropertyId::FontFamily, std::move(family));
  return {};
}

Result<void> parse_font_weight(const Ctx& ctx, std::span<const ValueToken> tokens,
                               std::vector<Declaration>& out) {
  constexpr std::string_view kHelp =
      "supported: normal (400), bold (700), or 100..900 in steps of 100 "
      "(`bolder` / `lighter` are not supported)";
  if (tokens.size() != 1) {
    return bad_value(ctx, kHelp);
  }
  const ValueToken& token = tokens[0];
  if (token.kind == ValueToken::Kind::Ident) {
    const std::string lower = ascii_lower(token.text);
    if (lower == "normal") {
      emit(out, ctx, PropertyId::FontWeight, SpecWeight{.value = 400});
      return {};
    }
    if (lower == "bold") {
      emit(out, ctx, PropertyId::FontWeight, SpecWeight{.value = 700});
      return {};
    }
    return bad_value(ctx, kHelp);
  }
  if (token.kind != ValueToken::Kind::Number || !representable(token.number)) {
    return bad_value(ctx, kHelp);
  }
  const auto weight = static_cast<int>(token.number);
  if (static_cast<double>(weight) != token.number || weight < 100 || weight > 900 ||
      weight % 100 != 0) {
    return bad_value(ctx, kHelp);
  }
  emit(out, ctx, PropertyId::FontWeight, SpecWeight{.value = weight});
  return {};
}

Result<void> parse_line_height(const Ctx& ctx, std::span<const ValueToken> tokens,
                               std::vector<Declaration>& out) {
  constexpr std::string_view kHelp = "supported: normal, a unitless number, px or em";
  if (tokens.size() != 1) {
    return bad_value(ctx, kHelp);
  }
  const ValueToken& token = tokens[0];
  if (token.kind == ValueToken::Kind::Ident) {
    if (ascii_lower(token.text) != "normal") {
      return bad_value(ctx, kHelp);
    }
    emit(out, ctx, PropertyId::LineHeight, SpecLineHeight::make_normal());
    return {};
  }
  if (token.kind == ValueToken::Kind::Number && token.number != 0) {
    // 「範囲外」と「負」は別のこと。まとめると `line-height: 1e39` が
    // 「負の数は不可」と言ってしまう（issue #19）。
    if (!representable(token.number)) {
      return out_of_range(ctx);
    }
    if (token.number < 0) {
      return bad_value(ctx, "`line-height` must not be negative");
    }
    emit(out, ctx, PropertyId::LineHeight,
         SpecLineHeight::make_number(static_cast<float>(token.number)));
    return {};
  }
  Result<SpecLength> length = to_length(ctx, token, false);
  if (!length) {
    return std::unexpected(length.error());
  }
  emit(out, ctx, PropertyId::LineHeight, SpecLineHeight::make_length(*length));
  return {};
}

Result<void> parse_letter_spacing(const Ctx& ctx, std::span<const ValueToken> tokens,
                                  std::vector<Declaration>& out) {
  if (tokens.size() == 1 && tokens[0].kind == ValueToken::Kind::Ident &&
      ascii_lower(tokens[0].text) == "normal") {
    emit(out, ctx, PropertyId::LetterSpacing, SpecLength{.value = 0, .em = false});
    return {};
  }
  return parse_single_length(ctx, tokens, PropertyId::LetterSpacing, true, out);
}

Result<void> parse_flex_factor(const Ctx& ctx, const ValueToken& token, PropertyId property,
                               std::vector<Declaration>& out) {
  // 範囲外（`flex-grow: 1e39`）を「非負の数を書け」と言わない（issue #19）。
  if (token.kind == ValueToken::Kind::Number && !representable(token.number)) {
    return out_of_range(ctx);
  }
  if (token.kind != ValueToken::Kind::Number || token.number < 0) {
    return bad_value(ctx, "expected a non-negative number");
  }
  emit(out, ctx, property, SpecNumber{.value = static_cast<float>(token.number)});
  return {};
}

// `flex` は 1〜3 値。2 番目が単位なしの数値なら shrink、そうでなければ basis。
Result<void> parse_flex(const Ctx& ctx, std::span<const ValueToken> tokens,
                        std::vector<Declaration>& out) {
  constexpr std::string_view kHelp =
      "supported: none, auto, <grow>, <grow> <shrink>, <grow> <basis>, <grow> <shrink> <basis>";
  if (tokens.empty() || tokens.size() > 3) {
    return bad_value(ctx, kHelp);
  }
  if (tokens.size() == 1 && tokens[0].kind == ValueToken::Kind::Ident) {
    const std::string lower = ascii_lower(tokens[0].text);
    const bool none = lower == "none";
    if (!none && lower != "auto") {
      return bad_value(ctx, kHelp);
    }
    emit(out, ctx, PropertyId::FlexGrow, SpecNumber{.value = none ? 0.0F : 1.0F});
    emit(out, ctx, PropertyId::FlexShrink, SpecNumber{.value = none ? 0.0F : 1.0F});
    emit(out, ctx, PropertyId::FlexBasis, SpecDimension::make_auto());
    return {};
  }

  std::vector<Declaration> parsed;
  if (Result<void> grow = parse_flex_factor(ctx, tokens[0], PropertyId::FlexGrow, parsed); !grow) {
    return grow;
  }
  std::size_t index = 1;
  bool has_shrink = false;
  if (index < tokens.size() && tokens[index].kind == ValueToken::Kind::Number) {
    if (Result<void> shrink = parse_flex_factor(ctx, tokens[index], PropertyId::FlexShrink, parsed);
        !shrink) {
      return shrink;
    }
    has_shrink = true;
    ++index;
  }
  if (!has_shrink) {
    emit(parsed, ctx, PropertyId::FlexShrink, SpecNumber{.value = 1});
  }
  if (index < tokens.size()) {
    Result<SpecDimension> basis = to_dimension(ctx, tokens[index], {.allow_percent = true});
    if (!basis) {
      return std::unexpected(basis.error());
    }
    emit(parsed, ctx, PropertyId::FlexBasis, *basis);
    ++index;
  } else {
    // `flex: 1` は `1 1 0px`（0 は単位なしでよいが計算値は px）
    emit(parsed, ctx, PropertyId::FlexBasis,
         SpecDimension::make_length(SpecLength{.value = 0, .em = false}));
  }
  if (index != tokens.size()) {
    return bad_value(ctx, kHelp);
  }
  for (Declaration& declaration : parsed) {
    out.push_back(std::move(declaration));
  }
  return {};
}

// ---- ディスパッチ ------------------------------------------------------------

Result<void> parse_by_name(PropertyName property, const Ctx& ctx,
                           std::span<const ValueToken> tokens, std::vector<Declaration>& out) {
  constexpr DimensionOptions kWidthOptions{.allow_percent = true, .allow_negative = false};
  constexpr DimensionOptions kHeightOptions{.allow_percent = false, .allow_negative = false};
  constexpr DimensionOptions kMarginOptions{.allow_percent = false, .allow_negative = true};
  switch (property) {
    case PropertyName::Display:
      return parse_display(ctx, tokens, out);
    case PropertyName::Width:
      return parse_single_dimension(ctx, tokens, PropertyId::Width, kWidthOptions, out);
    case PropertyName::Height:
      return parse_single_dimension(ctx, tokens, PropertyId::Height, kHeightOptions, out);
    case PropertyName::Margin:
      return parse_margin(ctx, tokens, out);
    case PropertyName::MarginTop:
      return parse_single_dimension(ctx, tokens, PropertyId::MarginTop, kMarginOptions, out);
    case PropertyName::MarginRight:
      return parse_single_dimension(ctx, tokens, PropertyId::MarginRight, kMarginOptions, out);
    case PropertyName::MarginBottom:
      return parse_single_dimension(ctx, tokens, PropertyId::MarginBottom, kMarginOptions, out);
    case PropertyName::MarginLeft:
      return parse_single_dimension(ctx, tokens, PropertyId::MarginLeft, kMarginOptions, out);
    case PropertyName::Padding:
      return parse_padding(ctx, tokens, out);
    case PropertyName::PaddingTop:
      return parse_single_length(ctx, tokens, PropertyId::PaddingTop, false, out);
    case PropertyName::PaddingRight:
      return parse_single_length(ctx, tokens, PropertyId::PaddingRight, false, out);
    case PropertyName::PaddingBottom:
      return parse_single_length(ctx, tokens, PropertyId::PaddingBottom, false, out);
    case PropertyName::PaddingLeft:
      return parse_single_length(ctx, tokens, PropertyId::PaddingLeft, false, out);
    case PropertyName::Border:
      return parse_border(ctx, tokens, out);
    case PropertyName::BorderWidth:
      return parse_single_length(ctx, tokens, PropertyId::BorderWidth, false, out);
    case PropertyName::BorderStyle:
      return parse_border_style_value(ctx, tokens, out);
    case PropertyName::BorderColor:
      return parse_color_property(ctx, tokens, PropertyId::BorderColor, true, out);
    case PropertyName::BorderRadius:
      return parse_single_length(ctx, tokens, PropertyId::BorderRadius, false, out);
    case PropertyName::Background:
    case PropertyName::BackgroundColor:
      return parse_color_property(ctx, tokens, PropertyId::BackgroundColor, false, out);
    case PropertyName::FlexDirection:
      return parse_flex_direction(ctx, tokens, out);
    case PropertyName::JustifyContent:
      return parse_justify_content(ctx, tokens, out);
    case PropertyName::AlignItems:
      return parse_align_items(ctx, tokens, out);
    case PropertyName::Gap:
      return parse_gap(ctx, tokens, out);
    case PropertyName::RowGap:
      return parse_single_length(ctx, tokens, PropertyId::RowGap, false, out);
    case PropertyName::ColumnGap:
      return parse_single_length(ctx, tokens, PropertyId::ColumnGap, false, out);
    case PropertyName::Flex:
      return parse_flex(ctx, tokens, out);
    case PropertyName::FlexGrow:
      if (tokens.size() != 1) {
        return bad_value(ctx, "expected a non-negative number");
      }
      return parse_flex_factor(ctx, tokens[0], PropertyId::FlexGrow, out);
    case PropertyName::FlexShrink:
      if (tokens.size() != 1) {
        return bad_value(ctx, "expected a non-negative number");
      }
      return parse_flex_factor(ctx, tokens[0], PropertyId::FlexShrink, out);
    case PropertyName::FlexBasis:
      return parse_single_dimension(ctx, tokens, PropertyId::FlexBasis, kWidthOptions, out);
    case PropertyName::Color:
      return parse_color_property(ctx, tokens, PropertyId::Color, false, out);
    case PropertyName::FontSize:
      return parse_single_length(ctx, tokens, PropertyId::FontSize, false, out);
    case PropertyName::FontFamily:
      return parse_font_family(ctx, tokens, out);
    case PropertyName::FontWeight:
      return parse_font_weight(ctx, tokens, out);
    case PropertyName::LineHeight:
      return parse_line_height(ctx, tokens, out);
    case PropertyName::LetterSpacing:
      return parse_letter_spacing(ctx, tokens, out);
    case PropertyName::TextAlign:
      return parse_text_align(ctx, tokens, out);
    case PropertyName::LineBreak:
      return parse_line_break(ctx, tokens, out);
    case PropertyName::OverflowWrap:
      return parse_overflow_wrap(ctx, tokens, out);
    case PropertyName::WritingMode:
      return parse_writing_mode(ctx, tokens, out);
  }
  return fail(ErrorKind::Internal, "unhandled CSS property", ctx.location);
}

std::span<const PropertyId> longhands_of(PropertyName property) {
  switch (property) {
    case PropertyName::Margin:
      return kMarginSides;
    case PropertyName::Padding:
      return kPaddingSides;
    case PropertyName::Border:
      return kBorderParts;
    case PropertyName::Flex:
      return kFlexParts;
    case PropertyName::Gap:
      return kGapParts;
    default:
      return {};
  }
}

// ショートハンドでないプロパティの longhand は 1 対 1 で対応する。
PropertyId single_longhand(PropertyName property) {
  switch (property) {
    case PropertyName::Display:
      return PropertyId::Display;
    case PropertyName::Width:
      return PropertyId::Width;
    case PropertyName::Height:
      return PropertyId::Height;
    case PropertyName::MarginTop:
      return PropertyId::MarginTop;
    case PropertyName::MarginRight:
      return PropertyId::MarginRight;
    case PropertyName::MarginBottom:
      return PropertyId::MarginBottom;
    case PropertyName::MarginLeft:
      return PropertyId::MarginLeft;
    case PropertyName::PaddingTop:
      return PropertyId::PaddingTop;
    case PropertyName::PaddingRight:
      return PropertyId::PaddingRight;
    case PropertyName::PaddingBottom:
      return PropertyId::PaddingBottom;
    case PropertyName::PaddingLeft:
      return PropertyId::PaddingLeft;
    case PropertyName::BorderWidth:
      return PropertyId::BorderWidth;
    case PropertyName::BorderStyle:
      return PropertyId::BorderStyle;
    case PropertyName::BorderColor:
      return PropertyId::BorderColor;
    case PropertyName::BorderRadius:
      return PropertyId::BorderRadius;
    case PropertyName::Background:
    case PropertyName::BackgroundColor:
      return PropertyId::BackgroundColor;
    case PropertyName::FlexDirection:
      return PropertyId::FlexDirection;
    case PropertyName::JustifyContent:
      return PropertyId::JustifyContent;
    case PropertyName::AlignItems:
      return PropertyId::AlignItems;
    case PropertyName::RowGap:
      return PropertyId::RowGap;
    case PropertyName::ColumnGap:
      return PropertyId::ColumnGap;
    case PropertyName::FlexGrow:
      return PropertyId::FlexGrow;
    case PropertyName::FlexShrink:
      return PropertyId::FlexShrink;
    case PropertyName::FlexBasis:
      return PropertyId::FlexBasis;
    case PropertyName::Color:
      return PropertyId::Color;
    case PropertyName::FontSize:
      return PropertyId::FontSize;
    case PropertyName::FontFamily:
      return PropertyId::FontFamily;
    case PropertyName::FontWeight:
      return PropertyId::FontWeight;
    case PropertyName::LineHeight:
      return PropertyId::LineHeight;
    case PropertyName::LetterSpacing:
      return PropertyId::LetterSpacing;
    case PropertyName::TextAlign:
      return PropertyId::TextAlign;
    case PropertyName::LineBreak:
      return PropertyId::LineBreak;
    case PropertyName::OverflowWrap:
      return PropertyId::OverflowWrap;
    case PropertyName::WritingMode:
      return PropertyId::WritingMode;
    case PropertyName::Margin:
    case PropertyName::Padding:
    case PropertyName::Border:
    case PropertyName::Gap:
    case PropertyName::Flex:
      break;
  }
  return PropertyId::Display;  // ここには来ない（ショートハンドは longhands_of が拾う）
}

// `inherit` / `initial` はショートハンドなら全 longhand に配る。
void emit_global(PropertyName property, GlobalKeyword keyword, const Ctx& ctx,
                 std::vector<Declaration>& out) {
  const std::span<const PropertyId> parts = longhands_of(property);
  if (parts.empty()) {
    out.push_back(Declaration{.property = single_longhand(property),
                              .global = keyword,
                              .value = std::monostate{},
                              .location = ctx.location});
    return;
  }
  for (const PropertyId part : parts) {
    out.push_back(Declaration{
        .property = part, .global = keyword, .value = std::monostate{}, .location = ctx.location});
  }
}

// `!important` は対応しない。値の中に `!` があれば必ずここで止める。
Result<void> reject_important(const Ctx& ctx, std::span<const ValueToken> tokens) {
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind != ValueToken::Kind::Delim || tokens[i].text != "!") {
      continue;
    }
    const bool important = i + 1 < tokens.size() && tokens[i + 1].kind == ValueToken::Kind::Ident &&
                           ascii_lower(tokens[i + 1].text) == "important";
    return fail(ErrorKind::CssParse,
                important
                    ? std::format("`!important` is not supported (in `{}: {}`)", ctx.name, ctx.raw)
                    : std::format("unexpected `!` in `{}: {}`", ctx.name, ctx.raw),
                ctx.location);
  }
  return {};
}

}  // namespace

Result<void> parse_declaration(std::string_view name, std::string_view raw_value,
                               SourceLocation name_location, SourceLocation value_location,
                               std::vector<Declaration>& out) {
  const std::optional<PropertyName> property = lookup_property(name);
  if (!property) {
    return fail(ErrorKind::UnsupportedProperty,
                std::format("`{}` is not a supported property", name), name_location);
  }

  const Ctx ctx{.name = name, .raw = trim_css_space(raw_value), .location = value_location};
  Result<std::vector<ValueToken>> tokens = tokenize_value(raw_value, value_location);
  if (!tokens) {
    return std::unexpected(tokens.error());
  }
  if (Result<void> important = reject_important(ctx, *tokens); !important) {
    return important;
  }
  if (tokens->empty()) {
    return fail(ErrorKind::CssParse, std::format("missing value for `{}`", name), value_location);
  }

  if (tokens->size() == 1 && (*tokens)[0].kind == ValueToken::Kind::Ident) {
    const std::string lower = ascii_lower((*tokens)[0].text);
    if (lower == "inherit" || lower == "initial") {
      emit_global(*property, lower == "inherit" ? GlobalKeyword::Inherit : GlobalKeyword::Initial,
                  ctx, out);
      return {};
    }
    if (lower == "unset" || lower == "revert") {
      return bad_value(ctx, "supported CSS-wide keywords: inherit, initial");
    }
  }

  return parse_by_name(*property, ctx, *tokens, out);
}

}  // namespace shashoku::style
