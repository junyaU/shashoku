#include "layout/flex_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "layout/engine.hpp"

// 単一行 flexbox。CSS Flexbox Level 1 §9 のうち、単一行に要るところだけを実装する。
// 主軸は論理方向で決まる: row の主軸 = inline 方向、column の主軸 = block 方向。
namespace shashoku::layout {
namespace {

using style::AlignItems;
using style::ComputedStyle;
using style::Dimension;
using style::JustifyContent;
using style::StyledNode;

constexpr float kEpsilon = 1.0F / 1024.0F;

struct Item {
  BlockInput input;
  const StyledNode* node = nullptr;      // 無名アイテムは null
  const ComputedStyle* style = nullptr;  // 同上（flex 係数と余白の出どころ）
  bool replaced = false;                 // <img>

  LogicalEdges<float> margin;
  LogicalEdges<bool> margin_auto;
  LogicalEdges<float> padding;
  float border = 0;

  float grow = 0;
  float shrink = 1;
  float base = 0;  // flex base size（主軸・content box）
  float target = 0;
  float min_main = 0;
  bool frozen = false;

  std::optional<float> cross_definite;  // 交差軸 content サイズが確定ならその値
  float cross = 0;                      // column で使う交差軸 content サイズ
  bool stretch = false;
  BlockBox box;
};

// ---- 主軸・交差軸の取り出し ----------------------------------------------------

float main_extra(const Item& item, bool row) {
  const LogicalEdges<float>& p = item.padding;
  return 2 * item.border + (row ? p.inline_start + p.inline_end : p.block_start + p.block_end);
}
float cross_extra(const Item& item, bool row) {
  const LogicalEdges<float>& p = item.padding;
  return 2 * item.border + (row ? p.block_start + p.block_end : p.inline_start + p.inline_end);
}
float margin_main_start(const Item& item, bool row) {
  return row ? item.margin.inline_start : item.margin.block_start;
}
float margin_main_end(const Item& item, bool row) {
  return row ? item.margin.inline_end : item.margin.block_end;
}
float margin_cross_start(const Item& item, bool row) {
  return row ? item.margin.block_start : item.margin.inline_start;
}
float margin_cross_end(const Item& item, bool row) {
  return row ? item.margin.block_end : item.margin.inline_end;
}
bool auto_main_start(const Item& item, bool row) {
  return row ? item.margin_auto.inline_start : item.margin_auto.block_start;
}
bool auto_main_end(const Item& item, bool row) {
  return row ? item.margin_auto.inline_end : item.margin_auto.block_end;
}
bool auto_cross_start(const Item& item, bool row) {
  return row ? item.margin_auto.block_start : item.margin_auto.inline_start;
}
bool auto_cross_end(const Item& item, bool row) {
  return row ? item.margin_auto.block_end : item.margin_auto.inline_end;
}
float outer_main(const Item& item, bool row) {
  return item.target + main_extra(item, row) + margin_main_start(item, row) +
         margin_main_end(item, row);
}
float box_main(const Item& item, bool row) {
  return row ? item.box.rect.inline_size : item.box.rect.block_size;
}
float box_cross(const Item& item, bool row) {
  return row ? item.box.rect.block_size : item.box.rect.inline_size;
}

// ---- アイテムの構築 -------------------------------------------------------------

// CSS Flexbox §4: コンテナの子はそれぞれ flex アイテムになり、連続するテキストは
// 無名の flex アイテムに包まれる。インライン級の要素（span / img）はブロック化する。
Result<std::vector<Item>> build_items(LayoutEngine& engine, const BlockInput& input,
                                      float percent_basis) {
  std::vector<Item> items;
  const std::span<const StyledNode> children = input.children;
  std::size_t i = 0;
  while (i < children.size()) {
    if (classify(children[i]) == ChildKind::Skip) {
      ++i;
      continue;
    }
    // テキストと <ruby> の連続は 1 つの無名アイテムにまとめる。<ruby> を単独の
    // flex アイテムにするとブロック化されて親文字とルビの組が壊れるため。
    const auto inline_like = [](const StyledNode& node) {
      return node.type == StyledNode::Type::Text || node.tag == "ruby";
    };
    if (inline_like(children[i])) {
      std::size_t end = i;
      while (end < children.size() && inline_like(children[end])) {
        ++end;
      }
      const std::span<const StyledNode> run = children.subspan(i, end - i);
      i = end;
      if (is_blank(run)) {
        continue;
      }
      Item anonymous;
      anonymous.input = BlockInput{.tag = "#anonymous",
                                   .style = input.style,
                                   .children = run,
                                   .location = run.front().location,
                                   .anonymous = true};
      items.push_back(std::move(anonymous));
      continue;
    }
    const StyledNode& child = children[i];
    ++i;
    if (child.tag == "rt") {
      return fail(ErrorKind::UnsupportedLayout, "<rt> is only allowed inside <ruby>",
                  child.location);
    }
    Item item;
    item.input = input_of(child);
    item.node = &child;
    item.style = &child.style;
    item.replaced = item.input.replaced != nullptr;
    items.push_back(std::move(item));
  }

  for (Item& item : items) {
    if (item.style == nullptr) {
      continue;  // 無名アイテムは flex: 0 1 auto・余白なし
    }
    item.margin = engine.resolve_margin(*item.style, percent_basis);
    item.margin_auto = engine.margin_is_auto(*item.style);
    item.padding = engine.map().edges(item.style->padding);
    item.border = std::max(item.style->border_width, 0.0F);
    item.grow = std::max(item.style->flex_grow, 0.0F);
    item.shrink = std::max(item.style->flex_shrink, 0.0F);
  }
  return items;
}

// アイテムの content-box の固有 inline サイズ。
Result<Intrinsic> item_intrinsic(LayoutEngine& engine, const Item& item, float percent_basis) {
  if (item.node == nullptr) {
    return engine.content_intrinsic(item.input, percent_basis);
  }
  if (!item.replaced) {
    const Dimension width = engine.map().inline_size(*item.style);
    if (!width.is_auto()) {
      const float value = std::max(resolve_length(width, percent_basis), 0.0F);
      return Intrinsic{.min_content = value, .max_content = value};
    }
  }
  return engine.content_intrinsic(item.input, percent_basis);
}

// 主軸のサイズプロパティ（row なら width、column なら height）。
// §4.5 の specified size suggestion はこれで決まり、**flex-basis は入らない**（A52）。
Dimension main_size_property(const Item& item, const LogicalMap& map, bool row) {
  if (item.style == nullptr) {
    return Dimension::auto_();
  }
  return row ? map.inline_size(*item.style) : map.block_size(*item.style);
}

// flex base size（§9.2）を決める指定。flex-basis が auto なら主軸のサイズプロパティ。
Dimension main_dimension(const Item& item, const LogicalMap& map, bool row) {
  if (item.style == nullptr) {
    return Dimension::auto_();
  }
  const Dimension basis = item.style->flex_basis;
  return basis.is_auto() ? main_size_property(item, map, row) : basis;
}

// ---- 交差軸のサイズ -------------------------------------------------------------

// 交差軸のサイズを決めるプロパティ（row なら height、column なら width）。
// 無名アイテムは自分のスタイルを持たないので auto。
Dimension cross_property(const Item& item, const LogicalMap& map, bool row) {
  if (item.style == nullptr) {
    return Dimension::auto_();
  }
  return row ? map.block_size(*item.style) : map.inline_size(*item.style);
}

Result<void> prepare_cross_row(LayoutEngine& engine, std::vector<Item>& items, AlignItems align,
                               float percent_basis) {
  for (Item& item : items) {
    if (item.replaced) {
      Result<ResolvedImage> image = engine.resolve_image(*item.node, percent_basis);
      if (!image) {
        return std::unexpected(image.error());
      }
      item.cross_definite = image->block_size;  // <img> は stretch でも歪めない
      continue;
    }
    const Dimension height = cross_property(item, engine.map(), true);
    if (!height.is_auto() && height.kind != Dimension::Kind::Percent) {
      item.cross_definite = std::max(height.value, 0.0F);
    }
    const bool auto_margin = auto_cross_start(item, true) || auto_cross_end(item, true);
    item.stretch = align == AlignItems::Stretch && !item.cross_definite && !auto_margin;
  }
  return {};
}

// column の交差軸は inline 方向。幅が変わると中身の折り返しが変わるので先に決める。
Result<void> prepare_cross_column(LayoutEngine& engine, std::vector<Item>& items, AlignItems align,
                                  float cross_size) {
  for (Item& item : items) {
    const float available = cross_size - margin_cross_start(item, false) -
                            margin_cross_end(item, false) - cross_extra(item, false);
    if (item.replaced) {
      Result<ResolvedImage> image = engine.resolve_image(*item.node, cross_size);
      if (!image) {
        return std::unexpected(image.error());
      }
      item.cross = image->inline_size;
      item.cross_definite = item.cross;
      continue;
    }
    const Dimension width = cross_property(item, engine.map(), false);
    const bool auto_margin = auto_cross_start(item, false) || auto_cross_end(item, false);
    if (!width.is_auto()) {
      item.cross = std::max(resolve_length(width, cross_size), 0.0F);
    } else if (align == AlignItems::Stretch && !auto_margin) {
      item.cross = std::max(available, 0.0F);
      item.stretch = true;
    } else {
      Result<Intrinsic> intrinsic = item_intrinsic(engine, item, cross_size);
      if (!intrinsic) {
        return std::unexpected(intrinsic.error());
      }
      // shrink-to-fit = min(max-content, max(利用可能幅, min-content))
      item.cross = std::max(
          std::min(intrinsic->max_content, std::max(available, intrinsic->min_content)), 0.0F);
    }
    item.cross_definite = item.cross;
  }
  return {};
}

// ---- flex base size と自動最小サイズ ---------------------------------------------

Result<void> prepare_base_row(LayoutEngine& engine, std::vector<Item>& items, float main_size) {
  for (Item& item : items) {
    // 自動最小サイズは「中身の min-content」で決まる。width が確定していてもそれは
    // 上限（§4.5 の content-based minimum size = min(指定サイズ, 内容サイズ)）にしかならない
    Result<Intrinsic> content = engine.content_intrinsic(item.input, main_size);
    if (!content) {
      return std::unexpected(content.error());
    }
    const Dimension main = main_dimension(item, engine.map(), true);
    item.base =
        main.is_auto() ? content->max_content : std::max(resolve_length(main, main_size), 0.0F);
    // §4.5 の specified size suggestion は **width**。flex-basis は使わない（A52）。
    // row の `%` は常に確定した基準（コンテナの content 幅）で解けるので definite として扱う
    const Dimension width = main_size_property(item, engine.map(), true);
    item.min_main = width.is_auto() ? content->min_content
                                    : std::min(content->min_content,
                                               std::max(resolve_length(width, main_size), 0.0F));
    if (item.replaced) {
      item.min_main = std::max(content->min_content, 0.0F);  // 画像は内容サイズより縮めない
    }
  }
  return {};
}

Result<void> prepare_base_column(LayoutEngine& engine, std::vector<Item>& items, float main_size,
                                 bool main_definite) {
  for (Item& item : items) {
    float content_height = 0;
    if (item.replaced) {
      Result<ResolvedImage> image = engine.resolve_image(*item.node, item.cross);
      if (!image) {
        return std::unexpected(image.error());
      }
      content_height = image->block_size;
    } else {
      // 交差軸（幅）を決めた上で内容の高さを測る。ここで組んだ箱は捨てるので、
      // 同じ条件の 2 回目以降は組み直さない（A29。組み直していたのが #5 の 2^depth）
      BoxSizing measure;
      measure.padding = item.padding;
      measure.border = item.border;
      measure.content_inline_size = item.cross;
      const Result<float> block_size =
          engine.measure_block_size(item.input, measure, item.border + item.padding.inline_start);
      if (!block_size) {
        return std::unexpected(block_size.error());
      }
      content_height = std::max(*block_size - main_extra(item, false), 0.0F);
    }
    const Dimension main = main_dimension(item, engine.map(), false);
    if (main.is_auto()) {
      item.base = content_height;
    } else if (main.kind == Dimension::Kind::Percent) {
      item.base = main_definite ? std::max(resolve_length(main, main_size), 0.0F) : content_height;
    } else {
      item.base = std::max(main.value, 0.0F);
    }
    // §4.5 の specified size suggestion は **height**。flex-basis は使わない（A52）。
    // 主軸が不定（高さ auto の column）なら `%` の height は definite ではないので、
    // 内容の高さがそのまま下限になる
    const Dimension height = main_size_property(item, engine.map(), false);
    const bool height_definite =
        !height.is_auto() && (height.kind != Dimension::Kind::Percent || main_definite);
    item.min_main = height_definite ? std::min(content_height,
                                               std::max(resolve_length(height, main_size), 0.0F))
                                    : content_height;
  }
  return {};
}

// ---- §9.7 主軸サイズの解決 --------------------------------------------------------

// 未凍結アイテムを base、凍結アイテムを target として数えたときの残り自由空間。
float remaining_free_space(const std::vector<Item>& items, bool row, float main_size, float gaps) {
  float remaining = main_size - gaps;
  for (const Item& item : items) {
    remaining -= (item.frozen ? item.target : item.base) + main_extra(item, row) +
                 margin_main_start(item, row) + margin_main_end(item, row);
  }
  return remaining;
}

struct Factors {
  float scaled = 0;  // grow なら flex-grow、shrink なら flex-shrink × 基準サイズ
  float raw = 0;
  std::size_t unfrozen = 0;
};

Factors unfrozen_factors(const std::vector<Item>& items, bool growing) {
  Factors out;
  for (const Item& item : items) {
    if (item.frozen) {
      continue;
    }
    ++out.unfrozen;
    out.raw += growing ? item.grow : item.shrink;
    out.scaled += growing ? item.grow : item.shrink * item.base;
  }
  return out;
}

void freeze_all(std::vector<Item>& items) {
  for (Item& item : items) {
    item.frozen = true;
  }
}

// §9.7-2: 伸び縮みしないアイテムを凍結する。
void freeze_inflexible(std::vector<Item>& items, bool growing) {
  for (Item& item : items) {
    const float hypothetical = std::max(item.base, item.min_main);
    const float factor = growing ? item.grow : item.shrink;
    if (factor <= 0 || (!growing && item.base < hypothetical - kEpsilon)) {
      item.target = hypothetical;
      item.frozen = true;
      continue;
    }
    item.target = item.base;
    item.frozen = false;
  }
}

// §9.7-4: 自由空間を 1 回配分して最小サイズ違反を直す。全部凍結したら true。
bool distribute_once(std::vector<Item>& items, bool row, bool growing, float main_size, float gaps,
                     float initial_free) {
  const Factors factors = unfrozen_factors(items, growing);
  if (factors.unfrozen == 0) {
    return true;
  }
  float remaining = remaining_free_space(items, row, main_size, gaps);
  // §9.7-4b: 係数の合計が 1 未満なら、初期自由空間にその合計を掛けた値を上限にする
  if (factors.raw < 1) {
    const float scaled = initial_free * factors.raw;
    if (std::abs(scaled) < std::abs(remaining)) {
      remaining = scaled;
    }
  }
  if (factors.scaled <= 0) {
    freeze_all(items);
    return true;
  }

  std::vector<float> unclamped(items.size(), 0);
  for (std::size_t i = 0; i < items.size(); ++i) {
    Item& item = items[i];
    if (item.frozen) {
      continue;
    }
    const float factor = growing ? item.grow : item.shrink * item.base;
    item.target = item.base + (remaining * (factor / factors.scaled));
    unclamped[i] = item.target;
  }
  // §9.7-4d: 最小サイズ違反を直す（最大サイズは無いので違反は常に 0 以上）
  float violation = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].frozen) {
      continue;
    }
    items[i].target = std::max(items[i].target, items[i].min_main);
    violation += items[i].target - unclamped[i];
  }
  if (violation <= kEpsilon) {
    freeze_all(items);
    return true;
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (!items[i].frozen && items[i].target > unclamped[i] + kEpsilon) {
      items[i].frozen = true;
    }
  }
  return false;
}

