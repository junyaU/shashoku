#include "layout/check_geometry.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/number_text.hpp"
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
  SourceLocation location;       // いま見ている箱・断片の位置
  std::string_view tag;          // 箱のタグ（"#root" / "div" / "#anonymous"）
  Counters* counters = nullptr;  // 見た数を足し込む（A21。出力には影響しない）
};

std::unexpected<Error> violation(const Scan& scan, std::string_view what, float value) {
  // 「どの上限を・いくつに対して・いくつだったか」を出す（A25 の流儀）。上限は
  // `length_px x dom_nodes` で導いた値なので、緩め方が分かるようにその式も書く。
  // 値の表記は number_text() を通す: NaN の符号ビットは CPU によって違うので、
  // そのまま出すと同じ入力でも文面が環境で変わる（core/number_text.hpp）。
  const std::string detail =
      std::isfinite(value) ? std::format("{} px, which exceeds the limit of {} px",
                                         number_text(value), number_text(scan.max_px))
                           : std::format("{}, which a float cannot represent; the limit is {} px",
                                         number_text(value), number_text(scan.max_px));
  return fail(ErrorKind::LimitExceeded,
              std::format("the laid out `{}` has {} = {} (RenderLimits::length_px x dom_nodes); "
                          "`%` and flex factors are resolved in layout, so a value that was "
                          "finite in the computed style can still blow up here",
                          scan.tag, what, detail),
              scan.location);
}

// 矩形 1 つ（位置 2 つ + 大きさ 2 つ）。名前は box ダンプの並びに合わせる。
Result<void> check_rect(const Scan& scan, std::string_view name, const LogicalRect& rect) {
  const std::array<std::pair<std::string_view, float>, 4> fields = {{
      {"inline_start", rect.inline_start},
      {"block_start", rect.block_start},
      {"inline_size", rect.inline_size},
      {"block_size", rect.block_size},
  }};
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
  ++scan.counters->geometry_nodes;
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
    ++scan.counters->geometry_nodes;
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
  ++scan.counters->geometry_nodes;
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
      ++scan.counters->geometry_nodes;
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
    ++scan.counters->geometry_nodes;
    if (const Result<void> ok = check_rect(scan, "background", background.rect); !ok) {
      return ok;
    }
  }
  return {};
}

// 前順（親 → 子、文書順）に辿る。再帰ではなく明示的なスタックにして、
// 入れ子の深い文書（nesting_depth = 256）でもスタックを使い切らないようにする。
Result<void> check_block_tree(const BlockBox& root, float max_px, Counters& counters) {
  std::vector<const BlockBox*> stack{&root};
  while (!stack.empty()) {
    const BlockBox& box = *stack.back();
    stack.pop_back();

    ++counters.geometry_nodes;
    const Scan scan{
        .max_px = max_px, .location = box.location, .tag = box.tag, .counters = &counters};
    if (const Result<void> ok = check_rect(scan, "rect", box.rect); !ok) {
      return ok;
    }
    if (const Result<void> ok = check_decoration(scan, box.decoration); !ok) {
      return ok;
    }
    const LogicalEdges<float>& padding = box.padding;
    const std::array<std::pair<std::string_view, float>, 4> edges = {{
        {"padding.inline_start", padding.inline_start},
        {"padding.inline_end", padding.inline_end},
        {"padding.block_start", padding.block_start},
        {"padding.block_end", padding.block_end},
    }};
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

Result<void> check_geometry(const BoxTree& tree, float max_geometry_px, Counters& counters) {
  return check_block_tree(tree.root, max_geometry_px, counters);
}

}  // namespace shashoku::layout
