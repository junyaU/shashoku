#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/geometry.hpp"
#include "layout/test_support.hpp"

// 性質テスト（DESIGN.md §10-4）。種を固定した乱数でランダムなスタイル付きツリーを作り、
// 「落ちない」だけでなくレイアウトの不変条件を検査する。乱数は入力の生成にだけ使い、
// レイアウトそのものは純粋関数のまま（同じ入力 → 同じ出力も検査する）。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;

constexpr float kTolerance = 1.0F / 512.0F;

constexpr auto kWords = std::to_array<std::string_view>({
    "あいうえお",
    "漢字とかな",
    "ABC def",
    "こんにちは、世界。",
    "「引用」です",
    "a\nb",
    "あ\nい",
    "  余白  ",
    "テスト",
    "long-word-without-spaces",
});

class Generator {
 public:
  Generator(std::uint32_t seed, bool vertical) : vertical_(vertical), rng_(seed) {}

  std::size_t pick(std::size_t count) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng_);
  }
  float pick_float(float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(rng_);
  }
  bool chance(double probability) { return std::bernoulli_distribution(probability)(rng_); }

  // available は生成中に分かる利用可能幅。子ブロックが親の content からはみ出さないよう、
  // 幅と余白はこの値から選ぶ（不変条件 (3) を「はみ出さない入力」で検査するため）。
  Tree make_block(int depth, float available) {
    const float unit = available / 16;
    const float padding = chance(0.4) ? pick_float(0, unit) : 0;
    const float border = chance(0.3) ? pick_float(0, unit / 2) : 0;
    const float margin_start = chance(0.4) ? pick_float(0, unit) : 0;
    const float margin_end = chance(0.4) ? pick_float(0, unit) : 0;
    const float margin_block = chance(0.5) ? pick_float(0, unit * 2) : 0;
    const float extra = 2 * padding + 2 * border + margin_start + margin_end;
    const bool percent_width = chance(0.3) && available - extra > 0;
    const float percent = pick_float(10, 100) * (available - extra) / available;
    const float font_size = chance(0.3) ? pick_float(8, 32) : 16;

    std::vector<Tree> children;
    const std::size_t count = pick(4);
    for (std::size_t i = 0; i < count; ++i) {
      const float inner = content_width(available, extra, percent_width, percent);
      if (depth > 0 && chance(0.2)) {
        children.push_back(make_flex(depth - 1, inner));
        continue;
      }
      if (depth > 0 && chance(0.35)) {
        children.push_back(make_block(depth - 1, inner));
        continue;
      }
      children.push_back(make_inline());
    }
    const bool vertical = vertical_;
    return block(std::move(children), [=](ComputedStyle& style) {
      style.padding = {padding, padding, padding, padding};
      style.border_width = border;
      // margin は inline 方向の 2 辺（横書き: 左右 / 縦書き: 上下）を start / end に使う
      style.margin =
          vertical ? Edges<Dimension>{Dimension::px(margin_start), Dimension::px(margin_block),
                                      Dimension::px(margin_end), Dimension::px(margin_block)}
                   : Edges<Dimension>{Dimension::px(margin_block), Dimension::px(margin_end),
                                      Dimension::px(margin_block), Dimension::px(margin_start)};
      if (percent_width) {
        // inline 方向のサイズを決めるプロパティは書字方向で入れ替わる（A1）
        (vertical ? style.height : style.width) = Dimension::percent(percent);
      }
      style.font_size = font_size;
    });
  }

  // flex コンテナ。検査側が「ブロックの積み上げ」と区別できるよう、タグを "flex" にする
  // （タグはボックスツリーではデバッグ用の文字列で、レイアウトの挙動には効かない）。
  Tree make_flex(int depth, float available) {
    constexpr auto kJustify = std::to_array<style::JustifyContent>({
        style::JustifyContent::FlexStart,
        style::JustifyContent::FlexEnd,
        style::JustifyContent::Center,
        style::JustifyContent::SpaceBetween,
        style::JustifyContent::SpaceAround,
        style::JustifyContent::SpaceEvenly,
    });
    constexpr auto kAlign = std::to_array<style::AlignItems>({
        style::AlignItems::Stretch,
        style::AlignItems::FlexStart,
        style::AlignItems::FlexEnd,
        style::AlignItems::Center,
    });
    const bool column = chance(0.4);
    const float gap = chance(0.5) ? pick_float(0, available / 32) : 0;
    const auto justify = kJustify[pick(kJustify.size())];
    const auto align = kAlign[pick(kAlign.size())];
    const bool fixed_height = chance(0.4);
    const float height = pick_float(20, 120);

    std::vector<Tree> children;
    const std::size_t count = 1 + pick(3);
    for (std::size_t i = 0; i < count; ++i) {
      if (chance(0.2)) {
        children.push_back(img("p"));
        continue;
      }
      if (depth > 0 && chance(0.3)) {
        children.push_back(make_block(depth - 1, available / static_cast<float>(count)));
        continue;
      }
      // flex アイテムは縮められるようにしておく（はみ出しの検査は別のテストで見る）
      const float grow = chance(0.5) ? 1 : 0;
      children.push_back(block({make_inline()}, [grow](ComputedStyle& style) {
        style.flex_grow = grow;
        style.flex_basis = Dimension::px(0);
      }));
    }
    const bool vertical = vertical_;
    return element("flex", style::Display::Flex, std::move(children), [=](ComputedStyle& style) {
      style.flex_direction = column ? style::FlexDirection::Column : style::FlexDirection::Row;
      style.justify_content = justify;
      style.align_items = align;
      style.column_gap = gap;
      style.row_gap = gap;
      if (fixed_height) {
        // block 方向のサイズだけを確定させる（縦書きでは width がその役）。inline 方向を
        // 勝手に確定させると親からはみ出してしまう
        (vertical ? style.width : style.height) = Dimension::px(height);
      }
    });
  }

  Tree make_inline() {
    if (chance(0.1)) {
      return img("p");
    }
    if (chance(0.12)) {
      // ルビ 1 組 / 2 組 / ルビなしの親文字つき
      if (chance(0.5)) {
        return ruby({text("漢"), rt("かん")});
      }
      return ruby({text("東"), rt("とう"), text("京"), rt("きょう"), text("都")});
    }
    if (chance(0.15)) {
      return br();
    }
    Tree leaf = text(std::string(kWords[pick(kWords.size())]));
    if (!chance(0.4)) {
      return leaf;
    }
    const float font_size = pick_float(8, 32);
    const auto color = static_cast<std::uint8_t>(pick(256));
    return inline_box({std::move(leaf)}, [=](ComputedStyle& style) {
      style.font_size = font_size;
      style.color = Color{color, 0, 0, 255};
    });
  }

 private:
  static float content_width(float available, float extra, bool percent_width, float percent) {
    if (percent_width) {
      return percent / 100 * available;
    }
    return available - extra > 0 ? available - extra : 0;
  }

  bool vertical_ = false;
  std::mt19937 rng_;
};

