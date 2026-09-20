#include "paint/display_list_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "layout/box_tree.hpp"
#include "raster/display_list.hpp"

namespace shashoku::paint {
namespace {

using layout::BlockBox;
using layout::BoxDecoration;
using layout::ImageFragment;
using layout::InlineBackground;
using layout::LineBox;
using layout::LogicalRect;
using layout::PositionedGlyph;
using layout::TextFragment;
using layout::WritingMode;

// ---------------------------------------------------------------------------
// 論理座標 → 物理座標（ARCHITECTURE.md A1）
//
// レイアウトの出力は inline（字送り）/ block（行送り）の論理座標で、ルート原点からの
// 絶対位置（box_tree.hpp）。物理座標は左上原点・+x 右・+y 下（core/geometry.hpp）。
//
//   horizontal-tb: x = inline,                     y = block
//   vertical-rl  : x = viewport_width − block_end, y = inline
//
// 縦書きでは block が「紙面の右端からの距離」なので、右端から引くと物理 x になる。
// 箱の左端は block_end（= block_start + block_size）側であることに注意。
// ---------------------------------------------------------------------------
class Axes {
 public:
  Axes(WritingMode mode, float viewport_width)
      : vertical_(mode == WritingMode::VerticalRl), viewport_width_(viewport_width) {}

  [[nodiscard]] Rect rect(const LogicalRect& r) const {
    if (!vertical_) {
      return Rect{r.inline_start, r.block_start, r.inline_size, r.block_size};
    }
    return Rect{viewport_width_ - r.block_end(), r.inline_start, r.block_size, r.inline_size};
  }

  // ペン位置（横書き: ベースライン上の点 / 縦書き: 行の中心軸上の点）。
  [[nodiscard]] Point pen(float inline_position, float block_position) const {
    if (!vertical_) {
      return Point{inline_position, block_position};
    }
    return Point{viewport_width_ - block_position, inline_position};
  }

 private:
  bool vertical_;
  float viewport_width_;
};

class Builder {
 public:
  Builder(WritingMode mode, float viewport_width) : axes_(mode, viewport_width) {}

  raster::DisplayList build(const BlockBox& root) {
    paint_block(root);
    return std::move(list_);
  }

 private:
  // 命令を 1 つ積む。グリフ以外の命令は DrawGlyphs のまとめを打ち切る
  // （間に背景や画像が挟まると描画順が変わってしまうため）。
  void emit(raster::DrawCmd command) {
    open_run_.reset();
    list_.push_back(std::move(command));
  }

  void paint_background(const LogicalRect& rect, const BoxDecoration& decoration) {
    if (decoration.background_color.transparent()) {
      return;
    }
    if (decoration.border_radius > 0) {
      emit(raster::FillRoundedRect{axes_.rect(rect), decoration.border_radius,
                                   decoration.background_color});
      return;
    }
    emit(raster::FillRect{axes_.rect(rect), decoration.background_color});
  }

  void paint_border(const LogicalRect& rect, const BoxDecoration& decoration) {
    if (decoration.border_width <= 0 || decoration.border_color.transparent()) {
      return;
    }
    emit(raster::StrokeRoundedRect{axes_.rect(rect), decoration.border_radius,
                                   decoration.border_width, decoration.border_color});
  }

  void append_glyphs(raster::DrawGlyphs& run, const TextFragment& fragment) const {
    run.glyphs.reserve(run.glyphs.size() + fragment.glyphs.size());
    for (const PositionedGlyph& glyph : fragment.glyphs) {
      // グリフ原点 = ペン位置 + (x_offset, y_offset)。オフセットは物理 px のまま運ばれる
      // （box_tree.hpp）ので、変換せずに足す。
      const Point pen = axes_.pen(glyph.inline_position, fragment.baseline);
      run.glyphs.emplace_back(glyph.glyph_id,
                              Point{pen.x + glyph.x_offset, pen.y + glyph.y_offset});
    }
  }

  void paint_text(const TextFragment& fragment) {
    // 何も描かない断片は命令を出さない。まとめの途中を打ち切る必要もない
    // （描かないので、前後の断片をまとめても描画結果は変わらない）。
    if (fragment.glyphs.empty() || fragment.color.transparent()) {
      return;
    }
    if (open_run_) {
      auto& run = std::get<raster::DrawGlyphs>(list_[*open_run_]);
      if (run.font == fragment.font && run.size == fragment.font_size &&
          run.color == fragment.color && run.sideways == fragment.sideways) {
        append_glyphs(run, fragment);
        return;
      }
    }
    raster::DrawGlyphs run;
    run.font = fragment.font;
    run.size = fragment.font_size;
    run.color = fragment.color;
    run.sideways = fragment.sideways;
    append_glyphs(run, fragment);
    open_run_ = list_.size();
    list_.emplace_back(std::move(run));
  }

  // 描画順は box_tree.hpp の ImageFragment のコメントどおり:
  // 背景 → （角丸があれば内周でクリップして）画像 → 枠線。
  void paint_image(const ImageFragment& fragment) {
    paint_background(fragment.rect, fragment.decoration);
    const bool clipped = fragment.decoration.border_radius > 0;
    if (clipped) {
      // 枠線の内周の角丸（display_list.hpp の StrokeRoundedRect と同じ式）。
      const float inner =
          std::max(0.0F, fragment.decoration.border_radius - fragment.decoration.border_width);
      emit(raster::PushClip{axes_.rect(fragment.content_rect), inner});
    }
    emit(raster::DrawImage{fragment.image, axes_.rect(fragment.content_rect)});
    if (clipped) {
      emit(raster::PopClip{});
    }
    paint_border(fragment.rect, fragment.decoration);
  }

  void paint_line(const LineBox& line) {
    open_run_.reset();
    for (const layout::InlineFragment& fragment : line.fragments) {
      std::visit(
          [this](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TextFragment>) {
              paint_text(value);
            } else if constexpr (std::is_same_v<T, InlineBackground>) {
              if (!value.color.transparent()) {
                emit(raster::FillRect{axes_.rect(value.rect), value.color});
              }
            } else {
              paint_image(value);
            }
          },
          fragment);
    }
    open_run_.reset();
  }

  void paint_block(const BlockBox& box) {
    paint_background(box.rect, box.decoration);
    paint_border(box.rect, box.decoration);
    if (const std::vector<BlockBox>* blocks = box.blocks()) {
      for (const BlockBox& child : *blocks) {
        paint_block(child);
      }
      return;
    }
    if (const std::vector<LineBox>* lines = box.lines()) {
      for (const LineBox& line : *lines) {
        paint_line(line);
      }
    }
  }

  Axes axes_;
  raster::DisplayList list_;
  // list_ の中の「まだ継ぎ足せる DrawGlyphs」の位置。
  std::optional<std::size_t> open_run_;
};

}  // namespace

raster::DisplayList build_display_list(const layout::BoxTree& tree) {
  return Builder(tree.writing_mode, tree.viewport_width).build(tree.root);
}

}  // namespace shashoku::paint
