#include "layout/check_geometry.hpp"

#include <cmath>
#include <format>
#include <string_view>
#include <variant>
#include <vector>

#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "shashoku/error.hpp"

namespace shashoku::layout {
namespace {

// 上限の判定は 1 つの比較で済ませる: NaN も ±inf も範囲外もまとめて false になる
// （style 側の `within()` と同じ形。A36 / A9）。
bool within(float value, float max_px) { return value >= -max_px && value <= max_px; }

// 検査の文脈。違反したときだけ文字列を作る（走査は要素数に比例して回る）。
struct Scan {
  float max_px = 0;
  SourceLocation location;   // いま見ている箱・断片の位置
  std::string_view tag;      // 箱のタグ（"#root" / "div" / "#anonymous"）
};

std::unexpected<Error> violation(const Scan& scan, std::string_view what, float value) {
  const std::string_view reason = std::isfinite(value)
                                      ? "which exceeds the limit"
                                      : "which a float cannot represent (the limit";
  return fail(ErrorKind::LimitExceeded,
              std::format("the laid out `{}` has {} = {} px, {} of {} px; `%` and flex factors "
                          "are resolved here, so a value that was finite in the computed style "
                          "can still blow up (raise RenderLimits::length_px to allow it)",
                          scan.tag, what, value, reason, scan.max_px),
              scan.location);
}

// 矩形 1 つ（位置 2 つ + 大きさ 2 つ）。名前は box ダンプの並びに合わせる。
Result<void> check_rect(const Scan& scan, std::string_view name, const LogicalRect& rect) {
  const std::pair<std::string_view, float> fields[] = {
      {"inline_start", rect.inline_start},
      {"block_start", rect.block_start},
      {"inline_size", rect.inline_size},
      {"block_size", rect.block_size},
  };
  for (const auto& [field, value] : fields) {
    if (!within(value, scan.max_px)) {
      return violation(scan, std::format("{}.{}", name, field), value);
    }
  }
  // 端（= 位置 + 大きさ）も見る: 片方ずつは収まっていても、足すと超えることがある。
  // paint はこの端を使うので、ここが壊れていると絵が消える。
  if (!within(rect.inline_end(), scan.max_px)) {
    return violation(scan, std::format("{}.inline_end", name), rect.inline_end());
  }
  if (!within(rect.block_end(), scan.max_px)) {
    return violation(scan, std::format("{}.block_end", name), rect.block_end());
  }
  return {};
}

Result<void> check_decoration(const Scan& scan, const BoxDecoration& decoration) {
  if (!within(decoration.border_width, scan.max_px)) {
    return violation(scan, "border_width", decoration.border_width);
  }
  if (!within(decoration.border_radius, scan.max_px)) {
    return violation(scan, "border_radius", decoration.border_radius);
  }
  return {};
}

Result<void> check_text_fragment(const Scan& block_scan, const TextFragment& fragment) {
  // 断片は自分の位置を持っている（A31）ので、そちらで報告する方が近い。
  Scan scan = block_scan;
  scan.location = fragment.location;
  if (!within(fragment.font_size, scan.max_px)) {
    return violation(scan, "text.font_size", fragment.font_size);
  }
  if (!within(fragment.baseline, scan.max_px)) {
    return violation(scan, "text.baseline", fragment.baseline);
  }
  if (!within(fragment.inline_start, scan.max_px)) {
    return violation(scan, "text.inline_start", fragment.inline_start);
  }
  if (!within(fragment.inline_size, scan.max_px)) {
    return violation(scan, "text.inline_size", fragment.inline_size);
  }
  for (const PositionedGlyph& glyph : fragment.glyphs) {
    if (!within(glyph.inline_position, scan.max_px)) {
      return violation(scan, "glyph.inline_position", glyph.inline_position);
    }
    if (!within(glyph.x_offset, scan.max_px)) {
      return violation(scan, "glyph.x_offset", glyph.x_offset);
    }
    if (!within(glyph.y_offset, scan.max_px)) {
      return violation(scan, "glyph.y_offset", glyph.y_offset);
    }
  }
  return {};
}

Result<void> check_line(const Scan& scan, const LineBox& line) {
  if (const Result<void> ok = check_rect(scan, "line", line.rect); !ok) {
    return ok;
  }
  if (!within(line.baseline, scan.max_px)) {
    return violation(scan, "line.baseline", line.baseline);
  }
  for (const InlineFragment& fragment : line.fragments) {
    if (const auto* text = std::get_if<TextFragment>(&fragment)) {
      if (const Result<void> ok = check_text_fragment(scan, *text); !ok) {
        return ok;
      }
      continue;
    }
    if (const auto* image = std::get_if<ImageFragment>(&fragment)) {
      if (const Result<void> ok = check_rect(scan, "image", image->rect); !ok) {
        return ok;
      }
      if (const Result<void> ok = check_rect(scan, "image.content", image->content_rect); !ok) {
        return ok;
      }
      if (const Result<void> ok = check_decoration(scan, image->decoration); !ok) {
        return ok;
      }
      continue;
    }
    const auto& background = std::get<InlineBackground>(fragment);
    if (const Result<void> ok = check_rect(scan, "background", background.rect); !ok) {
      return ok;
    }
  }
  return {};
}

// 前順（親 → 子、文書順）に辿る。再帰ではなく明示的なスタックにして、
// 入れ子の深い文書（nesting_depth = 256）でもスタックを使い切らないようにする。
Result<void> check_block_tree(const BlockBox& root, float max_px) {
  std::vector<const BlockBox*> stack{&root};
  while (!stack.empty()) {
    const BlockBox& box = *stack.back();
    stack.pop_back();

    const Scan scan{.max_px = max_px, .location = box.location, .tag = box.tag};
    if (const Result<void> ok = check_rect(scan, "rect", box.rect); !ok) {
      return ok;
    }
    if (const Result<void> ok = check_decoration(scan, box.decoration); !ok) {
      return ok;
    }
    const LogicalEdges<float>& padding = box.padding;
    const std::pair<std::string_view, float> edges[] = {
        {"padding.inline_start", padding.inline_start},
        {"padding.inline_end", padding.inline_end},
        {"padding.block_start", padding.block_start},
        {"padding.block_end", padding.block_end},
    };
    for (const auto& [field, value] : edges) {
      if (!within(value, max_px)) {
        return violation(scan, field, value);
      }
    }

    if (const std::vector<LineBox>* lines = box.lines()) {
      for (const LineBox& line : *lines) {
        if (const Result<void> ok = check_line(scan, line); !ok) {
          return ok;
        }
      }
      continue;
    }
    const std::vector<BlockBox>& blocks = *box.blocks();
    for (std::size_t i = blocks.size(); i > 0; --i) {
      stack.push_back(&blocks[i - 1]);  // 文書順に取り出せるよう逆順に積む
    }
  }
  return {};
}

}  // namespace

Result<void> check_geometry(const BoxTree& tree, float max_geometry_px) {
  return check_block_tree(tree.root, max_geometry_px);
}

}  // namespace shashoku::layout
