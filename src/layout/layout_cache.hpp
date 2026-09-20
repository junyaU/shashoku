#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>

#include "layout/engine.hpp"
#include "layout/inline_paragraph.hpp"

// レイアウト 1 回のあいだだけ生きるメモ（ARCHITECTURE.md A29 / issue #5）。
//
// flex は「部分木を測ってから置く」ので、同じ部分木・同じ段落に何度も触る。素直に書くと
// column の入れ子で 2^depth になる。ここに「同じ入力なら同じ結果」を覚えておき、
// **計測**の 2 回目以降を省く。配置（layout_block）は従来どおり 1 回ずつ実行するので、
// 座標の浮動小数点のビット列は変わらない。
//
// 約束（DESIGN.md §3-5「純粋関数」を壊さないための条件）:
//   * ポインタ値は**検索キーにしか使わない**。値も順序も出力には出さない（反復もしない）
//   * float はビット列で比べる。近い値をまとめない = 同じ入力からは同じ出力
//   * グローバル・static を持たない。LayoutEngine が 1 つ所有し、レイアウトが終われば消える
//   * メモが効いた場合と効かない場合で結果は同じ（`layout_without_memo()` が固定する）
namespace shashoku::layout {

// メモの検索キーに float を入れるためのビット列。
[[nodiscard]] inline std::uint32_t float_bits(float value) {
  return std::bit_cast<std::uint32_t>(value);
}

// 部分木の同一性。`BlockInput` のうち**幾何に効く**ものだけを持つ。
// tag と location は箱の名前とエラーメッセージにしか使われないので入れない。
// スタイル付きツリーはレイアウト中に不変なので、ノードのポインタが部分木を一意に表す。
struct SubtreeId {
  const void* style = nullptr;     // ComputedStyle
  const void* children = nullptr;  // children.data()
  const void* replaced = nullptr;  // 置換要素（<img>）のノード
  std::size_t child_count = 0;
  bool anonymous = false;  // 無名ボックスは flex コンテナにならない（layout_block の分岐）

  [[nodiscard]] static SubtreeId of(const BlockInput& input) {
    return SubtreeId{.style = input.style,
                     .children = input.children.data(),
                     .replaced = input.replaced,
                     .child_count = input.children.size(),
                     .anonymous = input.anonymous};
  }
};

// ポインタの全順序は std::less で作る（生の `<` は無関係なポインタ同士では未規定）。
[[nodiscard]] inline bool subtree_less(const SubtreeId& a, const SubtreeId& b) {
  constexpr std::less<> kPtr;  // 透過ファンクタ（const void* 同士の全順序）
  if (a.style != b.style) {
    return kPtr(a.style, b.style);
  }
  if (a.children != b.children) {
    return kPtr(a.children, b.children);
  }
  if (a.replaced != b.replaced) {
    return kPtr(a.replaced, b.replaced);
  }
  if (a.child_count != b.child_count) {
    return a.child_count < b.child_count;
  }
  return static_cast<int>(a.anonymous) < static_cast<int>(b.anonymous);
}

[[nodiscard]] inline bool subtree_equal(const SubtreeId& a, const SubtreeId& b) {
  return a.style == b.style && a.children == b.children && a.replaced == b.replaced &&
         a.child_count == b.child_count && a.anonymous == b.anonymous;
}

// 計測（この部分木をこの条件で組んだときの border-box の block サイズ）の検索キー。
// **結果に効く入力をすべて入れる**（足りないと「黙って違う高さを使う」バグになる）:
// 部分木の同一性 + `BoxSizing` 全部 + 内容の inline 開始位置。
// margin は layout_block が読まないが、将来の取りこぼしを作らないように入れてある。
struct MeasureKey {
  static constexpr std::size_t kNumbers = 13;

  SubtreeId subtree;
  std::array<std::uint32_t, kNumbers> numbers{};

