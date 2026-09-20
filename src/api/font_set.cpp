#include "shashoku/font_set.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace shashoku {

void FontSet::add(std::span<const std::uint8_t> font_bytes) {
  fonts_.emplace_back(font_bytes.begin(), font_bytes.end());
}

std::size_t FontSet::size() const noexcept { return fonts_.size(); }

bool FontSet::empty() const noexcept { return fonts_.empty(); }

std::span<const std::uint8_t> FontSet::at(std::size_t index) const noexcept {
  if (index >= fonts_.size()) {
    return {};
  }
  return fonts_[index];
}

}  // namespace shashoku
