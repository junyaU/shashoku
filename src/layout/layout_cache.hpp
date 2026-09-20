#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <utility>

#include "layout/engine.hpp"
#include "layout/inline_paragraph.hpp"

// レイアウト 1 回のあいだだけ生きるメモ（ARCHITECTURE.md A29 / issue #5）。
//
// flex は「部分木を測ってから置く」ので、同じ部分木・同じ段落に何度も触る。ここに
// 「同じ入力なら同じ結果」を覚えておき、**計測**の 2 回目以降を省く。配置（layout_block）は
// 従来どおり 1 ずつ実行するので、座標の浮動小数点のビット列は変わらない。
//
// 約束（DESIGN.md §3-5「純粋関数」を壊さないための条件）:
//   * ポインタ値は**検索キーにしか使わない**。値も順序も出力には出さない（反復もしない）
//   * float はビット列で比べる。近い値をまとめない = 同じ入力からは同じ出力
//   * グローバル・static を持たない。LayoutEngine が 1 つ所有し、レイアウトが終われば消える
//   * メモが効いた場合と効かない場合で結果は同じ（`layout_without_memo()` が固定する）
namespace shashoku::layout {

// 準備済み段落（PreparedParagraph）の検索キー。段落の中身は「どのノードの並びか」と
// 「どのブロックのスタイルで組むか」だけで決まり、**行の幅には依らない**（A6 / A27）。
// ただし <img> を含む段落だけは content_inline_size が入るので共有しない（shared_paragraph）。
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
  std::map<ParagraphKey, PreparedParagraph> paragraphs_;
};

}  // namespace shashoku::layout