void resolve_flexible_lengths(std::vector<Item>& items, bool row, bool main_definite,
                              float main_size, float gap) {
  for (Item& item : items) {
    item.target = std::max(item.base, item.min_main);
    item.frozen = true;
  }
  if (!main_definite || items.empty()) {
    return;  // 主軸が不定（高さ auto の column）なら伸び縮みしない
  }

  const float gaps = gap * static_cast<float>(items.size() - 1);
  float hypothetical_sum = gaps;
  for (const Item& item : items) {
    hypothetical_sum += outer_main(item, row);
  }
  const bool growing = hypothetical_sum < main_size;

  freeze_inflexible(items, growing);
  const float initial_free = remaining_free_space(items, row, main_size, gaps);
  for (std::size_t guard = 0; guard <= items.size() + 1; ++guard) {
    if (distribute_once(items, row, growing, main_size, gaps, initial_free)) {
      return;
    }
  }
}

// ---- 配置 -----------------------------------------------------------------------

Result<void> layout_items(LayoutEngine& engine, std::vector<Item>& items, bool row) {
  for (Item& item : items) {
    BoxSizing sizing;
    sizing.margin = item.margin;
    sizing.padding = item.padding;
    sizing.border = item.border;
    if (row) {
      sizing.content_inline_size = item.target;
      sizing.content_block_size = item.cross_definite;
    } else {
      sizing.content_inline_size = item.cross;
      sizing.content_block_size = item.target;
    }
    // border-box の原点を (0, 0) に置いて組み、あとで最終位置へ平行移動する
    Result<BlockBox> box =
        engine.layout_block(item.input, sizing, item.border + item.padding.inline_start, 0);
    if (!box) {
      return std::unexpected(box.error());
    }
    item.box = std::move(*box);
  }
  return {};
}