// 空白を落とした文字列（クラスタの取りこぼし・重複の検査に使う）。
std::string without_spaces(std::string_view text) {
  std::string out;
  for (const char byte : text) {
    if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f') {
      out.push_back(byte);
    }
  }
  return out;
}

void collect_source_text(const style::StyledNode& node, std::string& out) {
  if (node.type == style::StyledNode::Type::Text) {
    out += node.text;
    return;
  }
  for (const style::StyledNode& child : node.children) {
    collect_source_text(child, out);
  }
}

void collect_laid_out_text(const BlockBox& box, std::string& out) {
  if (const std::vector<LineBox>* lines = box.lines()) {
    for (const LineBox& line : *lines) {
      for (const TextFragment* fragment : text_fragments(line)) {
        out += fragment->text;
      }
    }
    return;
  }
  for (const BlockBox& child : *box.blocks()) {
    collect_laid_out_text(child, out);
  }
}

void check_geometry(const BlockBox& box) {
  const std::vector<BlockBox>* blocks = box.blocks();
  if (blocks == nullptr) {
    return;
  }
  const bool flex = box.tag == "flex";
  const LogicalRect content = box.content_rect();
  const BlockBox* previous = nullptr;
  for (const BlockBox& child : *blocks) {
    // (3) ブロックの積み上げでは、子は親の content 領域の inline 範囲に収まる。
    // flex では収まらないことがあるので検査しない（自動最小サイズによる end 側のあふれと、
    // align-items: center / flex-end で交差軸がはみ出す「unsafe」な寄せ。どちらも CSS どおり）。
    if (!flex) {
      EXPECT_GE(child.rect.inline_start, content.inline_start - kTolerance) << box.tag;
      EXPECT_LE(child.rect.inline_end(), content.inline_end() + kTolerance)
          << box.tag << " > " << child.tag;
    }
    EXPECT_GE(child.rect.inline_size, 0);
    EXPECT_GE(child.rect.block_size, 0);
    // (4) 兄弟は重ならない。ブロックの積み上げと flex column は block 方向に、
    // flex row は inline 方向に離れる（どちらかの軸で必ず離れている）
    if (previous != nullptr) {
      const bool block_disjoint = previous->rect.block_end() <= child.rect.block_start + kTolerance;
      const bool inline_disjoint =
          previous->rect.inline_end() <= child.rect.inline_start + kTolerance;
      EXPECT_TRUE(block_disjoint || (flex && inline_disjoint)) << box.tag;
    }
    previous = &child;
    check_geometry(child);
  }
}

