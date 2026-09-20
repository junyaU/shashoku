#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace shashoku {

// render() に渡すフォントの束（DESIGN.md §8）。
//
// **追加順がフォールバック順**（ARCHITECTURE.md §3.5）: `font-family` で名前を指定した
// フォントが先、その後は追加順に全フォントを試す。日本語の文章では「欧文フォント →
// 和文フォント」の順に足すのが定石。
//
// このクラスはバイト列をコピーして持つだけで、解釈はしない（フォントとして正しいかは
// render() が判定し、壊れていれば ErrorKind::FontLoad を返す）。ネットワークにも
// ファイルシステムにも触れないので、読み込みは呼び出し側の仕事（DESIGN.md §4）。
class FontSet {
 public:
  void add(std::span<const std::uint8_t> font_bytes);

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  // 範囲外の index では空になる（例外を投げない）。
  [[nodiscard]] std::span<const std::uint8_t> at(std::size_t index) const noexcept;

 private:
  std::vector<std::vector<std::uint8_t>> fonts_;
};

}  // namespace shashoku