// 単一行なので「行の交差サイズ」= コンテナの交差サイズ。row の stretch はここで適用する。
float resolve_line_cross(std::vector<Item>& items, bool row, bool cross_definite,
                         float cross_size) {
  float line_cross = cross_size;
  if (!cross_definite) {
    line_cross = 0;
    for (const Item& item : items) {
      line_cross = std::max(line_cross, box_cross(item, row) + margin_cross_start(item, row) +
                                            margin_cross_end(item, row));
    }
  }
  if (!row) {
    return line_cross;  // column の stretch は幅なので、組む前に反映ずみ
  }
  for (Item& item : items) {
    if (item.stretch) {  // 箱の高さを伸ばすだけ。中身は上詰めのまま（再レイアウト不要）
      item.box.rect.block_size =
          std::max(line_cross - margin_cross_start(item, row) - margin_cross_end(item, row), 0.0F);
    }
  }
  return line_cross;
}

struct MainPlacement {
  float offset = 0;
  float between = 0;
  float auto_share = 0;
};

MainPlacement place_main(const std::vector<Item>& items, bool row, JustifyContent justify,
                         float gap, float container_main) {
  MainPlacement out;
  out.between = gap;
  float used = gap * static_cast<float>(items.size() - 1);
  std::size_t autos = 0;
  for (const Item& item : items) {
    used += box_main(item, row) + margin_main_start(item, row) + margin_main_end(item, row);
    autos += static_cast<std::size_t>(auto_main_start(item, row)) +
             static_cast<std::size_t>(auto_main_end(item, row));
  }
  const float free = container_main - used;
  if (free <= 0) {
    return out;  // 余りが無い（あふれている）: 先頭詰め
  }
  if (autos > 0) {
    out.auto_share = free / static_cast<float>(autos);  // auto マージンが先に吸収する
    return out;
  }
  const auto count = static_cast<float>(items.size());
  switch (justify) {
    case JustifyContent::FlexStart:
      break;
    case JustifyContent::FlexEnd:
      out.offset = free;
      break;
    case JustifyContent::Center:
      out.offset = free / 2;
      break;
    case JustifyContent::SpaceBetween:
      if (items.size() > 1) {
        out.between += free / (count - 1);
      }
      break;
    case JustifyContent::SpaceAround:
      out.offset = free / count / 2;
      out.between += free / count;
      break;
    case JustifyContent::SpaceEvenly:
      out.offset = free / (count + 1);
      out.between += free / (count + 1);
      break;
  }
  return out;
}

