#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shashoku {

// render() に渡す画像の束（ARCHITECTURE.md A12）。
//
// 画像は名前で参照する: `<img src="icon">` はここに `add("icon", ...)` で入れたバイト列を
// 引く。URL もファイルパスも解釈しない（外部リソースを取りに行かないため。DESIGN.md §4）。
// 対応形式は PNG のみ。バイト列の解釈は render() が行い、壊れていれば
// ErrorKind::ImageDecode を返す。同じ名前を 2 回足すと ErrorKind::InvalidOption。
class ImageSet {
 public:
  void add(std::string name, std::span<const std::uint8_t> png_bytes);

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  // 範囲外の index では空になる（例外を投げない）。
  [[nodiscard]] std::string_view name(std::size_t index) const noexcept;
  [[nodiscard]] std::span<const std::uint8_t> bytes(std::size_t index) const noexcept;

 private:
  struct Entry {
    std::string name;
    std::vector<std::uint8_t> bytes;
  };

  std::vector<Entry> images_;
};

}  // namespace shashoku
