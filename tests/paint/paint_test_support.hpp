#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "layout/box_tree.hpp"
#include "paint/display_list_builder.hpp"
#include "raster/display_list.hpp"

// paint のテストはボックスツリーを手で組む（layout を通さない）。
// レイアウトの都合から切り離して「この木ならこの命令列」だけを検証するため。
namespace shashoku::paint::test {

using layout::BlockBox;
using layout::BoxDecoration;
using layout::BoxTree;
using layout::ImageFragment;
using layout::InlineBackground;
using layout::InlineFragment;
using layout::LineBox;
using layout::LogicalRect;
using layout::PositionedGlyph;
using layout::TextFragment;
using layout::WritingMode;

inline LogicalRect lrect(float inline_start, float block_start, float inline_size,
                         float block_size) {
  return LogicalRect{inline_start, block_start, inline_size, block_size};
}

inline BoxDecoration fill(Color color) {
  BoxDecoration decoration;
  decoration.background_color = color;
  return decoration;
}

inline BoxDecoration border(float width, Color color, float radius = 0) {
  BoxDecoration decoration;
  decoration.border_width = width;
  decoration.border_color = color;
  decoration.border_radius = radius;
  return decoration;
}

inline BlockBox block(std::string tag, LogicalRect rect, BoxDecoration decoration,
                      std::vector<BlockBox> blocks) {
  BlockBox box;
  box.tag = std::move(tag);
  box.rect = rect;
  box.decoration = decoration;
  box.children = std::move(blocks);
  return box;
}

inline BlockBox block(std::string tag, LogicalRect rect, BoxDecoration decoration,
                      std::vector<LineBox> lines) {
  BlockBox box;
  box.tag = std::move(tag);
  box.rect = rect;
  box.decoration = decoration;
  box.children = std::move(lines);
  return box;
}

inline LineBox line(LogicalRect rect, float baseline, std::vector<InlineFragment> fragments) {
  LineBox box;
  box.rect = rect;
  box.baseline = baseline;
  box.fragments = std::move(fragments);
  return box;
}

// グリフは [glyph_id, ペン位置の inline 座標] の並びで書く（オフセットは 0）。
inline TextFragment text(FontId font, float font_size, Color color, float baseline,
                         std::initializer_list<std::pair<GlyphId, float>> glyphs) {
  TextFragment fragment;
  fragment.font = font;
  fragment.font_size = font_size;
  fragment.color = color;
  fragment.baseline = baseline;
  for (const auto& [glyph_id, position] : glyphs) {
    fragment.glyphs.push_back(PositionedGlyph{glyph_id, position, 0, 0});
  }
  if (!fragment.glyphs.empty()) {
    fragment.inline_start = fragment.glyphs.front().inline_position;
    fragment.inline_size = fragment.glyphs.back().inline_position + font_size -
                           fragment.glyphs.front().inline_position;
  }
  return fragment;
}

inline BoxTree tree(BlockBox root, float viewport_width = 100,
                    WritingMode mode = WritingMode::HorizontalTb) {
  BoxTree out;
  out.writing_mode = mode;
  out.viewport_width = viewport_width;
  out.root = std::move(root);
  return out;
}

// 命令列の比較。失敗時は JSON ダンプを出して差分を読めるようにする
// （variant の既定の出力ではどの命令が違うのか分からないため）。
inline ::testing::AssertionResult commands_eq(const raster::DisplayList& actual,
                                              const raster::DisplayList& expected) {
  if (actual == expected) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "actual:\n"
                                       << dump_json(actual) << "\nexpected:\n"
                                       << dump_json(expected);
}

}  // namespace shashoku::paint::test