// 交差軸の位置（align-items と交差軸の auto マージン）。
float cross_position(const Item& item, bool row, AlignItems align, float content_cross_start,
                     float line_cross) {
  const float outer =
      box_cross(item, row) + margin_cross_start(item, row) + margin_cross_end(item, row);
  const float free = line_cross - outer;
  const std::size_t autos = static_cast<std::size_t>(auto_cross_start(item, row)) +
                            static_cast<std::size_t>(auto_cross_end(item, row));
  if (autos > 0 && free > 0) {
    const float share = free / static_cast<float>(autos);
    return content_cross_start + margin_cross_start(item, row) +
           (auto_cross_start(item, row) ? share : 0);
  }
  switch (align) {
    case AlignItems::FlexEnd:
      return content_cross_start + line_cross - margin_cross_end(item, row) - box_cross(item, row);
    case AlignItems::Center:
      return content_cross_start + (free / 2) + margin_cross_start(item, row);
    case AlignItems::Stretch:
    case AlignItems::FlexStart:
      break;
  }
  return content_cross_start + margin_cross_start(item, row);
}

// 主軸に沿って並べ、交差軸に寄せてから、各アイテムを最終位置へ平行移動する。
std::vector<BlockBox> place_items(std::vector<Item>& items, bool row, AlignItems align,
                                  const MainPlacement& placement, float content_main_start,
                                  float content_cross_start, float line_cross) {
  std::vector<BlockBox> boxes;
  boxes.reserve(items.size());
  float cursor = content_main_start + placement.offset;
  for (std::size_t i = 0; i < items.size(); ++i) {
    Item& item = items[i];
    if (i > 0) {
      cursor += placement.between;
    }
    cursor +=
        margin_main_start(item, row) + (auto_main_start(item, row) ? placement.auto_share : 0);
    const float main_position = cursor;
    cursor += box_main(item, row) + margin_main_end(item, row) +
              (auto_main_end(item, row) ? placement.auto_share : 0);
    const float cross_pos = cross_position(item, row, align, content_cross_start, line_cross);
    translate(item.box, row ? main_position : cross_pos, row ? cross_pos : main_position);
    boxes.push_back(std::move(item.box));
  }
  return boxes;
}

