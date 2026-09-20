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

bool resolve_break_anywhere(style::OverflowWrap value) {
  return value != style::OverflowWrap::Normal;
}

std::size_t CharStyleTable::intern(const style::ComputedStyle& style, text::Direction direction) {
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

  const CharStyle layers{.shaping = shaping_slot->second,
                         .decoration = decoration_slot->second,
                         .breaking = breaking_slot->second};
  const auto [style_slot, style_added] = style_index_.try_emplace(
      std::tuple{layers.shaping, layers.decoration, layers.breaking}, styles_.size());
  if (style_added) {
    styles_.push_back(layers);
  }
  return style_slot->second;
}

}  // namespace shashoku::layout