  [[nodiscard]] static MeasureKey of(const BlockInput& input, const BoxSizing& sizing,
                                     float content_inline_start) {
    return MeasureKey{
        .subtree = SubtreeId::of(input),
        .numbers = {float_bits(sizing.margin.inline_start), float_bits(sizing.margin.inline_end),
                    float_bits(sizing.margin.block_start), float_bits(sizing.margin.block_end),
                    float_bits(sizing.padding.inline_start), float_bits(sizing.padding.inline_end),
                    float_bits(sizing.padding.block_start), float_bits(sizing.padding.block_end),
                    float_bits(sizing.border), float_bits(sizing.content_inline_size),
                    float_bits(sizing.content_block_size.value_or(0)),
                    static_cast<std::uint32_t>(sizing.content_block_size.has_value() ? 1 : 0),
                    float_bits(content_inline_start)}};
  }

  [[nodiscard]] bool operator<(const MeasureKey& other) const {
    if (!subtree_equal(subtree, other.subtree)) {
      return subtree_less(subtree, other.subtree);
    }
    return numbers < other.numbers;
  }
};

// 固有寸法（content_intrinsic）の検索キー。結果は部分木と `%` の基準だけで決まる。
struct IntrinsicKey {
  SubtreeId subtree;
  std::uint32_t percent_basis = 0;

  [[nodiscard]] static IntrinsicKey of(const BlockInput& input, float percent_basis) {
    return IntrinsicKey{.subtree = SubtreeId::of(input),
                        .percent_basis = float_bits(percent_basis)};
  }

  [[nodiscard]] bool operator<(const IntrinsicKey& other) const {
    if (!subtree_equal(subtree, other.subtree)) {
      return subtree_less(subtree, other.subtree);
    }
    return percent_basis < other.percent_basis;
  }
};

// 準備済み段落（PreparedParagraph）の検索キー。段落の中身は「どのノードの並びか」と
// 「どのブロックのスタイルで組むか」だけで決まり、**行の幅には依らない**（A6 / A27）。
// ただし <img> を含む段落だけは content_inline_size が入るので共有しない（LayoutCache::share）。
struct ParagraphKey {
  const void* children = nullptr;
  const void* block_style = nullptr;
  std::size_t child_count = 0;

  [[nodiscard]] bool operator<(const ParagraphKey& other) const {
    constexpr std::less<> kPtr;  // 透過ファンクタ（const void* 同士の全順序）
    if (children != other.children) {
      return kPtr(children, other.children);
    }
    if (block_style != other.block_style) {
      return kPtr(block_style, other.block_style);
    }
    return child_count < other.child_count;
  }
};

class LayoutCache {
 public:
  // 計測結果。見つからなければ nullptr。
  [[nodiscard]] const float* measured(const MeasureKey& key) const {
    const auto found = measured_.find(key);
    return found == measured_.end() ? nullptr : &found->second;
  }
  void remember(const MeasureKey& key, float block_size) { measured_.try_emplace(key, block_size); }

  [[nodiscard]] const Intrinsic* intrinsic(const IntrinsicKey& key) const {
    const auto found = intrinsics_.find(key);
    return found == intrinsics_.end() ? nullptr : &found->second;
  }
  void remember(const IntrinsicKey& key, const Intrinsic& value) {
    intrinsics_.try_emplace(key, value);
  }

  [[nodiscard]] const PreparedParagraph* paragraph(const ParagraphKey& key) const {
    const auto found = paragraphs_.find(key);
    return found == paragraphs_.end() ? nullptr : &found->second;
  }
  // 返す参照は LayoutCache が生きているあいだ有効（std::map はノードごとの確保なので、
  // あとから別の段落を足しても既存の要素は動かない）。
  const PreparedParagraph& remember(const ParagraphKey& key, PreparedParagraph&& paragraph) {
    return paragraphs_.try_emplace(key, std::move(paragraph)).first->second;
  }

 private:
  std::map<MeasureKey, float> measured_;
  std::map<IntrinsicKey, Intrinsic> intrinsics_;
  std::map<ParagraphKey, PreparedParagraph> paragraphs_;
};

}  // namespace shashoku::layout