// 主軸が不定（高さ auto の column）のときのコンテナの主軸サイズ = 内容の合計。
float content_main_size(const std::vector<Item>& items, bool row, float gap) {
  if (items.empty()) {
    return 0;
  }
  float total = gap * static_cast<float>(items.size() - 1);
  for (const Item& item : items) {
    total += box_main(item, row) + margin_main_start(item, row) + margin_main_end(item, row);
  }
  return total;
}

}  // namespace

Result<Intrinsic> flex_intrinsic(LayoutEngine& engine, const BlockInput& input,
                                 float percent_basis) {
  Result<std::vector<Item>> built = build_items(engine, input, percent_basis);
  if (!built) {
    return std::unexpected(built.error());
  }
  const std::vector<Item>& items = *built;
  const bool row = input.style->flex_direction == style::FlexDirection::Row;
  const float gap = std::max(row ? input.style->column_gap : input.style->row_gap, 0.0F);

  Intrinsic out;
  for (const Item& item : items) {
    Result<Intrinsic> inner = item_intrinsic(engine, item, percent_basis);
    if (!inner) {
      return std::unexpected(inner.error());
    }
    // 固有寸法は inline 方向の話なので、足す余白も inline 方向のもの
    const float extra = (2 * item.border) + item.padding.inline_start + item.padding.inline_end +
                        item.margin.inline_start + item.margin.inline_end;
    if (row) {
      out.min_content += inner->min_content + extra;
      out.max_content += inner->max_content + extra;
    } else {
      out.min_content = std::max(out.min_content, inner->min_content + extra);
      out.max_content = std::max(out.max_content, inner->max_content + extra);
    }
  }
  if (row && items.size() > 1) {
    const float gaps = gap * static_cast<float>(items.size() - 1);
    out.min_content += gaps;
    out.max_content += gaps;
  }
  return out;
}

