#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "shashoku/error.hpp"

namespace shashoku::style {

// ② の出力の型。全プロパティが必ず埋まっている（未指定は initial 値か継承値）。
//
// em は計算値化の時点で px に解決する。ただし `%` と `auto` は包含ブロックの大きさが
// 決まる ③ レイアウトまで解決できないので、Dimension として残す
// （CSS の「計算値」と「使用値」の区別。DESIGN.md §5 の「1.2em や未指定が消えた状態」の例外）。
//
// **不変条件（ARCHITECTURE.md A36）**: ここに入っている長さ（padding / margin の px /
// border-width / border-radius / row-gap / column-gap / letter-spacing / width / height /
// flex-basis の px / line-height の px）は、すべて**有限**で、絶対値が
// `RenderLimits::length_px`（既定 2^24 px）**以内**である。`line-height` が倍率のときは
// 「倍率 x その要素の font_size」が同じ上限以内。resolve() がこれを保証し、超えたら
// `LimitExceeded`（位置つき）で失敗する。後段（layout / paint / raster）はこの前提に
// 寄りかかってよい。
//
// 対象外が 2 つある:
//   - `%`（Dimension::Kind::Percent）は layout が包含ブロックで解決するので、その上限は
//     layout の出口が見る（issue #19 の第 2 段階）
//   - `font_size` は A25 の `RenderLimits::font_size_device_px`（scale 込み）が api で見る。
//     非有限な font_size だけは resolve() が止めるので、ここには有限な値しか入らない

struct Dimension {
  enum class Kind : std::uint8_t { Auto, Px, Percent };

  Kind kind = Kind::Auto;
  float value = 0;  // Px: px / Percent: 50% なら 50 / Auto: 未使用

  // NOLINTNEXTLINE(readability-identifier-naming): `auto` は予約語なので末尾に `_` を付ける
  static Dimension auto_() { return {}; }
  static Dimension px(float v) { return {Kind::Px, v}; }
  static Dimension percent(float v) { return {Kind::Percent, v}; }
  [[nodiscard]] bool is_auto() const { return kind == Kind::Auto; }

  bool operator==(const Dimension&) const = default;
};

enum class Display : std::uint8_t { Block, Flex, Inline, None };
// CSS Box Sizing 3 §3: `width` / `height` / `flex-basis` がどの箱の寸法かを決める（A56）。
enum class BoxSizing : std::uint8_t { ContentBox, BorderBox };
enum class FlexDirection : std::uint8_t { Row, Column };
enum class JustifyContent : std::uint8_t {
  FlexStart,
  FlexEnd,
  Center,
  SpaceBetween,
  SpaceAround,
  SpaceEvenly,
};
enum class AlignItems : std::uint8_t { Stretch, FlexStart, FlexEnd, Center };
enum class TextAlign : std::uint8_t { Start, End, Left, Right, Center, Justify };
// Auto は「エンジンの既定に従う」（③ の layout::Options::line_break.strictness を使う）
enum class LineBreak : std::uint8_t { Auto, Loose, Normal, Strict };
enum class OverflowWrap : std::uint8_t { Normal, Anywhere, BreakWord };
enum class WritingMode : std::uint8_t { HorizontalTb, VerticalRl };

struct LineHeight {
  enum class Kind : std::uint8_t { Normal, Number, Px };

  Kind kind = Kind::Normal;
  float value = 0;  // Number: 倍率（その要素自身の font-size に掛ける。倍率のまま継承）/ Px: px

  bool operator==(const LineHeight&) const = default;
};

struct ComputedStyle {
  // ---- ボックス（継承しない）-------------------------------------------------
  Display display = Display::Inline;
  // border-box なら width / height / flex-basis は border box の寸法（A56）。
  // content への引き算は ③ layout が行う（`%` の解決が要るため）
  BoxSizing box_sizing = BoxSizing::ContentBox;
  Dimension width;   // box_sizing がどの箱の幅かを決める
  Dimension height;  // Percent は非対応（② がエラーにする）
  Edges<Dimension> margin{Dimension::px(0), Dimension::px(0), Dimension::px(0), Dimension::px(0)};
  Edges<float> padding;         // px
  float border_width = 0;       // 4 辺共通。border-style: none なら 0
  Color border_color = kBlack;  // 初期値は currentColor（② が color で埋める）
  float border_radius = 0;      // 4 隅共通、px
  Color background_color = kTransparent;

  // ---- flex コンテナ / アイテム（継承しない）----------------------------------
  FlexDirection flex_direction = FlexDirection::Row;
  JustifyContent justify_content = JustifyContent::FlexStart;
  AlignItems align_items = AlignItems::Stretch;
  float row_gap = 0;
  float column_gap = 0;
  float flex_grow = 0;
  float flex_shrink = 1;
  Dimension flex_basis;  // Auto = width / height を見る

  // ---- テキスト（継承する）---------------------------------------------------
  Color color = kBlack;
  float font_size = 16;
  // 指定順。総称ファミリ（sans-serif 等）も文字列のまま入れる
  std::vector<std::string> font_family;
  int font_weight = 400;  // 100..900
  LineHeight line_height;
  float letter_spacing = 0;  // px
  TextAlign text_align = TextAlign::Start;
  LineBreak line_break = LineBreak::Auto;
  OverflowWrap overflow_wrap = OverflowWrap::Normal;
  WritingMode writing_mode = WritingMode::HorizontalTb;

  bool operator==(const ComputedStyle&) const = default;
};

// スタイル付きツリー。display: none の要素と <style> / <rp> 要素はこの木に現れない。
struct StyledNode {
  enum class Type : std::uint8_t { Element, Text };

  Type type = Type::Element;
  std::string tag;  // Element のみ（レイアウトが ruby / rt / br / img を見分けるのに使う）
  std::string text;  // Text のみ（DOM のテキストそのまま。空白の畳み込みは ③）
  ComputedStyle style;  // Text ノードは親要素のスタイル（継承プロパティ）を持つ
  std::vector<StyledNode> children;

  // <img> のみ
  std::string image_src;            // src 属性（ImageSet の名前）
  std::optional<float> attr_width;  // width / height 属性（px）。CSS の width / height が優先
  std::optional<float> attr_height;

  SourceLocation location;
};

}  // namespace shashoku::style
