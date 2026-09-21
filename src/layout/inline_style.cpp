#include "layout/inline_style.hpp"

#include <cstddef>
#include <tuple>
#include <utility>

namespace shashoku::layout {

text::TextStyle shaping_style_of(const style::ComputedStyle& style, text::Direction direction) {
  return text::TextStyle{.font_family = style.font_family,
                         .font_weight = style.font_weight,
                         .font_size = style.font_size,
                         .direction = direction};
}

linebreak::Strictness resolve_strictness(style::LineBreak value, linebreak::Strictness fallback) {
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
  return fallback;  // A17: エンジンの既定に従う
}

linebreak::Wrap resolve_wrap(style::OverflowWrap value) {
  switch (value) {
    case style::OverflowWrap::Anywhere:
      return linebreak::Wrap::Anywhere;  // min-content にも効く（CSS Text 3 §5.4）
    case style::OverflowWrap::BreakWord:
      return linebreak::Wrap::BreakWord;  // 緊急分割だけ
    case style::OverflowWrap::Normal:
      break;
  }
  return linebreak::Wrap::Normal;
}

std::size_t CharStyleTable::intern(const style::ComputedStyle& style, text::Direction direction,
                                   const SourceLocation& location) {
  text::TextStyle shaping = shaping_style_of(style, direction);
  const auto [shaping_slot, shaping_added] = shaping_index_.try_emplace(shaping, shaping_.size());
  if (shaping_added) {
    shaping_.push_back(std::move(shaping));
  }

  const DecorationStyle decoration{.color = style.color,
                                   .letter_spacing = style.letter_spacing,
                                   .line_height = style.line_height};
  const auto [decoration_slot, decoration_added] =
      decoration_index_.try_emplace(decoration, decoration_.size());
  if (decoration_added) {
    decoration_.push_back(decoration);
  }

  const BreakingStyle breaking{.line_break = style.line_break,
                               .overflow_wrap = style.overflow_wrap};
  const auto [breaking_slot, breaking_added] =
      breaking_index_.try_emplace(breaking, breaking_.size());
  if (breaking_added) {
    breaking_.push_back(breaking);
  }

  const auto [location_slot, location_added] =
      location_index_.try_emplace(location, location_.size());
  if (location_added) {
    location_.push_back(location);
  }

  const CharStyle layers{.shaping = shaping_slot->second,
                         .decoration = decoration_slot->second,
                         .breaking = breaking_slot->second,
                         .location = location_slot->second};
  const auto [style_slot, style_added] = style_index_.try_emplace(
      std::tuple{layers.shaping, layers.decoration, layers.breaking, layers.location},
      styles_.size());
  if (style_added) {
    styles_.push_back(layers);
  }
  return style_slot->second;
}

}  // namespace shashoku::layout