Result<BlockBox> layout_flex(LayoutEngine& engine, const BlockInput& input, const BoxSizing& sizing,
                             float content_inline_start, float block_start) {
  const bool row = input.style->flex_direction == style::FlexDirection::Row;
  const float content_block_start = block_start + sizing.border + sizing.padding.block_start;
  const float gap = std::max(row ? input.style->column_gap : input.style->row_gap, 0.0F);
  const bool main_definite = row || sizing.content_block_size.has_value();
  const float main_size = row ? sizing.content_inline_size : sizing.content_block_size.value_or(0);
  const bool cross_definite = !row || sizing.content_block_size.has_value();
  const float cross_size = row ? sizing.content_block_size.value_or(0) : sizing.content_inline_size;
  const AlignItems align = input.style->align_items;

  Result<std::vector<Item>> built = build_items(engine, input, sizing.content_inline_size);
  if (!built) {
    return std::unexpected(built.error());
  }
  std::vector<Item> items = std::move(*built);

  const Result<void> prepared =
      row ? prepare_cross_row(engine, items, align, sizing.content_inline_size)
          : prepare_cross_column(engine, items, align, cross_size);
  if (!prepared) {
    return std::unexpected(prepared.error());
  }
  const Result<void> based = row ? prepare_base_row(engine, items, main_size)
                                 : prepare_base_column(engine, items, main_size, main_definite);
  if (!based) {
    return std::unexpected(based.error());
  }
  resolve_flexible_lengths(items, row, main_definite, main_size, gap);
  if (const Result<void> laid = layout_items(engine, items, row); !laid) {
    return std::unexpected(laid.error());
  }

  const float line_cross = resolve_line_cross(items, row, cross_definite, cross_size);
  const float container_main = main_definite ? main_size : content_main_size(items, row, gap);
  const MainPlacement placement =
      items.empty() ? MainPlacement{}
                    : place_main(items, row, input.style->justify_content, gap, container_main);
  std::vector<BlockBox> boxes =
      place_items(items, row, align, placement, row ? content_inline_start : content_block_start,
                  row ? content_block_start : content_inline_start, line_cross);

  const float content_block_size =
      sizing.content_block_size.value_or(row ? line_cross : container_main);
  BlockBox box;
  box.tag = input.tag;
  box.location = input.location;
  if (!input.anonymous) {
    box.decoration = BoxDecoration{.background_color = input.style->background_color,
                                   .border_width = sizing.border,
                                   .border_color = input.style->border_color,
                                   .border_radius = std::max(input.style->border_radius, 0.0F)};
  }
  box.padding = sizing.padding;
  box.rect = LogicalRect{
      .inline_start = content_inline_start - sizing.border - sizing.padding.inline_start,
      .block_start = block_start,
      .inline_size = sizing.content_inline_size + 2 * sizing.border + sizing.padding.inline_start +
                     sizing.padding.inline_end,
      .block_size = content_block_size + 2 * sizing.border + sizing.padding.block_start +
                    sizing.padding.block_end};
  box.children = std::move(boxes);
  return box;
}

}  // namespace shashoku::layout
