#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"

// 空のインラインボックス（文字を 1 つも持たない <span> など）と行の高さ（issue #23 / A42）。
//
// CSS 2.1 §10.8:「空のインライン要素も空のインラインボックスを作る。そのボックスは
// マージン・パディング・ボーダーと line-height を持つので、**内容のある要素と同じように**
// この計算に参加する」。§10.8.1 の「テキストも保持された空白も他のインライン内容も含まない
// 行ボックスは高さ 0 として扱う」はそのままなので、空 span **だけ**の段落は行を持たない。
//
// 期待値の出どころは issue #23 の再現表（Chrome 153 の実測）。ここでは偽の TextMeasurer
// （ascent 0.88em / descent 0.12em / line_gap 0）で組むので、実測の 115.84375 px
// （font-size: 80px の line-height: normal）はちょうど 80 になる。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::LineHeight;

StyleFn font_size(float px) {
  return [px](ComputedStyle& style) { style.font_size = px; };
}

StyleFn line_height_px(float px) {
  return [px](ComputedStyle& style) { style.line_height = LineHeight{LineHeight::Kind::Px, px}; };
}

// 文書順のすべての行ボックスの block_size。
std::vector<float> line_heights(const BoxTree& tree) {
  std::vector<float> out;
  for (const LineBox* line : all_lines(tree)) {
    out.push_back(line->rect.block_size);
  }
  return out;
}

void expect_line_heights(const BoxTree& tree, const std::vector<float>& expected) {
  const std::vector<float> actual = line_heights(tree);
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_FLOAT_EQ(actual[i], expected[i]) << "行 " << i;
  }
}

// ---- issue #23 の再現表 -----------------------------------------------------------
// 8 行のうち横書きの 7 行。縦書きの 1 行は次のテスト。

struct ReproCase {
  std::string name;
  std::function<std::vector<Tree>()> children;
  std::vector<float> expected;  // 行ごとの block_size（空なら行ボックス 0 個）
};

TEST(LayoutEmptyInline, ReproTableFromIssue23) {
  const std::vector<ReproCase> cases = {
      // 基準（Chrome 24 / shashoku 23.171875）
      {"A", [] { return std::vector<Tree>{text("A")}; }, {fake_line_height(16)}},
      // 不具合: 空 span の font-size が効かない（Chrome 116 / 期待 115.84375）
      {"A + 空 span(font-size:80px)",
       [] { return std::vector<Tree>{text("A"), inline_box({}, font_size(80))}; },
       {fake_line_height(80)}},
      // 不具合: 空 span の line-height が効かない（Chrome 60 / 期待 60）
      {"A + 空 span(line-height:60px)",
       [] { return std::vector<Tree>{text("A"), inline_box({}, line_height_px(60))}; },
       {60}},
      // 不具合: テキストの前にあっても同じ
      {"空 span(line-height:60px) + A",
       [] { return std::vector<Tree>{inline_box({}, line_height_px(60)), text("A")}; },
       {60}},
      // 現状で正しい: 空 span だけの段落は行ボックスを作らない（CSS 2.1 §10.8.1）
      {"空 span(font-size:80px) だけ",
       [] { return std::vector<Tree>{inline_box({}, font_size(80))}; },
       {}},
      // 現状で正しい: <br> の直後（後ろにアイテムが無い）の空 span はどの行にも参加しない
      {"A + <br> + 空 span(font-size:80px)",
       [] { return std::vector<Tree>{text("A"), br(), inline_box({}, font_size(80))}; },
       {fake_line_height(16)}},
      // 現状で正しい: 畳み込み後に空白 1 個が残る span は空ではない
      {"A + span(font-size:80px, 空白のみ) + B",
       [] {
         return std::vector<Tree>{text("A"), inline_box({text(" ")}, font_size(80)), text("B")};
       },
       {fake_line_height(80)}},
  };
  for (const ReproCase& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    FakeMeasurer measurer;
    const auto root = build({block(test_case.children())});
    const auto tree = run_layout(root, 400, measurer);
    ASSERT_TRUE(tree.has_value());
    expect_line_heights(*tree, test_case.expected);
  }
}

// 再現表の 8 行目（縦書き）。縦書きでは行の高さは中心軸の上下に line-height の半分ずつ。
TEST(LayoutEmptyInline, VerticalEmptySpanJoinsTheLine) {
  FakeMeasurer measurer;
  const auto root =
      build_vertical({block({text("あ"), inline_box({}, font_size(80)), text("い")})});
  const auto tree = run_layout(root, vertical_options(400, 400), measurer);
  ASSERT_TRUE(tree.has_value());
  expect_line_heights(*tree, {fake_line_height(80)});
}

// ---- 組み合わせ: {横書き, 縦書き} × {font-size, line-height} × 空 span の位置 -----------

struct Property {
  std::string name;
  StyleFn style;
  float tall;  // その指定で高くなる行の block_size
};

