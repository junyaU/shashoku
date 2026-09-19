#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "core/color.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"

// style モジュールの内部型。CSS の「指定値」（specified value）と、
// セレクタ / 規則 / カスケードの優先順位を表す。
//
// 計算値（ComputedStyle）との違いは em が残っていること: em の解決に必要な font-size は
// カスケードが終わるまで確定しないので、宣言の段階では単位つきのまま持つ（ARCHITECTURE.md §3.7）。

namespace shashoku::style {

// 枠線のスタイル。ComputedStyle は border_width に畳んで持つ（none なら 0）ので、
// カスケードの途中でだけ必要になる内部の型（A11: 4 辺共通のみ）。
enum class BorderStyle : std::uint8_t { None, Solid };

// 指定値の長さ。em は計算値化のときに px にする。
struct SpecLength {
  float value = 0;
  bool em = false;

  bool operator==(const SpecLength&) const = default;
};

// width / height / margin / flex-basis の指定値（auto と % を含みうる。A5）。
struct SpecDimension {
  enum class Kind : std::uint8_t { Auto, Length, Percent };

  Kind kind = Kind::Auto;
  SpecLength length;
  float percent = 0;

  static SpecDimension make_auto() { return {Kind::Auto, SpecLength{}, 0}; }
  static SpecDimension make_length(SpecLength length) { return {Kind::Length, length, 0}; }
  static SpecDimension make_percent(float percent) {
    return {Kind::Percent, SpecLength{}, percent};
  }

  bool operator==(const SpecDimension&) const = default;
};

struct SpecColor {
  bool current_color = false;  // currentColor: 計算値化で自身の color を入れる
  Color color;

  bool operator==(const SpecColor&) const = default;
};

struct SpecLineHeight {
  enum class Kind : std::uint8_t { Normal, Number, Length };

  Kind kind = Kind::Normal;
  float number = 0;  // Number: 倍率（倍率のまま継承する）
  SpecLength length;

  static SpecLineHeight make_normal() { return {Kind::Normal, 0, SpecLength{}}; }
  static SpecLineHeight make_number(float number) { return {Kind::Number, number, SpecLength{}}; }
  static SpecLineHeight make_length(SpecLength length) { return {Kind::Length, 0, length}; }

  bool operator==(const SpecLineHeight&) const = default;
};

// variant の別扱いにするための薄いラッパ（float / int をそのまま入れると型で区別できない）。
struct SpecNumber {
  float value = 0;

  bool operator==(const SpecNumber&) const = default;
};

struct SpecWeight {
  int value = 400;

  bool operator==(const SpecWeight&) const = default;
};

struct SpecFontFamily {
  std::vector<std::string> names;

  bool operator==(const SpecFontFamily&) const = default;
};

// ComputedStyle のフィールド 1 つ（または border-style のような内部の状態 1 つ）に対応する
// longhand プロパティ。ショートハンドは宣言パースの時点でここまで展開する。
enum class PropertyId : std::uint8_t {
  Display,
  Width,
  Height,
  MarginTop,
  MarginRight,
  MarginBottom,
  MarginLeft,
  PaddingTop,
  PaddingRight,
  PaddingBottom,
  PaddingLeft,
  BorderWidth,
  BorderStyle,
  BorderColor,
  BorderRadius,
  BackgroundColor,
  FlexDirection,
  JustifyContent,
  AlignItems,
  RowGap,
  ColumnGap,
  FlexGrow,
  FlexShrink,
  FlexBasis,
  Color,
  FontSize,
  FontFamily,
  FontWeight,
  LineHeight,
  LetterSpacing,
  TextAlign,
  LineBreak,
  OverflowWrap,
  WritingMode,
};

// 全プロパティで受け付ける CSS 全体キーワード（unset / revert は UnsupportedValue）。
enum class GlobalKeyword : std::uint8_t { None, Inherit, Initial };

using SpecifiedValue =
    std::variant<std::monostate,  // inherit / initial のときのプレースホルダ
                 SpecDimension, SpecLength, SpecNumber, SpecWeight, SpecColor, SpecLineHeight,
                 SpecFontFamily, Display, FlexDirection, JustifyContent, AlignItems, TextAlign,
                 LineBreak, OverflowWrap, WritingMode, BorderStyle>;

struct Declaration {
  PropertyId property = PropertyId::Display;
  GlobalKeyword global = GlobalKeyword::None;
  SpecifiedValue value;
  SourceLocation location;  // 値の先頭（入力 HTML 上の位置）
};

// カスケードの由来。数値の大小がそのまま優先順位（ARCHITECTURE.md §3.7）。
enum class Origin : std::uint8_t { UserAgent, Author, Inline };

// (id, class, tag) の 3 つ組。宣言順の比較がそのまま詳細度の比較になる。
struct Specificity {
  std::uint16_t id_count = 0;
  std::uint16_t class_count = 0;
  std::uint16_t tag_count = 0;

  auto operator<=>(const Specificity&) const = default;
  bool operator==(const Specificity&) const = default;
};

// 単一の複合セレクタ（`p.note#x`）。結合子は対応しないので、これが 1 セレクタの全体。
struct Selector {
  std::string tag;  // 空 = 任意（`*`、またはタグを書かなかった場合）。小文字化済み
  std::vector<std::string> classes;
  std::string id;
  Specificity specificity;
};

struct Rule {
  std::vector<Selector> selectors;
  std::vector<Declaration> declarations;
  std::uint32_t order = 0;  // スタイルシート内の出現順（同詳細度の決着に使う）
};

using Stylesheet = std::vector<Rule>;

}  // namespace shashoku::style
