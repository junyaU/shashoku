#include "layout/check_overflow.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "layout/box_tree.hpp"

namespace shashoku::layout {
namespace {

// 出力の矩形（紙面）。論理座標を paint と同じ読み替え（A1）で物理座標にしてから突き合わせる。
// 縦書きの「下」は論理の inline 方向の終端、「左」は block 方向の終端なので、
// 論理座標のまま判定すると上下左右を取り違える。
//
//   horizontal-tb: x = inline,                     y = block
//   vertical-rl  : x = viewport_width - block_end, y = inline
//
// 高さは `viewport_height` があるときだけ見る。省略（内容追従）なら紙面が内容に合わせて
// 伸びるので、縦方向には定義上はみ出せない（A46 / §3.8）。
// 超過量と、それを出した辺。
struct Excess {
  OverflowEdge edge = OverflowEdge::Right;
  float px = 0;
};

class Canvas {
 public:
  explicit Canvas(const BoxTree& tree)
      : vertical_(tree.writing_mode == WritingMode::VerticalRl),
        width_(tree.viewport_width),
        height_(tree.viewport_height) {}

  // 紙面の外に出た量の最大と、その辺。中に収まっていれば px は 0 以下。
  [[nodiscard]] Excess excess(const LogicalRect& rect) const {
    float left = rect.inline_start;
    float right = rect.inline_end();
    float top = rect.block_start;
    float bottom = rect.block_end();
    if (vertical_) {
      // 箱の物理的な左端は block_end 側（block = 紙面の右端からの距離）
      left = width_ - rect.block_end();
      right = width_ - rect.block_start;
      top = rect.inline_start;
      bottom = rect.inline_end();
    }
    // 同点なら先に見た辺が残る（右 → 下 → 左 → 上）。比較だけなので決定的
    Excess out{.edge = OverflowEdge::Right, .px = right - width_};
    const auto consider = [&out](OverflowEdge edge, float px) {
      if (px > out.px) {
        out = Excess{.edge = edge, .px = px};
      }
    };
    if (height_) {
      consider(OverflowEdge::Bottom, bottom - *height_);
    }
    consider(OverflowEdge::Left, -left);
    if (height_) {
      consider(OverflowEdge::Top, -top);
    }
    return out;
  }

 private:
  bool vertical_;
  float width_;
  std::optional<float> height_;
};

// 報告順（入力位置の昇順）。MissingGlyph::operator< と同じ比べ方で、決定的であること。
bool location_less(const SourceLocation& a, const SourceLocation& b) {
  if (a.offset != b.offset) {
    return a.offset < b.offset;
  }
  if (a.line != b.line) {
    return a.line < b.line;
  }
  return a.column < b.column;
}

// 走査の状態。ルートだけは候補にしない（box_tree.hpp の ContentOverflow を参照）。
struct Pending {
  const BlockBox* box = nullptr;
  bool is_root = false;
};

// 行の中身。行ボックスが収まっていても、置換要素（<img>）は行の外に出られる
// （行ボックスの inline 範囲はブロックの content 幅で固定。box_tree.hpp の LineBox）。
// 文字の断片とインライン背景は見ない: ぶら下げた約物やイタリックの張り出しは
// 「箱がはみ出した」ではないので、ここでは数えない（A46 の「見ないもの」）。
void collect_in_line(const Canvas& canvas, const LineBox& line, const SourceLocation& location,
                     std::vector<ContentOverflow>& out) {
  for (const InlineFragment& fragment : line.fragments) {
    const auto* image = std::get_if<ImageFragment>(&fragment);
    if (image == nullptr) {
      continue;
    }
    const Excess excess = canvas.excess(image->rect);
    if (excess.px > kOverflowTolerancePx) {
      out.push_back(
          ContentOverflow{.location = location, .overflow_px = excess.px, .edge = excess.edge});
    }
  }
}

}  // namespace

std::string_view to_string(OverflowEdge edge) noexcept {
  switch (edge) {
    case OverflowEdge::Right:
      return "right";
    case OverflowEdge::Bottom:
      return "bottom";
    case OverflowEdge::Left:
      return "left";
    case OverflowEdge::Top:
      return "top";
  }
  return "right";  // 到達しない（enum は 4 値）。既定を返して黙って壊れないようにする
}

std::vector<ContentOverflow> collect_overflows(const BoxTree& tree) {
  const Canvas canvas(tree);
  std::vector<ContentOverflow> out;

  // 前順（親 → 子、文書順）。check_geometry と同じく、再帰ではなく明示的なスタックで
  // 入れ子の深い文書（nesting_depth = 256）に備える。
  std::vector<Pending> stack{Pending{.box = &tree.root, .is_root = true}};
  while (!stack.empty()) {
    const Pending pending = stack.back();
    stack.pop_back();
    const BlockBox& box = *pending.box;

    if (!pending.is_root) {
      const Excess excess = canvas.excess(box.rect);
      if (excess.px > kOverflowTolerancePx) {
        // 最も外側の 1 件だけ数える: 中に降りない（子孫は必ず一緒にはみ出している）
        out.push_back(ContentOverflow{
            .location = box.location, .overflow_px = excess.px, .edge = excess.edge});
        continue;
      }
    }

    if (const std::vector<LineBox>* lines = box.lines()) {
      for (const LineBox& line : *lines) {
        // 行ボックスと断片は自分の入力位置を持たないので、含むブロック要素の位置で報告する
        const Excess excess = canvas.excess(line.rect);
        if (excess.px > kOverflowTolerancePx) {
          out.push_back(ContentOverflow{
              .location = box.location, .overflow_px = excess.px, .edge = excess.edge});
          continue;  // 祖先（この行）が出ているので、中の断片は数えない
        }
        collect_in_line(canvas, line, box.location, out);
      }
      continue;
    }
    const std::vector<BlockBox>& blocks = *box.blocks();
    for (std::size_t i = blocks.size(); i > 0; --i) {
      stack.push_back(Pending{.box = &blocks[i - 1], .is_root = false});  // 文書順に取り出す
    }
  }

  // 位置の昇順に並べ、同じ位置は 1 件にまとめる（超過量は最大を採る）。
  // 走査はおおむね文書順だが、flex の並べ替えや負のマージンがあるので並べ直す。
  std::stable_sort(out.begin(), out.end(), [](const ContentOverflow& a, const ContentOverflow& b) {
    return location_less(a.location, b.location);
  });
  std::vector<ContentOverflow> merged;
  merged.reserve(out.size());
  for (const ContentOverflow& overflow : out) {
    if (!merged.empty() && merged.back().location == overflow.location) {
      if (overflow.overflow_px > merged.back().overflow_px) {
        merged.back() = overflow;  // 大きいほうの辺も一緒に残す
      }
      continue;
    }
    merged.push_back(overflow);
  }
  return merged;
}

}  // namespace shashoku::layout