struct Placement {
  std::string name;
  // 引数は空 span に当てるスタイル。
  std::function<std::vector<Tree>(const StyleFn&)> children;
  std::size_t line_count = 1;
  std::vector<std::size_t> tall_lines;  // 高くなる行の添字
};

TEST(LayoutEmptyInline, EmptyBoxJoinsTheLineItBelongsTo) {
  const std::vector<Property> properties = {
      {"font-size:80px", font_size(80), fake_line_height(80)},
      {"line-height:60px", line_height_px(60), 60},
  };
  const std::vector<Placement> placements = {
      {"行頭",
       [](const StyleFn& style) { return std::vector<Tree>{inline_box({}, style), text("あい")}; },
       1,
       {0}},
      {"行中",
       [](const StyleFn& style) {
         return std::vector<Tree>{text("あ"), inline_box({}, style), text("い")};
       },
       1,
       {0}},
      {"行末",
       [](const StyleFn& style) { return std::vector<Tree>{text("あい"), inline_box({}, style)}; },
       1,
       {0}},
      {"<br> の前",
       [](const StyleFn& style) {
         return std::vector<Tree>{text("あ"), inline_box({}, style), br(), text("い")};
       },
       2,
       {0}},
      {"<br> の後",
       [](const StyleFn& style) {
         return std::vector<Tree>{text("あ"), br(), inline_box({}, style), text("い")};
       },
       2,
       {1}},
      // <br> の直後で、後ろにアイテムが無い = どの行にも参加しない（issue #23 の規則）
      {"<br> の後で段落の末尾",
       [](const StyleFn& style) {
         return std::vector<Tree>{text("あ"), br(), inline_box({}, style)};
       },
       1,
       {}},
  };

  for (const bool vertical : {false, true}) {
    for (const Property& property : properties) {
      for (const Placement& placement : placements) {
        SCOPED_TRACE(std::string(vertical ? "縦書き" : "横書き") + " / " + property.name + " / " +
                     placement.name);
        FakeMeasurer measurer;
        std::vector<Tree> children = placement.children(property.style);
        const auto root = vertical ? build_vertical({block(std::move(children))})
                                   : build({block(std::move(children))});
        const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                                   : run_layout(root, 400, measurer);
        ASSERT_TRUE(tree.has_value());
        std::vector<float> expected(placement.line_count, fake_line_height(16));
        for (const std::size_t at : placement.tall_lines) {
          expected[at] = property.tall;
        }
        expect_line_heights(*tree, expected);
      }
    }
  }
}

