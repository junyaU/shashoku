#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/color.hpp"
#include "core/ids.hpp"
#include "shashoku/error.hpp"
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
//
// **不変条件（ARCHITECTURE.md A36）**: ここに入っている数値 —— 矩形の位置・大きさ・端、
// 枠線の幅と半径、padding、ベースライン、断片の font-size と inline 範囲、グリフの位置と
// offset —— は、**すべて有限**で、絶対値が `Options::max_geometry_px`（既定は
// `RenderLimits::length_px x dom_nodes` = 2^24 x 20,000）**以内**である。
// `layout()` が出口で `check_geometry()` を通して保証し、破れていたら `LimitExceeded`
// （その箱の位置つき）で失敗する。後段（paint / raster）はこの前提に寄りかかってよい。
//
// なぜ「長さの上限」より緩いか: 座標は長さの足し算なので、`length_px` をそのまま使うと
// 高さ 1000 px のブロックを 2 万個積んだだけで落ちてしまう。一方で「有限であること」だけでは
// `width: 1e38%` @200（= 2e38）が通ってしまい、**ビューポート幅によってエラーになったり
// ならなかったり**する。`length_px x dom_nodes` なら、②style を通った文書は必ず収まり、
// 収まらないのは `%` や flex の比のように入力の長さに比例しない計算が壊れたときだけになる。
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
  // 断片の**先頭のグリフ**が属するテキストノードの先頭の位置（A31 / issue #9）。
  // 「この断片は HTML のどこから来たか」をダンプで引くためのもので、描画には使わない。
  // 断片は位置では切らない（位置で切ると DrawGlyphs が無意味に分かれる。A27）ので、
  // 1 つの断片が複数のノードにまたがることがある。
  SourceLocation location;

  bool operator==(const TextFragment&) const = default;
};

// インライン背景（span の background-color）。1 行につき 1 つの矩形。
struct InlineBackground {
  LogicalRect rect;
  Color color = kTransparent;

  bool operator==(const InlineBackground&) const = default;
};

// <img>。行の中の分割不能な箱（linebreak::ItemKind::Atomic）として流れる。
// display: block の <img> も「画像断片 1 個だけの行を持つブロック」として表す（型を増やさない）。
// 描画順（paint）: decoration の背景 → 画像（content_rect に拡縮。border_radius があれば
// 角丸でクリップ）→ decoration の枠線。
struct ImageFragment {
  ImageId image = 0;  // render() に渡された画像テーブルの添字（raster::DrawImage にそのまま渡す）
  LogicalRect rect;          // border-box
  LogicalRect content_rect;  // 画像を描く範囲（rect から border と padding を除いたもの）
  BoxDecoration decoration;

  bool operator==(const ImageFragment&) const = default;
};

// 行の中身。vector の順序がそのまま描画順（背景 → 文字）。
// ルビは baseline の違う TextFragment で表す（新しい variant を足さない）。
using InlineFragment = std::variant<TextFragment, InlineBackground, ImageFragment>;

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
  // この箱を生んだ要素の入力位置（`TextFragment::location` と同じ考え方。A31 / A36）。
  // 無名ブロックは包んだインラインの連続の先頭、合成ルートは入力の先頭。
  // **paint は読まない**ので絵には一切影響しない。出口の検査（A36）が「どの要素の座標が
  // 壊れたか」を報告するのに使い、`dump_json()` が出す。
  SourceLocation location;
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

// 豆腐（どのフォントにもグリフがなかった文字）1 件（A31 / issue #9 / DESIGN.md §6-6）。
// 報告の粒度は「(コードポイント, テキストノード) の組ごとに 1 件」: 同じノードに同じ絵文字が
// 5 個あっても 1 件、別のノードなら別件。api が Warning に変換する。
struct MissingGlyph {
  char32_t codepoint = 0;
  // その文字を含むテキストノードの先頭（StyledNode::location）。文字単位の桁ではない
  // （文字参照や空白の畳み込みを遡らないと出せないため。A31）。
  SourceLocation location;

  bool operator==(const MissingGlyph&) const = default;
  // 報告順（入力位置の昇順 → コードポイントの昇順）。決定的であること。
  [[nodiscard]] bool operator<(const MissingGlyph& other) const {
    if (location.offset != other.location.offset) {
      return location.offset < other.location.offset;
    }
    if (location.line != other.location.line) {
      return location.line < other.location.line;
    }
    if (location.column != other.location.column) {
      return location.column < other.location.column;
    }
    return codepoint < other.codepoint;
  }
};

struct BoxTree {
  WritingMode writing_mode = WritingMode::HorizontalTb;
  float viewport_width = 0;              // 物理 px。paint が論理 → 物理の変換に使う
  std::optional<float> viewport_height;  // 物理 px（縦書きでは必須）
  BlockBox root;
  // 豆腐の記録。上の operator< の順（入力位置 → コードポイント）に並び、重複はない。
  // paint は読まない（絵には影響しない）。api が Warning にし、dump_json が出す。
  std::vector<MissingGlyph> missing_glyphs;

  // 内容の block 方向の大きさ（api が画像の高さを決めるのに使う。ARCHITECTURE.md §3.10）。
  [[nodiscard]] float content_block_size() const { return root.rect.block_end(); }

  bool operator==(const BoxTree&) const = default;
};

}  // namespace shashoku::layout