// 1 つの種で木を作り、指定の書字方向で組んで不変条件を検査する。
void check_seed(std::uint32_t seed, bool vertical) {
  Generator generator(seed, vertical);
  const float inline_size = generator.pick_float(80, 800);
  std::vector<Tree> children;
  const std::size_t count = 1 + generator.pick(3);
  for (std::size_t i = 0; i < count; ++i) {
    children.push_back(generator.make_block(3, inline_size));
  }
  // 縦書きでは inline 方向が高さなので、viewport の高さを行の長さにする
  const style::StyledNode root =
      vertical ? build_vertical(std::move(children)) : build(std::move(children));
  const Options options = vertical ? vertical_options(400, inline_size) : make_options(inline_size);

  FakeMeasurer measurer;
  const ImageLookup images = image_table({{.src = "p", .id = 1, .width = 24, .height = 12}});
  const auto tree = run_layout(root, options, measurer, images);
  ASSERT_TRUE(tree.has_value()) << "seed " << seed << ": " << to_string(tree.error());

  // (2) 入力のクラスタはちょうど 1 回ずつどこかの行に現れる（空白の畳み込みぶんを除く）
  std::string source;
  collect_source_text(root, source);
  std::string laid_out;
  collect_laid_out_text(tree->root, laid_out);
  EXPECT_EQ(without_spaces(laid_out), without_spaces(source)) << "seed " << seed;

  // (3)(4) 幾何の不変条件
  SCOPED_TRACE(testing::Message() << "seed " << seed << (vertical ? " vertical" : " horizontal"));
  check_geometry(tree->root);
  EXPECT_FLOAT_EQ(tree->root.rect.inline_size, inline_size) << "seed " << seed;

  // (5) 同じ入力 → 同じ出力
  FakeMeasurer again;
  const auto twice = run_layout(root, options, again, images);
  ASSERT_TRUE(twice.has_value());
  EXPECT_EQ(dump_json(*tree), dump_json(*twice)) << "seed " << seed;
}

TEST(LayoutProperty, RandomTreesKeepTheInvariants) {
  for (std::uint32_t seed = 1; seed <= 150; ++seed) {
    check_seed(seed, false);
  }
}

// 縦書きでも同じ不変条件が成り立つ（論理座標のまま組んでいるので、軸が入れ替わるだけ）。
TEST(LayoutProperty, RandomTreesKeepTheInvariantsInVerticalWritingMode) {
  for (std::uint32_t seed = 1; seed <= 150; ++seed) {
    check_seed(seed, true);
  }
}

}  // namespace
}  // namespace shashoku::layout::test