// line-height と font-size は両方が効く（line-height: 1 なら font-size そのもの）。
TEST(LayoutEmptyInline, FontSizeAndLineHeightBothApply) {
  FakeMeasurer measurer;
  const auto root = build({block({text("A"), inline_box({}, [](ComputedStyle& style) {
                                    style.font_size = 80;
                                    style.line_height = LineHeight{LineHeight::Kind::Number, 1};
                                  })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  expect_line_heights(*tree, {80});
}

// 折り返しの位置にちょうど来る空 span は「char_pos 以降の最初のアイテムの行」= 次の行。
TEST(LayoutEmptyInline, EmptyBoxAtTheWrapPositionJoinsTheFollowingLine) {
  FakeMeasurer measurer;
  const auto make = [] {
    return build({block({text("あい"), inline_box({}, font_size(80)), text("うえ")})});
  };
  // 幅 32 = 全角 2 文字。空 span は 3 文字目（次の行の先頭）の直前にある
  const auto narrow = run_layout(make(), 32, measurer);
  ASSERT_TRUE(narrow.has_value());
  expect_line_heights(*narrow, {fake_line_height(16), fake_line_height(80)});
  // 折り返さない幅なら 1 行で、その行が高くなる
  const auto wide = run_layout(make(), 400, measurer);
  ASSERT_TRUE(wide.has_value());
  expect_line_heights(*wide, {fake_line_height(80)});
}

// 入れ子の空 span。内側・外側のどちらに指定があっても効く。
TEST(LayoutEmptyInline, NestedEmptyBoxes) {
  FakeMeasurer measurer;
  // 外側に指定（内側は継承するので、空ボックスは 2 つとも 80px）
  const auto outer = build({block({text("A"), inline_box({inline_box({})}, font_size(80))})});
  const auto outer_tree = run_layout(outer, 400, measurer);
  ASSERT_TRUE(outer_tree.has_value());
  expect_line_heights(*outer_tree, {fake_line_height(80)});
  // 内側だけに指定（外側は 16px のまま）
  const auto inner = build({block({text("A"), inline_box({inline_box({}, font_size(80))})})});
  const auto inner_tree = run_layout(inner, 400, measurer);
  ASSERT_TRUE(inner_tree.has_value());
  expect_line_heights(*inner_tree, {fake_line_height(80)});
}

// background-color を付けた空 span: 行の高さには効くが、背景の矩形は出ない
// （CSS 2.1 どおり。文字が 1 つも無いので幅 0 の矩形になる）。
TEST(LayoutEmptyInline, EmptyBoxWithBackgroundPaintsNothing) {
  FakeMeasurer measurer;
  const auto root = build({block({text("A"), inline_box({}, [](ComputedStyle& style) {
                                    style.font_size = 80;
                                    style.background_color = Color{255, 0, 0, 255};
                                  })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  expect_line_heights(*tree, {fake_line_height(80)});
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_TRUE(backgrounds(*lines[0]).empty());
}

// ルビ組の中の空 span（親文字の並びの一部）。組は 1 アイテムなので、
// 空 span は「char_pos 以降の最初のアイテム」が無く、直前のアイテム = 組の行に参加する。
TEST(LayoutEmptyInline, EmptyBoxInsideRubyBase) {
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical ? "縦書き" : "横書き");
    FakeMeasurer measurer;
    std::vector<Tree> children = {ruby({text("漢"), inline_box({}, font_size(80)), rt("かん")})};
    const auto root = vertical ? build_vertical({block(std::move(children))})
                               : build({block(std::move(children))});
    const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                               : run_layout(root, 400, measurer);
    ASSERT_TRUE(tree.has_value());
    expect_line_heights(*tree, {fake_line_height(80)});
  }
}

// 中身が空の <ruby></ruby> は空の span と同じ扱い（issue #23 の「方針」）。
TEST(LayoutEmptyInline, EmptyRubyElementIsAnEmptyInlineBox) {
  FakeMeasurer measurer;
  const auto root = build({block({text("A"), ruby({}, font_size(80)), text("B")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  expect_line_heights(*tree, {fake_line_height(80)});
  EXPECT_EQ(line_texts(*tree), std::vector<std::string>{"AB"});
}

// flex アイテムの中の空 span。幅（固有寸法）は変えず、高さだけが変わる。
TEST(LayoutEmptyInline, EmptyBoxInsideFlexItem) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({text("あい"), inline_box({}, font_size(80))})}, [](ComputedStyle& style) {
        style.flex_direction = style::FlexDirection::Column;
        style.align_items = style::AlignItems::FlexStart;
      })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  expect_line_heights(*tree, {fake_line_height(80)});
  // 幅は max-content（全角 2 文字 = 32）のまま。空ボックスの幅は 0
  const std::vector<BlockBox>* items = tree->root.blocks()->front().blocks();
  ASSERT_NE(items, nullptr);
  ASSERT_EQ(items->size(), 1U);
  EXPECT_FLOAT_EQ(items->front().rect.inline_size, 32);
  EXPECT_FLOAT_EQ(items->front().rect.block_size, fake_line_height(80));
}

// ---- 「変わらないこと」の固定 --------------------------------------------------------

// 空ボックスは linebreak::Item を増やさないので、改行位置は変わらない。
TEST(LayoutEmptyInline, BreakPositionsAreUnchanged) {
  FakeMeasurer measurer;
  const auto plain = build({block({text("あいうえお")})});
  const auto plain_tree = run_layout(plain, 32, measurer);
  ASSERT_TRUE(plain_tree.has_value());
  const std::vector<std::string> expected = line_texts(*plain_tree);
  ASSERT_EQ(expected.size(), 3U);

  // 空 span を文字の間のあちこちに入れても、行の中身は同じ
  const auto spiced = build({block(
      {inline_box({}, font_size(80)), text("あい"), inline_box({}, font_size(80)), text("うえ"),
       inline_box({}, font_size(80)), text("お"), inline_box({}, font_size(80))})});
  const auto spiced_tree = run_layout(spiced, 32, measurer);
  ASSERT_TRUE(spiced_tree.has_value());
  EXPECT_EQ(line_texts(*spiced_tree), expected);
}

// 固有寸法（min-content / max-content）も変わらない。
TEST(LayoutEmptyInline, IntrinsicSizesAreUnchanged) {
  const auto make = [](bool with_empty) {
    std::vector<Tree> children = {text("ab cdef")};
    if (with_empty) {
      children.insert(children.begin(), inline_box({}, font_size(80)));
      children.push_back(inline_box({}, font_size(80)));
    }
    return build({flex({block(std::move(children))}, [](ComputedStyle& style) {
      style.flex_direction = style::FlexDirection::Column;
      style.align_items = style::AlignItems::FlexStart;
    })});
  };
  for (const float width : {400.0F, 20.0F}) {
    SCOPED_TRACE(width);
    FakeMeasurer measurer;
    const auto plain = run_layout(make(false), width, measurer);
    ASSERT_TRUE(plain.has_value());
    const auto spiced = run_layout(make(true), width, measurer);
    ASSERT_TRUE(spiced.has_value());
    // 400 なら max-content（7 文字 × 0.5em = 56）、20 なら min-content（"cdef" = 32）
    EXPECT_FLOAT_EQ(spiced->root.blocks()->front().blocks()->front().rect.inline_size,
                    plain->root.blocks()->front().blocks()->front().rect.inline_size);
  }
}

}  // namespace
}  // namespace shashoku::layout::test
