#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/layout.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

// レイアウトのテスト用ヘルパー（ARCHITECTURE.md §3.8 / DESIGN.md §10-3）。
// フォントなしで「手で組んだスタイル付きツリー → ボックスツリーの座標」を検証できるようにする。
namespace shashoku::layout::test {

// 偽の TextMeasurer の約束（テストの期待値はすべてこれを前提にする）:
//   * 送りは全角（East Asian Width が W / F）= 1em、それ以外（ASCII・半角）= 0.5em
//   * 1 コードポイント = 1 クラスタ = 1 グリフ。ただし結合文字（U+0300–U+036F・U+3099・U+309A）と
//     異体字セレクタ（U+FE00–U+FE0F・U+E0100–U+E01EF）は直前のクラスタに吸収して送り 0
//   * glyph_id = コードポイントの下位 16bit、font = 0（fallback_chars にある文字だけ font = 1）
//   * ascent = 0.88em、descent = 0.12em、line_gap = 0
inline constexpr float kAscentRatio = 0.88F;
inline constexpr float kDescentRatio = 0.12F;

[[nodiscard]] float fake_ascent(float font_size);
[[nodiscard]] float fake_descent(float font_size);
// line-height: normal の行の高さ（= ascent + descent + line_gap）。
[[nodiscard]] float fake_line_height(float font_size);

class FakeMeasurer final : public text::TextMeasurer {
 public:
  text::ShapedText shape(std::u32string_view text, const text::TextStyle& style) override;
  text::FontMetrics metrics(const text::TextStyle& style) override;

  // フォールバックの再現。ここに入れた文字だけ font = 1 を返す
  std::u32string fallback_chars;
  // メトリクスの比率。既定は 0.88 / 0.12（dump の文字列固定テストだけ切りのいい値に変える）
  float ascent_ratio = kAscentRatio;
  float descent_ratio = kDescentRatio;
  // 縦書き（TextStyle::direction == Vertical）では ASCII を横倒しにする
  // （実物の Shaper が UAX #50 の Vertical_Orientation でやることの最小の模倣）。
  bool sideways_latin_in_vertical = true;
  // shape() を呼んだ回数（A6「シェーピングは段落全体で 1 回」の検査用）
  int shape_calls = 0;
};

// ---- スタイル付きツリーのビルダー ----------------------------------------------
// 継承は build() が上から下へ流す（style モジュールの代わり）。
// 上書きはラムダで書く: block({text("あ")}, [](ComputedStyle& s) { s.width = Dimension::px(100); })

using StyleFn = std::function<void(style::ComputedStyle&)>;

struct Tree {
  style::StyledNode::Type type = style::StyledNode::Type::Element;
  std::string tag;
  std::string text;
  StyleFn style;
  std::vector<Tree> children;
  std::string image_src;  // <img> のみ
  std::optional<float> attr_width;
  std::optional<float> attr_height;
};

[[nodiscard]] Tree text(std::string_view content);
[[nodiscard]] Tree block(std::vector<Tree> children, StyleFn style = nullptr);
[[nodiscard]] Tree inline_box(std::vector<Tree> children, StyleFn style = nullptr);
[[nodiscard]] Tree br();
// <ruby>。子は親文字（text / inline_box）と rt() を交互に並べる。
[[nodiscard]] Tree ruby(std::vector<Tree> children, StyleFn style = nullptr);
// <rt>。UA スタイルの font-size 50% をここで当てる。
[[nodiscard]] Tree rt(std::string_view content, StyleFn style = nullptr);
// display: flex のブロック。
[[nodiscard]] Tree flex(std::vector<Tree> children, StyleFn style = nullptr);
// <img>（display: inline）。属性の width / height は attr_* で渡す。
[[nodiscard]] Tree img(std::string_view src, std::optional<float> attr_width = std::nullopt,
                       std::optional<float> attr_height = std::nullopt, StyleFn style = nullptr);
// 任意のタグ・display のノード（img / ruby / flex などのエラー系テスト用）。
[[nodiscard]] Tree element(std::string_view tag, style::Display display,
                           std::vector<Tree> children = {}, StyleFn style = nullptr);

// 合成ルート "#root"（display: block）を作る。
[[nodiscard]] style::StyledNode build(std::vector<Tree> children, StyleFn style = nullptr);
// writing-mode: vertical-rl のルート。
[[nodiscard]] style::StyledNode build_vertical(std::vector<Tree> children, StyleFn style = nullptr);

// ---- レイアウトの呼び出し --------------------------------------------------------
[[nodiscard]] Options make_options(float viewport_width);
// 縦書き用（viewport_height が必須）。
[[nodiscard]] Options vertical_options(float viewport_width, float viewport_height);

// テスト用の画像テーブル（src → ImageId と固有寸法）。
struct ImageEntry {
  std::string src;
  ImageId id = 0;
  float width = 0;
  float height = 0;
};
[[nodiscard]] ImageLookup image_table(std::vector<ImageEntry> entries);

// 画像なし（ImageLookup は常に nullopt）。
[[nodiscard]] Result<BoxTree> run_layout(const style::StyledNode& root, const Options& options,
                                         text::TextMeasurer& measurer);
[[nodiscard]] Result<BoxTree> run_layout(const style::StyledNode& root, const Options& options,
                                         text::TextMeasurer& measurer, const ImageLookup& images);
// 幅だけ指定する短縮形。
[[nodiscard]] Result<BoxTree> run_layout(const style::StyledNode& root, float viewport_width,
                                         text::TextMeasurer& measurer);
[[nodiscard]] Result<BoxTree> run_layout(const style::StyledNode& root, float viewport_width,
                                         text::TextMeasurer& measurer, const ImageLookup& images);

// ---- ボックスツリーからの取り出し --------------------------------------------------
// 文書順のすべての行ボックス。
[[nodiscard]] std::vector<const LineBox*> all_lines(const BoxTree& tree);
// 行ごとのテキスト（TextFragment の text を連結したもの）。
[[nodiscard]] std::vector<std::string> line_texts(const BoxTree& tree);
[[nodiscard]] std::vector<std::string> line_texts(const BlockBox& box);

struct BlockRect {
  std::string tag;
  LogicalRect rect;
};
// 前順のすべてのブロックの矩形（ルートを含む）。
[[nodiscard]] std::vector<BlockRect> block_rects(const BoxTree& tree);
// tag の n 番目（0 始まり）のブロック。見つからなければ nullptr。
[[nodiscard]] const BlockBox* find_block(const BoxTree& tree, std::string_view tag,
                                         std::size_t index = 0);

// 行内のグリフのペン位置（inline 座標）を描画順に並べたもの。
[[nodiscard]] std::vector<float> glyph_positions(const LineBox& line);
// 行内の TextFragment だけを描画順に。
[[nodiscard]] std::vector<const TextFragment*> text_fragments(const LineBox& line);
[[nodiscard]] std::vector<const InlineBackground*> backgrounds(const LineBox& line);
// 行内の画像断片を描画順に。
[[nodiscard]] std::vector<const ImageFragment*> image_fragments(const LineBox& line);
// 文書順のすべての画像断片。
[[nodiscard]] std::vector<const ImageFragment*> all_images(const BoxTree& tree);

}  // namespace shashoku::layout::test
