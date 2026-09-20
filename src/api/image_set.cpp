#include "shashoku/image_set.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shashoku {

void ImageSet::add(std::string name, std::span<const std::uint8_t> png_bytes) {
  images_.push_back(
      Entry{std::move(name), std::vector<std::uint8_t>(png_bytes.begin(), png_bytes.end())});
}

std::size_t ImageSet::size() const noexcept { return images_.size(); }

bool ImageSet::empty() const noexcept { return images_.empty(); }

std::string_view ImageSet::name(std::size_t index) const noexcept {
  if (index >= images_.size()) {
    return {};
  }
  return images_[index].name;
}

std::span<const std::uint8_t> ImageSet::bytes(std::size_t index) const noexcept {
  if (index >= images_.size()) {
    return {};
  }
  return images_[index].bytes;
}

}  // namespace shashoku
