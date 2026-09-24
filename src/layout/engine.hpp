#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/counters.hpp"
#include "layout/layout.hpp"
#include "layout/logical.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

// レイアウトの内部共通部分。block / flex / 固有寸法 / <img> の各ファイルが使う。
namespace shashoku::layout {

// 使用値（CSS の used value）。すべて論理方向（A1）。
struct BoxSizing {
  LogicalEdges<float> margin;
  LogicalEdges<float> padding;
  float border = 0;                         // 4 辺共通（A11）
  float content_inline_size = 0;            //
  std::optional<float> content_block_size;  // block 方向が auto なら nullopt
};

// 1 つのブロックの中身。無名ブロック / 無名 flex アイテムは対応する StyledNode を
// 持たないので、木のノードではなくこの形で渡す。
struct BlockInput {
  std::string tag;
  const style::ComputedStyle* style = nullptr;  // 塗り・支柱・text-align の出どころ
  std::span<const style::StyledNode> children;
  SourceLocation location;
  bool anonymous = false;  // 無名ブロックは親の塗りを繰り返さない
  // 置換要素（<img>）。非 null なら children は空で、中身は画像 1 個
  const style::StyledNode* replaced = nullptr;
};

// 固有寸法（content-box の inline サイズ）。flex の flex-basis: auto と
// 自動最小サイズ・shrink-to-fit に使う。
struct Intrinsic {
  float min_content = 0;
  float max_content = 0;
};

// <img> の解決済みサイズ（content box、論理方向）。
struct ResolvedImage {
  ImageId id = 0;
  float inline_size = 0;
  float block_size = 0;
};

enum class ChildKind : std::uint8_t { Skip, Inline, Block };

// `box-sizing: border-box` で引く padding の辺を選ぶ軸（A56）。border は 4 辺共通（A11）。
// **論理方向**なので、物理の `width` / `height` から引くときは writing-mode で読み替える
// （`image.cpp` がそうしている）。
enum class SizeAxis : std::uint8_t { Inline, Block };

[[nodiscard]] ChildKind classify(const style::StyledNode& node);
// 無名ブロック / 無名 flex アイテムを作らなくてよい「空白だけのインラインの連続」か。
[[nodiscard]] bool is_blank(std::span<const style::StyledNode> nodes);
// A5: `%` と `auto` はレイアウトまで解けない。Auto の扱いは呼び出し側の責任（0 を返す）。
[[nodiscard]] float resolve_length(const style::Dimension& dimension, float percent_basis);
// A10: 隣り合う兄弟ブロックのマージンの相殺。正の最大 + 負の最小。
[[nodiscard]] float collapse_margins(float a, float b);
// 木ノードから BlockInput を作る（<img> なら replaced を立てる）。
[[nodiscard]] BlockInput input_of(const style::StyledNode& node);

// (0, 0) で組んだ部分木を最終位置へ動かす。レイアウトは開始位置について平行移動で
// 閉じている（どこにも丸めや絶対位置依存の分岐がない）ので、これで「測ってから置く」
// ができる。flex が使う。
void translate(BlockBox& box, float delta_inline, float delta_block);

// レイアウト 1 回のあいだだけ生きるメモ（A29）。定義は layout_cache.hpp。
// engine.hpp は中身を知らない（layout_cache.hpp が engine.hpp を include するため）。
class LayoutCache;

// 入力位置の前後（報告順。MissingGlyph::operator< と同じ並べ方）。
[[nodiscard]] inline bool is_earlier(const SourceLocation& a, const SourceLocation& b) {
  if (a.offset != b.offset) {
    return a.offset < b.offset;
  }
  if (a.line != b.line) {
    return a.line < b.line;
  }
  return a.column < b.column;
}

class LayoutEngine {
 public:
  // memo: 計測結果のメモ（A29）を使うか。false にすると毎回組み直す（テスト用の口。
  // 出力は同じでなければならない。layout_without_memo() を参照）。
  LayoutEngine(const Options& options, text::TextMeasurer& measurer, const ImageLookup& images,
               WritingMode mode, Counters& counters, bool memo = true);
  ~LayoutEngine();  // LayoutCache が不完全型なので out-of-line
  LayoutEngine(const LayoutEngine&) = delete;
  LayoutEngine& operator=(const LayoutEngine&) = delete;
  LayoutEngine(LayoutEngine&&) = delete;
  LayoutEngine& operator=(LayoutEngine&&) = delete;

