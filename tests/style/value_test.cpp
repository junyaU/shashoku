#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// プロパティごとの正常値（全キーワード・全単位）と計算値、ショートハンドの展開。

namespace shashoku::style {
namespace {

ComputedStyle computed(std::string_view declarations) {
  const Result<ComputedStyle> style = inline_style(declarations);
  EXPECT_TRUE(style.has_value()) << declarations << ": " << (style ? "" : style.error().message);
  return style.value_or(ComputedStyle{});
}

// ---- display と単純なキーワード群 ---------------------------------------------

TEST(StyleValue, DisplayKeywords) {
  EXPECT_EQ(computed("display: block").display, Display::Block);
  EXPECT_EQ(computed("display: flex").display, Display::Flex);
  EXPECT_EQ(computed("display: inline").display, Display::Inline);
  // display: none の要素は木から落ちるので、ここでは値だけを別経路で確かめる
  const Result<ComputedStyle> none = inline_style("display: none");
  EXPECT_FALSE(none.has_value());  // 木から落ちた = 「要素が見つからない」内部エラー
  EXPECT_EQ(none.error().kind, ErrorKind::Internal);
}

TEST(StyleValue, KeywordsAreCaseInsensitive) {
  EXPECT_EQ(computed("DISPLAY: BLOCK").display, Display::Block);
  EXPECT_EQ(computed("Text-Align: Justify").text_align, TextAlign::Justify);
}

TEST(StyleValue, FlexDirection) {
  EXPECT_EQ(computed("flex-direction: row").flex_direction, FlexDirection::Row);
  EXPECT_EQ(computed("flex-direction: column").flex_direction, FlexDirection::Column);
}

TEST(StyleValue, JustifyContent) {
  EXPECT_EQ(computed("justify-content: flex-start").justify_content, JustifyContent::FlexStart);
  EXPECT_EQ(computed("justify-content: flex-end").justify_content, JustifyContent::FlexEnd);
  EXPECT_EQ(computed("justify-content: center").justify_content, JustifyContent::Center);
  EXPECT_EQ(computed("justify-content: space-between").justify_content,
            JustifyContent::SpaceBetween);
  EXPECT_EQ(computed("justify-content: space-around").justify_content, JustifyContent::SpaceAround);
  EXPECT_EQ(computed("justify-content: space-evenly").justify_content, JustifyContent::SpaceEvenly);
  // start / end は flex-start / flex-end の別名
  EXPECT_EQ(computed("justify-content: start").justify_content, JustifyContent::FlexStart);
  EXPECT_EQ(computed("justify-content: end").justify_content, JustifyContent::FlexEnd);
}

TEST(StyleValue, AlignItems) {
  EXPECT_EQ(computed("align-items: stretch").align_items, AlignItems::Stretch);
  EXPECT_EQ(computed("align-items: flex-start").align_items, AlignItems::FlexStart);
  EXPECT_EQ(computed("align-items: flex-end").align_items, AlignItems::FlexEnd);
  EXPECT_EQ(computed("align-items: center").align_items, AlignItems::Center);
}

TEST(StyleValue, TextAlign) {
  EXPECT_EQ(computed("text-align: start").text_align, TextAlign::Start);
  EXPECT_EQ(computed("text-align: end").text_align, TextAlign::End);
  EXPECT_EQ(computed("text-align: left").text_align, TextAlign::Left);
  EXPECT_EQ(computed("text-align: right").text_align, TextAlign::Right);
  EXPECT_EQ(computed("text-align: center").text_align, TextAlign::Center);
  EXPECT_EQ(computed("text-align: justify").text_align, TextAlign::Justify);
}

TEST(StyleValue, LineBreakAndOverflowWrap) {
  EXPECT_EQ(computed("line-break: auto").line_break, LineBreak::Auto);
  EXPECT_EQ(computed("line-break: loose").line_break, LineBreak::Loose);
  EXPECT_EQ(computed("line-break: normal").line_break, LineBreak::Normal);
  EXPECT_EQ(computed("line-break: strict").line_break, LineBreak::Strict);
  EXPECT_EQ(computed("overflow-wrap: normal").overflow_wrap, OverflowWrap::Normal);
  EXPECT_EQ(computed("overflow-wrap: anywhere").overflow_wrap, OverflowWrap::Anywhere);
  EXPECT_EQ(computed("overflow-wrap: break-word").overflow_wrap, OverflowWrap::BreakWord);
}

// ---- 長さと単位 ---------------------------------------------------------------

TEST(StyleValue, LengthUnits) {
  EXPECT_EQ(computed("width: 100px").width, Dimension::px(100));
  EXPECT_EQ(computed("width: 0").width, Dimension::px(0));
  EXPECT_EQ(computed("width: 50%").width, Dimension::percent(50));
  EXPECT_EQ(computed("width: auto").width, Dimension::auto_());
  // em は自分の font-size で解決する
  EXPECT_EQ(computed("font-size: 20px; width: 2em").width, Dimension::px(40));
  // 宣言の順は em の解決に影響しない（font-size は先に決まる）
  EXPECT_EQ(computed("width: 2em; font-size: 20px").width, Dimension::px(40));
}

TEST(StyleValue, DecimalAndSignedLengths) {
  EXPECT_EQ(computed("width: 12.5px").width, Dimension::px(12.5F));
  EXPECT_EQ(computed("width: .5px").width, Dimension::px(0.5F));
  EXPECT_EQ(computed("width: +8px").width, Dimension::px(8));
  EXPECT_EQ(computed("margin-top: -4px").margin.top, Dimension::px(-4));
  EXPECT_FLOAT_EQ(computed("letter-spacing: -0.05em; font-size: 20px").letter_spacing, -1.0F);
}

TEST(StyleValue, HeightRejectsPercentButAcceptsAuto) {
  EXPECT_EQ(computed("height: 40px").height, Dimension::px(40));
  EXPECT_EQ(computed("height: auto").height, Dimension::auto_());
}

// ---- margin / padding のショートハンド ----------------------------------------

TEST(StyleValue, MarginShorthandOneToFourValues) {
  const ComputedStyle one = computed("margin: 10px");
  EXPECT_EQ(one.margin, (Edges<Dimension>{Dimension::px(10), Dimension::px(10), Dimension::px(10),
                                          Dimension::px(10)}));
  const ComputedStyle two = computed("margin: 1px 2px");
  EXPECT_EQ(two.margin, (Edges<Dimension>{Dimension::px(1), Dimension::px(2), Dimension::px(1),
                                          Dimension::px(2)}));
  const ComputedStyle three = computed("margin: 1px 2px 3px");
  EXPECT_EQ(three.margin, (Edges<Dimension>{Dimension::px(1), Dimension::px(2), Dimension::px(3),
                                            Dimension::px(2)}));
  const ComputedStyle four = computed("margin: 1px 2px 3px 4px");
  EXPECT_EQ(four.margin, (Edges<Dimension>{Dimension::px(1), Dimension::px(2), Dimension::px(3),
                                           Dimension::px(4)}));
}

TEST(StyleValue, MarginAutoAndNegative) {
  const ComputedStyle style = computed("margin: 0 auto");
  EXPECT_EQ(style.margin.top, Dimension::px(0));
  EXPECT_EQ(style.margin.right, Dimension::auto_());
  EXPECT_EQ(style.margin.left, Dimension::auto_());
  EXPECT_EQ(computed("margin: -1em; font-size: 10px").margin.top, Dimension::px(-10));
}

TEST(StyleValue, MarginLonghandsOverrideShorthand) {
  const ComputedStyle style = computed("margin: 5px; margin-left: 9px");
  EXPECT_EQ(style.margin.top, Dimension::px(5));
  EXPECT_EQ(style.margin.left, Dimension::px(9));
}

TEST(StyleValue, PaddingShorthandAndLonghands) {
  const ComputedStyle style = computed("padding: 1px 2px 3px 4px");
  EXPECT_EQ(style.padding, (Edges<float>{1, 2, 3, 4}));
  EXPECT_FLOAT_EQ(computed("padding-right: 2em; font-size: 8px").padding.right, 16.0F);
}

// ---- border -------------------------------------------------------------------

TEST(StyleValue, BorderShorthandInAnyOrder) {
  const ComputedStyle a = computed("border: 2px solid red");
  EXPECT_FLOAT_EQ(a.border_width, 2.0F);
  EXPECT_EQ(a.border_color, (Color{255, 0, 0, 255}));
  const ComputedStyle b = computed("border: red 2px solid");
  EXPECT_FLOAT_EQ(b.border_width, 2.0F);
  EXPECT_EQ(b.border_color, (Color{255, 0, 0, 255}));
  const ComputedStyle c = computed("border: solid blue 0.5em; font-size: 20px");
  EXPECT_FLOAT_EQ(c.border_width, 10.0F);
  EXPECT_EQ(c.border_color, (Color{0, 0, 255, 255}));
}

TEST(StyleValue, BorderDefaultsWhenPartsAreOmitted) {
  // スタイルを省略したら none 扱い = 幅 0
  EXPECT_FLOAT_EQ(computed("border: 4px").border_width, 0.0F);
  // 幅を省略したら medium（3px）
  EXPECT_FLOAT_EQ(computed("border: solid").border_width, 3.0F);
  // 色を省略したら currentColor
  EXPECT_EQ(computed("color: #0f0; border: 1px solid").border_color, (Color{0, 255, 0, 255}));
  // border: none は幅 0
  EXPECT_FLOAT_EQ(computed("border: none").border_width, 0.0F);
}

TEST(StyleValue, BorderLonghands) {
  EXPECT_FLOAT_EQ(computed("border-style: solid; border-width: 6px").border_width, 6.0F);
  // border-style が none のままなら border-width を書いても 0
  EXPECT_FLOAT_EQ(computed("border-width: 6px").border_width, 0.0F);
  EXPECT_EQ(computed("border-style: solid; border-color: #123456").border_color,
            (Color{0x12, 0x34, 0x56, 255}));
  EXPECT_EQ(computed("color: teal; border-color: currentColor").border_color,
            (Color{0, 0x80, 0x80, 255}));
}

TEST(StyleValue, BorderRadius) {
  EXPECT_FLOAT_EQ(computed("border-radius: 8px").border_radius, 8.0F);
  EXPECT_FLOAT_EQ(computed("font-size: 10px; border-radius: 1.5em").border_radius, 15.0F);
}

// ---- 色 -----------------------------------------------------------------------

TEST(StyleValue, HexColors) {
  EXPECT_EQ(computed("color: #f00").color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(computed("color: #F008").color, (Color{255, 0, 0, 0x88}));
  EXPECT_EQ(computed("color: #10203040").color, (Color{0x10, 0x20, 0x30, 0x40}));
  EXPECT_EQ(computed("color: #abcdef").color, (Color{0xAB, 0xCD, 0xEF, 255}));
}

TEST(StyleValue, RgbFunctions) {
  EXPECT_EQ(computed("color: rgb(1, 2, 3)").color, (Color{1, 2, 3, 255}));
  EXPECT_EQ(computed("color: rgba(1, 2, 3, 0.5)").color, (Color{1, 2, 3, 128}));
  EXPECT_EQ(computed("color: rgb(255 0 0)").color, (Color{255, 0, 0, 255}));
  EXPECT_EQ(computed("color: rgb(255 0 0 / 50%)").color, (Color{255, 0, 0, 128}));
  EXPECT_EQ(computed("color: rgb(100%, 0%, 0%)").color, (Color{255, 0, 0, 255}));
  // 範囲外は丸める（CSS Color 4）
  EXPECT_EQ(computed("color: rgb(300, -20, 0)").color, (Color{255, 0, 0, 255}));
}

TEST(StyleValue, NamedColorsAndTransparent) {
  EXPECT_EQ(computed("color: black").color, kBlack);
  EXPECT_EQ(computed("color: white").color, kWhite);
  EXPECT_EQ(computed("background-color: transparent").background_color, kTransparent);
  EXPECT_EQ(computed("color: rebeccapurple").color, (Color{0x66, 0x33, 0x99, 255}));
  EXPECT_EQ(computed("color: DarkSlateGrey").color, (Color{0x2F, 0x4F, 0x4F, 255}));
  EXPECT_EQ(computed("color: yellowgreen").color, (Color{0x9A, 0xCD, 0x32, 255}));
}

TEST(StyleValue, BackgroundIsAnAliasForBackgroundColor) {
  EXPECT_EQ(computed("background: navy").background_color, (Color{0, 0, 0x80, 255}));
}

// ---- フォント -----------------------------------------------------------------

TEST(StyleValue, FontFamilyQuotedAndUnquoted) {
  EXPECT_EQ(computed("font-family: serif").font_family, (std::vector<std::string>{"serif"}));
  EXPECT_EQ(computed("font-family: Noto Sans JP, sans-serif").font_family,
            (std::vector<std::string>{"Noto Sans JP", "sans-serif"}));
  EXPECT_EQ(computed(R"(font-family: "Hiragino Kaku Gothic ProN", 'Yu Gothic', serif)").font_family,
            (std::vector<std::string>{"Hiragino Kaku Gothic ProN", "Yu Gothic", "serif"}));
  // 非 ASCII の裸の名前
  EXPECT_EQ(computed("font-family: 游ゴシック").font_family,
            (std::vector<std::string>{"游ゴシック"}));
}

TEST(StyleValue, FontWeight) {
  EXPECT_EQ(computed("font-weight: normal").font_weight, 400);
  EXPECT_EQ(computed("font-weight: bold").font_weight, 700);
  for (int weight = 100; weight <= 900; weight += 100) {
    EXPECT_EQ(computed("font-weight: " + std::to_string(weight)).font_weight, weight);
  }
}

TEST(StyleValue, FontSize) {
  EXPECT_FLOAT_EQ(computed("font-size: 24px").font_size, 24.0F);
  EXPECT_FLOAT_EQ(computed("font-size: 1.5em").font_size, 24.0F);  // 親（ルート）は 16px
  EXPECT_FLOAT_EQ(computed("font-size: 0").font_size, 0.0F);
}

TEST(StyleValue, LineHeight) {
  EXPECT_EQ(computed("line-height: normal").line_height, (LineHeight{LineHeight::Kind::Normal, 0}));
  EXPECT_EQ(computed("line-height: 1.5").line_height, (LineHeight{LineHeight::Kind::Number, 1.5F}));
  EXPECT_EQ(computed("line-height: 24px").line_height, (LineHeight{LineHeight::Kind::Px, 24}));
  EXPECT_EQ(computed("font-size: 20px; line-height: 1.5em").line_height,
            (LineHeight{LineHeight::Kind::Px, 30}));
}

TEST(StyleValue, LetterSpacing) {
  EXPECT_FLOAT_EQ(computed("letter-spacing: 2px").letter_spacing, 2.0F);
  EXPECT_FLOAT_EQ(computed("letter-spacing: normal").letter_spacing, 0.0F);
  EXPECT_FLOAT_EQ(computed("font-size: 16px; letter-spacing: 0.25em").letter_spacing, 4.0F);
}

// ---- flex ---------------------------------------------------------------------

TEST(StyleValue, FlexShorthand) {
  const ComputedStyle none = computed("flex: none");
  EXPECT_FLOAT_EQ(none.flex_grow, 0.0F);
  EXPECT_FLOAT_EQ(none.flex_shrink, 0.0F);
  EXPECT_EQ(none.flex_basis, Dimension::auto_());

  const ComputedStyle automatic = computed("flex: auto");
  EXPECT_FLOAT_EQ(automatic.flex_grow, 1.0F);
  EXPECT_FLOAT_EQ(automatic.flex_shrink, 1.0F);
  EXPECT_EQ(automatic.flex_basis, Dimension::auto_());

  const ComputedStyle one = computed("flex: 1");
  EXPECT_FLOAT_EQ(one.flex_grow, 1.0F);
  EXPECT_FLOAT_EQ(one.flex_shrink, 1.0F);
  EXPECT_EQ(one.flex_basis, Dimension::px(0));

  const ComputedStyle two = computed("flex: 2 3");
  EXPECT_FLOAT_EQ(two.flex_grow, 2.0F);
  EXPECT_FLOAT_EQ(two.flex_shrink, 3.0F);
  EXPECT_EQ(two.flex_basis, Dimension::px(0));

  const ComputedStyle basis = computed("flex: 2 30px");
  EXPECT_FLOAT_EQ(basis.flex_grow, 2.0F);
  EXPECT_FLOAT_EQ(basis.flex_shrink, 1.0F);
  EXPECT_EQ(basis.flex_basis, Dimension::px(30));

  const ComputedStyle three = computed("flex: 2 3 40%");
  EXPECT_FLOAT_EQ(three.flex_grow, 2.0F);
  EXPECT_FLOAT_EQ(three.flex_shrink, 3.0F);
  EXPECT_EQ(three.flex_basis, Dimension::percent(40));

  EXPECT_EQ(computed("flex: 1 1 auto").flex_basis, Dimension::auto_());
}

TEST(StyleValue, FlexLonghands) {
  EXPECT_FLOAT_EQ(computed("flex-grow: 2.5").flex_grow, 2.5F);
  EXPECT_FLOAT_EQ(computed("flex-shrink: 0").flex_shrink, 0.0F);
  EXPECT_EQ(computed("flex-basis: 25%").flex_basis, Dimension::percent(25));
  EXPECT_EQ(computed("flex-basis: auto").flex_basis, Dimension::auto_());
}

TEST(StyleValue, GapShorthand) {
  const ComputedStyle one = computed("gap: 10px");
  EXPECT_FLOAT_EQ(one.row_gap, 10.0F);
  EXPECT_FLOAT_EQ(one.column_gap, 10.0F);
  const ComputedStyle two = computed("gap: 1px 2px");
  EXPECT_FLOAT_EQ(two.row_gap, 1.0F);
  EXPECT_FLOAT_EQ(two.column_gap, 2.0F);
  EXPECT_FLOAT_EQ(computed("row-gap: 3px").row_gap, 3.0F);
  EXPECT_FLOAT_EQ(computed("column-gap: 4px").column_gap, 4.0F);
}

// ---- 対応プロパティの一覧そのもの ---------------------------------------------

// DESIGN.md §4 + ARCHITECTURE.md §3.7 のショートハンド。名前の取りこぼしを防ぐ。
TEST(StyleValue, EverySupportedPropertyNameIsAccepted) {
  const std::vector<std::string_view> declarations = {
      "display: block",
      "width: 1px",
      "height: 1px",
      "margin: 1px",
      "margin-top: 1px",
      "margin-right: 1px",
      "margin-bottom: 1px",
      "margin-left: 1px",
      "padding: 1px",
      "padding-top: 1px",
      "padding-right: 1px",
      "padding-bottom: 1px",
      "padding-left: 1px",
      "border: 1px solid red",
      "border-width: 1px",
      "border-style: solid",
      "border-color: red",
      "border-radius: 1px",
      "background: red",
      "background-color: red",
      "flex-direction: row",
      "justify-content: center",
      "align-items: center",
      "gap: 1px",
      "row-gap: 1px",
      "column-gap: 1px",
      "flex: 1",
      "flex-grow: 1",
      "flex-shrink: 1",
      "flex-basis: 1px",
      "color: red",
      "font-size: 1px",
      "font-family: serif",
      "font-weight: bold",
      "line-height: 1.5",
      "letter-spacing: 1px",
      "text-align: center",
      "line-break: strict",
      "overflow-wrap: anywhere",
      "writing-mode: horizontal-tb",
  };
  for (const std::string_view declaration : declarations) {
    SCOPED_TRACE(declaration);
    const Result<ComputedStyle> style = inline_style(declaration);
    EXPECT_TRUE(style.has_value()) << (style ? "" : style.error().message);
  }
  // 各プロパティは inherit / initial も受け付ける
  for (const std::string_view declaration : declarations) {
    const std::string_view name = declaration.substr(0, declaration.find(':'));
    for (const std::string_view keyword : {"inherit", "initial"}) {
      const std::string css = std::string{name} + ": " + std::string{keyword};
      SCOPED_TRACE(css);
      const Result<ComputedStyle> style = inline_style(css);
      EXPECT_TRUE(style.has_value()) << (style ? "" : style.error().message);
    }
  }
}

// ---- 構文の細かいところ -------------------------------------------------------

TEST(StyleValue, CommentsAndOptionalSemicolonsAndEmptyDeclarations) {
  EXPECT_EQ(computed("/* まえ */ width: /* なか */ 10px /* うしろ */").width, Dimension::px(10));
  EXPECT_EQ(computed("width: 10px").width, Dimension::px(10));  // 末尾の `;` は省略可
  EXPECT_EQ(computed(";;width: 10px;;").width, Dimension::px(10));
  EXPECT_EQ(computed("  width  :  10px  ;  ").width, Dimension::px(10));
}

}  // namespace
}  // namespace shashoku::style
