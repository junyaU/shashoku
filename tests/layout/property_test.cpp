#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

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
  explicit Generator(std::uint32_t seed) : rng_(seed) {}

  std::size_t pick(std::size_t count) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng_);
  }
  float pick_float(float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(rng_);
  }
  bool chance(double probability) { return std::bernoulli_distribution(probability)(rng_); }

  // available は生成中に分かる利用可能幅。子ブロックが親の content からはみ出さないよう、
  // 幅と余白はこの値から選ぶ（不変条件 (3) を「はみ出さない入力」で検査するため）。
  // NOLINTNEXTLINE(misc-no-recursion): テスト用の木の生成
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
      if (depth > 0 && chance(0.35)) {
        children.push_back(make_block(depth - 1, inner));
        continue;
      }
      children.push_back(make_inline());
    }
    return block(std::move(children), [=](ComputedStyle& style) {
      style.padding = {padding, padding, padding, padding};
      style.border_width = border;
      style.margin = {Dimension::px(margin_block), Dimension::px(margin_end),
                      Dimension::px(margin_block), Dimension::px(margin_start)};
      if (percent_width) {
        style.width = Dimension::percent(percent);
      }
      style.font_size = font_size;
    });
  }

  Tree make_inline() {
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

// NOLINTNEXTLINE(misc-no-recursion): テスト用の木の走査
void collect_source_text(const style::StyledNode& node, std::string& out) {
  if (node.type == style::StyledNode::Type::Text) {
    out += node.text;
    return;
  }
  for (const style::StyledNode& child : node.children) {
    collect_source_text(child, out);
  }
}

// NOLINTNEXTLINE(misc-no-recursion): テスト用の木の走査
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

// NOLINTNEXTLINE(misc-no-recursion): テスト用の木の走査
void check_geometry(const BlockBox& box) {
  const std::vector<BlockBox>* blocks = box.blocks();
  if (blocks == nullptr) {
    return;
  }
  const LogicalRect content = box.content_rect();
  const BlockBox* previous = nullptr;
  for (const BlockBox& child : *blocks) {
    // (3) 子ブロックは親の content 領域の inline 範囲に収まる
    EXPECT_GE(child.rect.inline_start, content.inline_start - kTolerance) << box.tag;
    EXPECT_LE(child.rect.inline_end(), content.inline_end() + kTolerance) << box.tag;
    EXPECT_GE(child.rect.inline_size, 0);
    EXPECT_GE(child.rect.block_size, 0);
    // (4) 兄弟ブロックは block 方向に重ならない（マージンは非負なので相殺しても離れる）
    if (previous != nullptr) {
      EXPECT_LE(previous->rect.block_end(), child.rect.block_start + kTolerance) << box.tag;
    }
    previous = &child;
    check_geometry(child);
  }
}

TEST(LayoutProperty, RandomTreesKeepTheInvariants) {
  for (std::uint32_t seed = 1; seed <= 200; ++seed) {
    Generator generator(seed);
    const float viewport = generator.pick_float(80, 800);
    std::vector<Tree> children;
    const std::size_t count = 1 + generator.pick(3);
    for (std::size_t i = 0; i < count; ++i) {
      children.push_back(generator.make_block(3, viewport));
    }
    const style::StyledNode root = build(std::move(children));

    FakeMeasurer measurer;
    const auto tree = run_layout(root, viewport, measurer);
    ASSERT_TRUE(tree.has_value()) << "seed " << seed << ": " << to_string(tree.error());

    // (2) 入力のクラスタはちょうど 1 回ずつどこかの行に現れる（空白の畳み込みぶんを除く）
    std::string source;
    collect_source_text(root, source);
    std::string laid_out;
    collect_laid_out_text(tree->root, laid_out);
    EXPECT_EQ(without_spaces(laid_out), without_spaces(source)) << "seed " << seed;

    // (3)(4) 幾何の不変条件
    check_geometry(tree->root);
    EXPECT_FLOAT_EQ(tree->root.rect.inline_size, viewport) << "seed " << seed;

    // (5) 同じ入力 → 同じ出力
    FakeMeasurer again;
    const auto twice = run_layout(root, viewport, again);
    ASSERT_TRUE(twice.has_value());
    EXPECT_EQ(dump_json(*tree), dump_json(*twice)) << "seed " << seed;
  }
}

}  // namespace
}  // namespace shashoku::layout::test
