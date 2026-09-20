#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/color.hpp"
#include "core/result.hpp"
#include "layout/engine.hpp"
#include "layout/inline_collect.hpp"
#include "layout/inline_layout.hpp"
#include "layout/inline_style.hpp"
#include "linebreak/line_breaker.hpp"
#include "text/text_measurer.hpp"

// インライン整形文脈の (b)(c)（ARCHITECTURE.md §3.8）: シェーピングとアイテム化。
// ここまでの結果が「準備済み段落」（PreparedParagraph）で、行の幅に依らない。
// だから固有寸法の計測と実際の配置で同じものを使い回せる（#5 はこれを外に取り出す）。
namespace shashoku::layout {

// (b) 1 回の shape() の結果。シェーピング属性が等しい連続に 1 つ（#8）。
struct ShapedRun {
  std::size_t shaping = 0;  // CharStyleTable のシェーピング属性の添字
  text::ShapedText shaped;
};

// ルビ組の親文字の 1 区間。装飾（色など）が変わる位置で切る。グリフは 1 回の
// シェーピング結果の部分範囲なので、切っても位置は変わらない。
struct BaseSegment {
  std::size_t run = kNone;
  std::size_t glyph_begin = 0;
  std::size_t glyph_end = 0;
  std::size_t style = 0;  // CharStyleTable の添字（区間の先頭の文字のもの）
  std::size_t char_begin = 0;
  std::size_t char_end = 0;
  float advance = 0;  // クラスタの送りの合計（letter-spacing 込み）
};

// ルビ 1 組。行分割器から見れば Atomic 1 個で、組の内部では改行しない。
struct RubyPiece {
  std::vector<BaseSegment> base;
  float base_width = 0;
  std::size_t base_style = 0;
  float base_ascent = 0;
  float base_font_size = 0;

  std::size_t rt_run = kNone;
  std::size_t rt_style = 0;
  float rt_width = 0;
  float rt_ascent = 0;
  float rt_descent = 0;
  float rt_font_size = 0;
  std::string rt_text;
};

// (c) linebreak::Item 1 個の出どころ。
struct ItemSource {
  std::size_t run = kNone;  // ForcedBreak・<img>・ルビ組は kNone
  std::size_t glyph_begin = 0;
  std::size_t glyph_end = 0;
  std::size_t char_begin = 0;  // 畳み込み後の文字の範囲
  std::size_t char_end = 0;
  // このアイテムの属性（CharStyleTable の添字）。装飾・行高と行分割ポリシーの両方をここから
  // 引く。1 つのクラスタが境界をまたぐときは**クラスタ先頭の文字**のものを使う（A27 / A28）。
  std::size_t style = 0;
  std::size_t image = kNone;
  std::size_t ruby = kNone;
};

// (a)〜(c) まで済ませた段落。行の幅に依らないので、固有寸法の計測と配置で共有できる。
struct PreparedParagraph {
  std::vector<FlatChar> chars;
  CharStyleTable styles;
  // シェーピング属性ごとのメトリクス（CharStyleTable::shaping_index() で引く）。
  std::vector<text::FontMetrics> metrics;
  std::vector<BackgroundScope> scopes;
  std::vector<ImagePiece> images;

  std::vector<ShapedRun> runs;
  std::vector<RubyPiece> rubies;
  std::vector<linebreak::Item> items;
  std::vector<ItemSource> sources;

  // 支柱（strut）。内容によらず全行の高さに参加する（CSS 2.1 §10.8）。
  std::size_t strut_style = 0;

  [[nodiscard]] float font_size(std::size_t style) const { return styles.shaping(style).font_size; }
  [[nodiscard]] Color color(std::size_t style) const { return styles.decoration(style).color; }
  [[nodiscard]] float letter_spacing(std::size_t style) const {
    return styles.decoration(style).letter_spacing;
  }
  [[nodiscard]] const style::LineHeight& line_height(std::size_t style) const {
    return styles.decoration(style).line_height;
  }
  [[nodiscard]] const text::FontMetrics& metrics_of(std::size_t style) const {
    return metrics[styles.shaping_index(style)];
  }
  // 「見た目が同じか」の鍵（TextFragment を切る単位）。行分割ポリシーは含めない。
  [[nodiscard]] std::size_t visual_key(std::size_t style) const {
    return styles.decoration_index(style);
  }
  // [char_begin, char_end) の UTF-8（TextFragment のデバッグ用テキスト）。
  [[nodiscard]] std::string text_of(std::size_t char_begin, std::size_t char_end) const;
};

// (a)〜(c) を実際に行う。共有できるものは shared_paragraph() 経由で 1 回に減らす。
[[nodiscard]] Result<PreparedParagraph> prepare_paragraph(const InlineInput& input,
                                                          LayoutEngine& engine);

// 準備済み段落の持ち主。共有できたときは LayoutCache の中身への参照、できなかったとき
// （<img> を含む段落 / メモ無効）は自分で持つ。どちらでも get() の読み方は同じ。
class ParagraphHandle {
 public:
  explicit ParagraphHandle(const PreparedParagraph& shared) : shared_(&shared) {}
  explicit ParagraphHandle(PreparedParagraph&& owned) : owned_(std::move(owned)) {}

  [[nodiscard]] const PreparedParagraph& get() const {
    return owned_.has_value() ? *owned_ : *shared_;
  }

 private:
  // どちらか一方だけが値を持つ（コンストラクタが 2 つあり、どちらも片方だけを埋める）。
  std::optional<PreparedParagraph> owned_;
  const PreparedParagraph* shared_ = nullptr;
};

// 同じ段落（= 同じノードの並びを同じブロックのスタイルで組んだもの）なら 2 回目以降は
// 組み直さない（A29）。固有寸法の計測と実際の配置が、シェーピングまで済んだ同じものを使う。
[[nodiscard]] Result<ParagraphHandle> shared_paragraph(const InlineInput& input,
                                                       LayoutEngine& engine);

}  // namespace shashoku::layout
