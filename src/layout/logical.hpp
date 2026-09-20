#pragma once

#include "core/geometry.hpp"
#include "layout/box_tree.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::layout {

// A1 の読み替え表。CSS の物理プロパティ（width / height / margin-top …）を
// 論理方向（inline / block）に直すのは **この 1 箇所だけ**。
// 縦書き（Phase 8）で増えるのはこの表への 1 分岐で、レイアウト本体は触らない。
//
//   horizontal-tb: inline = 横（左 → 右）、block = 縦（上 → 下）
//   vertical-rl  : inline = 縦（上 → 下）、block = 横（右 → 左）
class LogicalMap {
 public:
  explicit LogicalMap(WritingMode mode) : vertical_(mode == WritingMode::VerticalRl) {}

  [[nodiscard]] bool vertical() const { return vertical_; }

  // 字送り方向の大きさを決めるプロパティ（横書き: width、縦書き: height）。
  [[nodiscard]] style::Dimension inline_size(const style::ComputedStyle& style) const {
    return vertical_ ? style.height : style.width;
  }
  // 行送り方向の大きさを決めるプロパティ（横書き: height、縦書き: width）。
  [[nodiscard]] style::Dimension block_size(const style::ComputedStyle& style) const {
    return vertical_ ? style.width : style.height;
  }

  // 上右下左（CSS の並び）→ inline-start / inline-end / block-start / block-end。
  template <class T>
  [[nodiscard]] LogicalEdges<T> edges(const Edges<T>& physical) const {
    if (vertical_) {
      // vertical-rl: inline は上 → 下、block は右 → 左
      return LogicalEdges<T>{.inline_start = physical.top,
                             .inline_end = physical.bottom,
                             .block_start = physical.right,
                             .block_end = physical.left};
    }
    return LogicalEdges<T>{.inline_start = physical.left,
                           .inline_end = physical.right,
                           .block_start = physical.top,
                           .block_end = physical.bottom};
  }

  // 物理的な大きさ（<img> の固有寸法など）を論理方向に読み替える。
  [[nodiscard]] float inline_of(float width, float height) const {
    return vertical_ ? height : width;
  }
  [[nodiscard]] float block_of(float width, float height) const {
    return vertical_ ? width : height;
  }

  // シェーピングに渡す字送り方向。
  [[nodiscard]] text::Direction direction() const {
    return vertical_ ? text::Direction::Vertical : text::Direction::Horizontal;
  }

 private:
  bool vertical_ = false;
};

}  // namespace shashoku::layout
