#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "layout/engine.hpp"

// <img>（ARCHITECTURE.md §3.8 / A12）。
namespace shashoku::layout {
namespace {

using style::Dimension;
using style::StyledNode;

// 片側だけ決まっているときに、固有寸法の縦横比でもう片側を出す。
// 固有寸法に 0 が含まれていたら比が取れないので、固有寸法をそのまま使う。
float keep_ratio(float known, float known_intrinsic, float other_intrinsic) {
  if (known_intrinsic <= 0) {
    return other_intrinsic;
  }
  return known / known_intrinsic * other_intrinsic;
}

}  // namespace

Result<ResolvedImage> LayoutEngine::resolve_image(const StyledNode& node,
                                                  float percent_basis) const {
  const std::optional<ImageInfo> info = (*images_)(node.image_src);
  if (!info) {
    return fail(ErrorKind::ImageNotFound, "no image named `" + node.image_src + "` was given",
                node.location);
  }

  // CSS の width / height（物理）> width / height 属性 > 固有寸法
  const Dimension css_width = node.style.width;
  const Dimension css_height = node.style.height;
  if (css_height.kind == Dimension::Kind::Percent) {
    return fail(ErrorKind::UnsupportedValue, "percentage height is not supported", node.location);
  }
  std::optional<float> width;
  std::optional<float> height;
  if (!css_width.is_auto()) {
    width = std::max(resolve_length(css_width, percent_basis), 0.0F);
  } else if (node.attr_width) {
    width = std::max(*node.attr_width, 0.0F);
  }
  if (!css_height.is_auto()) {
    height = std::max(css_height.value, 0.0F);
  } else if (node.attr_height) {
    height = std::max(*node.attr_height, 0.0F);
  }

  const float intrinsic_width = std::max(info->width, 0.0F);
  const float intrinsic_height = std::max(info->height, 0.0F);
  if (!width && !height) {
    width = intrinsic_width;
    height = intrinsic_height;
  } else if (!width) {
    width = keep_ratio(*height, intrinsic_height, intrinsic_width);
  } else if (!height) {
    height = keep_ratio(*width, intrinsic_width, intrinsic_height);
  }

  return ResolvedImage{.id = info->id,
                       .inline_size = map_.inline_of(*width, *height),
                       .block_size = map_.block_of(*width, *height)};
}

// display: block の <img>（と flex アイテムになった <img>）。
// 「画像断片 1 個だけの行を持つブロック」で表す（box_tree.hpp の型を増やさない）。
// 塗りは ImageFragment 側が持つので BlockBox の decoration / padding は空にする
// （両方に入れると paint が背景と枠線を 2 回描いてしまう）。
// 行には支柱を入れない = 行の高さ = 画像の高さ。画像の下にディセンダぶんのすき間を作らない。
Result<BlockBox> LayoutEngine::layout_image_box(const BlockInput& input, const BoxSizing& sizing,
                                                float content_inline_start,
                                                float block_start) const {
  const Result<ResolvedImage> image = resolve_image(*input.replaced, 0);
  if (!image) {
    return std::unexpected(image.error());
  }
  const float content_block_start = block_start + sizing.border + sizing.padding.block_start;
  const float block_size = sizing.content_block_size.value_or(0);
  const LogicalRect border_box{
      .inline_start = content_inline_start - sizing.border - sizing.padding.inline_start,
      .block_start = block_start,
      .inline_size = sizing.content_inline_size + 2 * sizing.border + sizing.padding.inline_start +
                     sizing.padding.inline_end,
      .block_size =
          block_size + 2 * sizing.border + sizing.padding.block_start + sizing.padding.block_end};

  ImageFragment fragment{
      .image = image->id,
      .rect = border_box,
      .content_rect = LogicalRect{.inline_start = content_inline_start,
                                  .block_start = content_block_start,
                                  .inline_size = sizing.content_inline_size,
                                  .block_size = block_size},
      .decoration = BoxDecoration{.background_color = input.style->background_color,
                                  .border_width = sizing.border,
                                  .border_color = input.style->border_color,
                                  .border_radius = std::max(input.style->border_radius, 0.0F)}};

  LineBox line;
  line.rect = border_box;
  line.baseline = border_box.block_end();  // margin-box の下端 = ベースライン
  line.fragments.emplace_back(fragment);   // 自明にコピーできる小さな型

  BlockBox box;
  box.tag = input.tag;
  box.location = input.location;
  box.rect = border_box;
  box.children = std::vector<LineBox>{std::move(line)};
  return box;
}

}  // namespace shashoku::layout
