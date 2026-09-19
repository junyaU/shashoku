#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/color.hpp"
#include "core/ids.hpp"
#include "style/computed_style.hpp"

// ③ レイアウトの出力（ARCHITECTURE.md §3.8）= ⑤a paint の入力。
//
// 座標はすべて **論理座標**（A1）で、**ルート原点からの絶対位置**。単位は CSS px。
//   inline = 字送り方向、block = 行送り方向
//   横書き（horizontal-tb）: inline = x（右向き）、block = y（下向き）
//   縦書き（vertical-rl）  : inline = y（下向き）、block = 紙面の右端からの距離
// 物理座標への変換は paint が 1 回だけ行う（ARCHITECTURE.md §3.9）。
// グリフの x_offset / y_offset だけは例外で、text_measurer.hpp の座標の約束のまま
// **物理 px** で運ぶ（シェーピングが返した値をそのまま渡すため）。
namespace shashoku::layout {

using WritingMode = style::WritingMode;

struct LogicalRect {
  float inline_start = 0;
  float block_start = 0;
  float inline_size = 0;
  float block_size = 0;

  [[nodiscard]] float inline_end() const { return inline_start + inline_size; }
  [[nodiscard]] float block_end() const { return block_start + block_size; }

  bool operator==(const LogicalRect&) const = default;
};

// 論理方向の 4 辺（CSS の物理 4 辺を A1 の読み替え表で論理方向にしたもの）。
template <class T>
struct LogicalEdges {
  T inline_start{};
  T inline_end{};
  T block_start{};
  T block_end{};

  bool operator==(const LogicalEdges&) const = default;
};

// ブロックの塗り（A11: 枠線は 4 辺共通・角丸は 4 隅共通）。
struct BoxDecoration {
  Color background_color = kTransparent;
  float border_width = 0;
  Color border_color = kTransparent;
  float border_radius = 0;

  // 描画命令を 1 つも出さないか（paint が完全に透明な塗りを飛ばすための目安）。
  [[nodiscard]] bool invisible() const {
    return background_color.transparent() && (border_width <= 0 || border_color.transparent());
  }

  bool operator==(const BoxDecoration&) const = default;
};

// 行内に置かれたグリフ 1 個。
struct PositionedGlyph {
  GlyphId glyph_id = 0;
  // ペン位置の inline 座標（絶対）。グリフ原点 = ペン位置 + (x_offset, y_offset)。
  float inline_position = 0;
  float x_offset = 0;  // 物理 px（text_measurer.hpp の約束のまま）
  float y_offset = 0;

  bool operator==(const PositionedGlyph&) const = default;
};

// 同じフォント・サイズ・色・sideways のグリフ列（= DrawGlyphs 1 つぶん）。
// フォールバックでフォントが変わる箇所と sideways が変わる箇所で断片を分ける。
struct TextFragment {
  FontId font = 0;
  float font_size = 0;
  Color color = kBlack;
  bool sideways = false;  // 縦書き中の横倒し（欧文・数字）
  // ベースライン（縦書きでは行の中心軸）の block 座標（絶対）。
  // 行ボックスの baseline と一致するのが普通だが、ルビ（第 3 段）は別の値を持つ。
  float baseline = 0;
  // 断片が占める inline 範囲（絶対）。グリフの送りの合計ではなく、行分割器に渡した
  // アイテムの送り（letter-spacing と Spacing 込み）で測った値。
  float inline_start = 0;
  float inline_size = 0;
  std::vector<PositionedGlyph> glyphs;
  // デバッグ用の元テキスト（UTF-8）。描画には使わない。
  // 1 クラスタの途中でフォントが変わって断片が分かれた場合、2 つめ以降は空になる。
  std::string text;

  bool operator==(const TextFragment&) const = default;
};

// インライン背景（span の background-color）。1 行につき 1 つの矩形。
struct InlineBackground {
  LogicalRect rect;
  Color color = kTransparent;

  bool operator==(const InlineBackground&) const = default;
};

// 行の中身。vector の順序がそのまま描画順（背景 → 文字）。
// 第 2 段で画像断片、第 3 段でルビを足す（ルビは baseline の違う TextFragment で表せる見込み）。
using InlineFragment = std::variant<TextFragment, InlineBackground>;

struct LineBox {
  // 行ボックスの矩形。inline 方向はこの行を含むブロックの content 領域いっぱい
  // （text-align による寄せは断片の座標に入る）、block 方向は行の高さ。
  // あふれた行とぶら下げた約物の断片はこの矩形の外に出る（クリップしない。A4）。
  LogicalRect rect;
  // ベースライン（縦書きでは行の中心軸）の block 座標（絶対）。
  float baseline = 0;
  std::vector<InlineFragment> fragments;

  bool operator==(const LineBox&) const = default;
};

struct BlockBox {
  // デバッグ用のタグ名。無名ブロックは "#anonymous"、合成ルートは "#root"。
  std::string tag;
  LogicalRect rect;  // border-box
  BoxDecoration decoration;
  // content 領域を復元するための padding（border-box → content の差分。border は decoration）。
  LogicalEdges<float> padding;
  // 子はブロックの列か行ボックスの列のどちらか。inline と block が混在する子は
  // 無名ブロックで包むので、この 2 択になる（ARCHITECTURE.md §3.8）。
  std::variant<std::vector<BlockBox>, std::vector<LineBox>> children;

  [[nodiscard]] const std::vector<BlockBox>* blocks() const {
    return std::get_if<std::vector<BlockBox>>(&children);
  }
  [[nodiscard]] const std::vector<LineBox>* lines() const {
    return std::get_if<std::vector<LineBox>>(&children);
  }

  // content 領域（border-box から border と padding を除いたもの）。
  [[nodiscard]] LogicalRect content_rect() const {
    const float border = decoration.border_width;
    LogicalRect out;
    out.inline_start = rect.inline_start + border + padding.inline_start;
    out.block_start = rect.block_start + border + padding.block_start;
    out.inline_size = rect.inline_size - 2 * border - padding.inline_start - padding.inline_end;
    out.block_size = rect.block_size - 2 * border - padding.block_start - padding.block_end;
    return out;
  }

  bool operator==(const BlockBox&) const = default;
};

struct BoxTree {
  WritingMode writing_mode = WritingMode::HorizontalTb;
  float viewport_width = 0;              // 物理 px。paint が論理 → 物理の変換に使う
  std::optional<float> viewport_height;  // 物理 px（縦書きでは必須）
  BlockBox root;

  // 内容の block 方向の大きさ（api が画像の高さを決めるのに使う。ARCHITECTURE.md §3.10）。
  [[nodiscard]] float content_block_size() const { return root.rect.block_end(); }

  bool operator==(const BoxTree&) const = default;
};

}  // namespace shashoku::layout