  [[nodiscard]] const Options& options() const { return *options_; }
  [[nodiscard]] const ImageLookup& images() const { return *images_; }
  [[nodiscard]] const LogicalMap& map() const { return map_; }
  [[nodiscard]] WritingMode mode() const { return mode_; }

  // 計測カウンタ（issue #10-3）。出力には影響しない = 値を読んで分岐してはいけない。
  // 計測は const のレイアウト処理の途中でも起きるので、const から書ける形にしてある。
  [[nodiscard]] Counters& counters() const { return *counters_; }

  // 計測のメモ（A29）。**検索にしか使わない**（キーのポインタ値も map の順序も出力に出さない）。
  [[nodiscard]] LayoutCache& cache() const { return *cache_; }
  [[nodiscard]] bool memo_enabled() const { return memo_; }

  // 計測器の呼び出しは必ずここを通す（回数と文字数を数えるため。TextMeasurer 自体は公開しない）。
  // 失敗はそのまま伝播する（A30 / issue #3）。数えるのは「実際に行った仕事」なので、
  // 失敗した呼び出しも数える。
  [[nodiscard]] Result<text::ShapedText> shape(std::u32string_view text,
                                               const text::TextStyle& style) const {
    ++counters_->shape_calls;
    counters_->shaped_chars += text.size();
    return measurer_->shape(text, style);
  }
  [[nodiscard]] Result<text::FontMetrics> metrics(const text::TextStyle& style) const {
    ++counters_->metrics_calls;
    return measurer_->metrics(style);
  }

  // 豆腐（A31 / issue #9）。同じ段落は計測と配置で何度も組まれうる（<img> を含む段落や
  // メモ無効のとき）ので、**(位置, コードポイント) をキーに重複を除いて**溜める。
  // 集合の順序がそのまま報告順（入力位置の昇順 → コードポイントの昇順）になる。
  // 理由（A43）は文面のためだけの値で、重複除去のキーには入れない（同じテキストノードの
  // 同じ文字なら、フォント列も同じなので理由も必ず同じになる）。
  void record_missing_glyph(char32_t codepoint, const SourceLocation& location,
                            text::MissingReason reason = text::MissingReason::NotInAnyFont) {
    missing_glyphs_.insert(
        MissingGlyph{.codepoint = codepoint, .location = location, .reason = reason});
  }
  [[nodiscard]] std::vector<MissingGlyph> missing_glyphs() const {
    return {missing_glyphs_.begin(), missing_glyphs_.end()};
  }

  // font-family の要求を満たせなかった記録（A57）。豆腐と同じで、同じ段落が計測と配置で
  // 何度組まれても増えないよう **`font-family` の並びをキーに** 1 件だけ溜め、位置は
  // 入力順で最初のテキストノードの先頭（= offset が最小のもの）を採る。
  void record_font_fallback(const std::vector<std::string>& families,
                            const SourceLocation& location) {
    const auto [at, inserted] = font_fallbacks_.try_emplace(families, location);
    if (!inserted && is_earlier(location, at->second)) {
      at->second = location;
    }
  }
  // 位置の昇順（同じ位置なら並びの辞書順 = std::map の順）。決定的であること。
  [[nodiscard]] std::vector<FontFallback> font_fallbacks() const {
    std::vector<FontFallback> out;
    out.reserve(font_fallbacks_.size());
    for (const auto& [families, location] : font_fallbacks_) {
      out.push_back(FontFallback{.families = families, .location = location});
    }
    std::stable_sort(out.begin(), out.end(), [](const FontFallback& a, const FontFallback& b) {
      return is_earlier(a.location, b.location);
    });
    return out;
  }

  // CSS 2.1 §10.3.3（inline 方向）と §10.5（block 方向）の使用値。
  // override_inline / override_block は置換要素（<img>）と flex アイテムのように、
  // サイズが width / height プロパティの外で決まっている箱に使う。
  [[nodiscard]] Result<BoxSizing> resolve_box(
      const style::ComputedStyle& style, float containing_inline_size,
      const SourceLocation& location, std::optional<float> override_inline = std::nullopt,
      std::optional<float> override_block = std::nullopt) const;

