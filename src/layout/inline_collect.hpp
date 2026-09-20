#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/color.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/engine.hpp"
#include "layout/inline_layout.hpp"
#include "layout/inline_style.hpp"

// インライン整形文脈の (a)（ARCHITECTURE.md §3.8）: 木をほどいて 1 本にし、空白を畳み込む。
// フォントにもシェーピングにも触れない段（背景スコープの block 方向の広がりを出すために
// metrics() だけは呼ぶ）。次の段は inline_paragraph.hpp。
namespace shashoku::layout {

// 「添字なし」。ルビ組・画像・シェーピング結果を指さない欄に入れる。
inline constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// 行の中に流れる <img>。行分割器から見れば分割不能な箱（ItemKind::Atomic）。
struct ImagePiece {
  ImageId id = 0;
  LogicalEdges<float> margin;
  LogicalEdges<float> padding;
  float border = 0;
  float content_inline_size = 0;
  float content_block_size = 0;
  BoxDecoration decoration;

  [[nodiscard]] float border_inline() const {
    return content_inline_size + (2 * border) + padding.inline_start + padding.inline_end;
  }
  [[nodiscard]] float border_block() const {
    return content_block_size + (2 * border) + padding.block_start + padding.block_end;
  }
  [[nodiscard]] float margin_inline() const {
    return border_inline() + margin.inline_start + margin.inline_end;
  }
  [[nodiscard]] float margin_block() const {
    return border_block() + margin.block_start + margin.block_end;
  }
};

// 平坦化したインライン列の 1 文字。属性は CharStyleTable の添字 1 本で持つ（#8）。
struct FlatChar {
  enum class Kind : std::uint8_t { Text, ForcedBreak, Image };

  char32_t cp = 0;
  Kind kind = Kind::Text;
  std::size_t style = 0;  // CharStyleTable の添字
  std::size_t image = kNone;
};

// span の background-color。文字の範囲で覚えておき、(e) で行ごとの矩形にする。
// 「文字範囲に属性を対応付ける」仕組みの先輩（issue #8 の色もこの形に揃えた）。
struct BackgroundScope {
  Color color;
  std::size_t begin = 0;  // 文字の範囲 [begin, end)
  std::size_t end = 0;
  // block 方向の範囲。横書きは ascent / descent、縦書きは中心軸から ±(font-size / 2)
  float start_extent = 0;
  float size = 0;
};

// <ruby> の中の「親文字 + <rt>」1 組。親文字は chars の範囲で持つので、
// 色・背景・フォールバックによる断片の分割は普通のテキストと同じに効く。
struct RubyGroup {
  std::size_t base_begin = 0;
  std::size_t base_end = 0;
  std::size_t rt_style = 0;
  std::u32string rt_text;
};

// (a) の出力。文字の範囲（背景スコープ・ルビ組）は畳み込み後の添字に直してある。
struct Collected {
  std::vector<FlatChar> chars;
  CharStyleTable styles;
  std::vector<BackgroundScope> scopes;  // 外側の span が先（描画順）
  std::vector<ImagePiece> images;
  std::vector<RubyGroup> rubies;
  std::vector<std::size_t> ruby_at;  // 文字の位置 → そこから始まるルビ組（なければ kNone）
};

[[nodiscard]] Result<Collected> collect_inline(const InlineInput& input, LayoutEngine& engine);

}  // namespace shashoku::layout