  // `box-sizing: border-box` のときに指定値から引く量（A56）。
  // content-box なら 0、border-box なら「その軸の padding 2 辺 + border x 2」。
  // 指定値が長さで与えられていない（`auto`）箱には関係しない。
  [[nodiscard]] float border_box_extra(const style::ComputedStyle& style, SizeAxis axis) const;

  // `width` / `height` / `flex-basis` の指定値 → content サイズ（A56。CSS Box Sizing 3 §3）。
  // `%` は先に percent_basis で解決し、そのあとで引く。下限は 0。
  // `Auto` の扱いは呼び出し側の責任（resolve_length と同じく 0 を返す）。
  [[nodiscard]] float content_from_specified(const style::ComputedStyle& style,
                                             const style::Dimension& size, float percent_basis,
                                             SizeAxis axis) const;

  // 論理方向のマージン（auto は 0）と、どの辺が auto だったか。
  [[nodiscard]] LogicalEdges<float> resolve_margin(const style::ComputedStyle& style,
                                                   float percent_basis) const;
  [[nodiscard]] LogicalEdges<bool> margin_is_auto(const style::ComputedStyle& style) const;

  // block_start は border-box の block 開始位置（絶対）。マージンは呼び出し側が消費済み。
  Result<BlockBox> layout_block(const BlockInput& input, const BoxSizing& sizing,
                                float content_inline_start, float block_start);

  // 部分木を「この条件で組んだときの border-box の block サイズ」だけ測る（A29）。
  // 同じ条件の 2 回目以降は組み直さずにメモを返すので、flex の「測って捨てる」が
  // 入れ子の深さに対して指数にならない。**配置には使わない**（箱は返さない）。
  Result<float> measure_block_size(const BlockInput& input, const BoxSizing& sizing,
                                   float content_inline_start);

  // content-box の固有 inline サイズ。同じ部分木・同じ `%` の基準ならメモを返す（A29）。
  Result<Intrinsic> content_intrinsic(const BlockInput& input, float percent_basis);
  // 子 1 つぶんの margin-box の固有 inline サイズ。
  Result<Intrinsic> outer_intrinsic(const style::StyledNode& node, float percent_basis);

  // <img> のサイズ解決: CSS の width / height > width / height 属性 > 固有寸法。
  [[nodiscard]] Result<ResolvedImage> resolve_image(const style::StyledNode& node,
                                                    float percent_basis) const;

 private:
  [[nodiscard]] Result<BlockBox> layout_image_box(const BlockInput& input, const BoxSizing& sizing,
                                                  float content_inline_start,
                                                  float block_start) const;
  // メモを見ないで固有寸法を出す本体（content_intrinsic がメモの外側）。
  Result<Intrinsic> compute_intrinsic(const BlockInput& input, float percent_basis);

  const Options* options_;
  text::TextMeasurer* measurer_;
  const ImageLookup* images_;
  LogicalMap map_;
  WritingMode mode_;
  Counters* counters_;
  std::unique_ptr<LayoutCache> cache_;
  // 溜まった豆腐。std::set の順序がそのまま出力の順序になる（MissingGlyph::operator<）ので、
  // ポインタ値も unordered の反復順もここには入らない（DESIGN.md §3-5）。
  std::set<MissingGlyph> missing_glyphs_;
  // 満たせなかった font-family の並び（A57）。キーは並びそのもので、値は最初の位置。
  // std::map なのでポインタ値も unordered の反復順も出力に出ない（DESIGN.md §3-5）。
  std::map<std::vector<std::string>, SourceLocation> font_fallbacks_;
  bool memo_ = true;
};

// テスト用の口: メモを使わずに組む（A29）。メモが効いた場合と効かない場合で出力が
// 1 ビットも変わらないことを固定するために使う。製品の呼び出し側は layout() を使う。
Result<BoxTree> layout_without_memo(const style::StyledNode& root, const Options& options,
                                    text::TextMeasurer& measurer, const ImageLookup& images,
                                    Counters* counters = nullptr);

}  // namespace shashoku::layout
