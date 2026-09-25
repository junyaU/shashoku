#include "shashoku/error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// fail loudly（DESIGN.md §3-6 / ARCHITECTURE.md A46）の網羅。
//
// 見るのは **kind の識別子・入力位置・hint** の 3 つ（A46-6「機械が読める契約」）。
// message の文面は人向けで版が変われば変わりうるので、文字列の一致を見るのは
// 「文面そのものが仕様」の箇所（著者の綴りを残す、など）だけに限る。
//
// A46「一度に全部」: style は安全に解決を続けられる問題を集めて最後まで解決する。
// `resolve_collect()`（style_test_dom.hpp）が集めた診断をそのまま返す。

namespace shashoku::style {
namespace {

// ---- 共通の道具 -----------------------------------------------------------------

// `<div style="...">` を解決して、計算値と集めた診断をまとめて返す。
struct Outcome {
  ComputedStyle style;  // errors があっても木は最後まで解決される（捨てた宣言は「無かった」扱い）
  std::vector<RenderError> errors;
  bool truncated = false;

  [[nodiscard]] std::vector<ErrorKind> kinds() const {
    std::vector<ErrorKind> out;
    out.reserve(errors.size());
    for (const RenderError& error : errors) {
      out.push_back(error.kind);
    }
    return out;
  }
};

Outcome collect(const html::Node& tree,
                std::size_t max_diagnostics = RenderLimits{}.max_diagnostics) {
  Resolved resolved = resolve_collect(tree, kMaxStyleRules, kMaxLengthPx, max_diagnostics);
  Outcome out;
  out.errors = std::move(resolved.errors);
  out.truncated = resolved.truncated;
  if (resolved.tree && !resolved.tree->children.empty()) {
    out.style = resolved.tree->children.front().style;
  }
  return out;
}

Outcome collect_inline(std::string_view declarations, SourceLocation attribute = {}) {
  return collect(test_root(test_element("div", {test_attr("style", declarations, attribute)})));
}

Outcome collect_sheet(std::string_view css) {
  return collect(test_root(test_style_element(css), test_element("div")));
}

// 宣言の位置が付くことまで見る（fail loudly は「どこが原因か」まで含めて成り立つ）。
constexpr SourceLocation kStyleAttribute{.offset = 41, .line = 3, .column = 9};

struct Case {
  std::string_view css;
  ErrorKind kind;
};

// 宣言 1 つにつき診断 1 件。kind はその種類（位置は下の「エラーの位置」節で見る）。
void expect_errors(const std::vector<Case>& cases) {
  for (const Case& test : cases) {
    SCOPED_TRACE(test.css);
    const Outcome outcome = collect_inline(test.css);
    ASSERT_EQ(outcome.errors.size(), 1U) << "診断が 1 件のはず";
    EXPECT_EQ(outcome.errors.front().kind, test.kind) << outcome.errors.front().message;
    EXPECT_FALSE(outcome.errors.front().message.empty());
  }
}

void expect_sheet_errors(const std::vector<Case>& cases) {
  for (const Case& test : cases) {
    SCOPED_TRACE(test.css);
    const Outcome outcome = collect_sheet(test.css);
    ASSERT_EQ(outcome.errors.size(), 1U) << "診断が 1 件のはず";
    EXPECT_EQ(outcome.errors.front().kind, test.kind) << outcome.errors.front().message;
    EXPECT_FALSE(outcome.errors.front().message.empty());
  }
}

// ---- 対応外のプロパティ ---------------------------------------------------------

TEST(StyleError, UnsupportedProperties) {
  expect_errors({
      {"float: left", ErrorKind::UnsupportedProperty},
      {"position: absolute", ErrorKind::UnsupportedProperty},
      {"box-shadow: 0 0 4px black", ErrorKind::UnsupportedProperty},
      {"grid-template-columns: 1fr 1fr", ErrorKind::UnsupportedProperty},
      {"border-top: 1px solid red", ErrorKind::UnsupportedProperty},
      {"border-top-left-radius: 4px", ErrorKind::UnsupportedProperty},
      {"overflow: hidden", ErrorKind::UnsupportedProperty},
      {"transform: rotate(3deg)", ErrorKind::UnsupportedProperty},
      {"opacity: 0.5", ErrorKind::UnsupportedProperty},
      {"flex-wrap: wrap", ErrorKind::UnsupportedProperty},
      {"text-orientation: upright", ErrorKind::UnsupportedProperty},
      {"font: 16px serif", ErrorKind::UnsupportedProperty},
      {"-webkit-line-clamp: 2", ErrorKind::UnsupportedProperty},
  });
}

// A56: `box-sizing` は対応したので、プロパティ名そのものは通る。値だけが 2 つに限られる。
TEST(StyleError, BoxSizingIsSupportedAndOnlyItsValuesAreChecked) {
  for (const std::string_view css : {"box-sizing: content-box", "box-sizing: border-box"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    EXPECT_TRUE(outcome.errors.empty())
        << (outcome.errors.empty() ? "" : outcome.errors.front().message);
  }
  const Outcome outcome = collect_inline("box-sizing: padding-box");
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedValue);
  EXPECT_NE(outcome.errors.front().message.find("content-box, border-box"), std::string::npos)
      << outcome.errors.front().message;
}

// `* { box-sizing: border-box }` は普段の AI の HTML の定番（A56 の根拠。10/10 が書いた）。
// `box-sizing` は**箱のプロパティではない**ので、`*` が inline の要素に当たっても
// `unsupported-layout` にならない（なったら 1 行そのままでは通らなくなる）。
TEST(StyleError, UniversalBoxSizingReachesInlineElementsWithoutError) {
  const html::Node tree = test_root(test_style_element("* { box-sizing: border-box }"),
                                    test_parent("div", {}, test_element("span")));
  const Resolved resolved = resolve_collect(tree);
  ASSERT_TRUE(resolved.tree.has_value());
  EXPECT_TRUE(resolved.errors.empty())
      << (resolved.errors.empty() ? "" : resolved.errors.front().message);
  ASSERT_FALSE(resolved.tree->children.empty());
  const StyledNode& div = resolved.tree->children.front();
  EXPECT_EQ(div.style.box_sizing, BoxSizing::BorderBox);
  ASSERT_FALSE(div.children.empty());
  EXPECT_EQ(div.children.front().style.display, Display::Inline);
  EXPECT_EQ(div.children.front().style.box_sizing, BoxSizing::BorderBox);
}

// 安定した契約はケバブケースの識別子（A46-6）。message ではなくこちらで機械が読む。
TEST(StyleError, KindIdentifierIsTheStableContract) {
  EXPECT_EQ(to_string(collect_inline("float: left").errors.front().kind), "unsupported-property");
  EXPECT_EQ(to_string(collect_inline("display: grid").errors.front().kind), "unsupported-value");
  EXPECT_EQ(to_string(collect_inline("color red").errors.front().kind), "css-parse");
  EXPECT_EQ(to_string(collect(test_root(test_element("span", {test_attr("style", "padding: 1px")})))
                          .errors.front()
                          .kind),
            "unsupported-layout");
}

TEST(StyleError, UnsupportedPropertyMessageNamesTheProperty) {
  const Outcome outcome = collect_inline("float: left");
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_NE(outcome.errors.front().message.find("float"), std::string::npos)
      << outcome.errors.front().message;
}

// ---- hint（A46「直し方つき」）-----------------------------------------------------
//
// 「未対応です」だけでは次に何をすればよいか分からない（#20）。**未対応だと分かっている
// プロパティ**には一言だけ添える。A46 以降、その一言は `RenderError::hint` に入り、
// **message には混ぜない**（機械側が分けて読める。CLI は別の行に出す）。

// `kPropertyHints`（src/style/value_parser.cpp）に載っている名前。
// ここが実質的な表の写しなので、表を増やしたらこの配列も増やす。
constexpr auto kHintedProperties = std::to_array<std::string_view>({
    "background-clip",
    "background-image",
    "flex-wrap",
    "float",
    "grid-area",
    "grid-auto-columns",
    "grid-auto-flow",
    "grid-auto-rows",
    "grid-column",
    "grid-column-gap",
    "grid-gap",
    "grid-row",
    "grid-row-gap",
    "grid-template-areas",
    "grid-template-columns",
    "grid-template-rows",
    "max-height",
    "max-width",
    "min-height",
    "min-width",
    "position",
    "text-combine-upright",
    // 片側だけの border は接頭辞の一致で 1 つの規則になっている（A53 の hint (a)）
    "border-top",
    "border-right",
    "border-bottom",
    "border-left",
    "border-top-width",
    "border-right-style",
    "border-bottom-color",
    "border-left-width",
});

TEST(StyleError, KnownUnsupportedPropertiesCarryAHint) {
  for (const std::string_view property : kHintedProperties) {
    SCOPED_TRACE(property);
    const Outcome outcome = collect_inline(std::string{property} + ": 1px");
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
    EXPECT_FALSE(error.hint.empty()) << "hint が空: " << error.message;
    // message は「何が対応外か」だけ。代替は hint 側にある（括弧の文は残っていない）
    EXPECT_EQ(error.message, "`" + std::string{property} + "` is not a supported property");
    EXPECT_EQ(error.message.find('('), std::string::npos) << error.message;
  }
}

// 代替の中身（「shashoku で実際に同じ結果が出せると確かめたものだけ」という規則は A46 でも不変）。
TEST(StyleError, HintsNameTheVerifiedAlternative) {
  struct Hint {
    std::string_view css;
    std::string_view needle;
  };
  const std::array<Hint, 12> cases{{
      // CSS Box Alignment 3 §8.4 の legacy gap properties。写し先は 3 つとも対応済みだが、
      // shashoku に grid は無く flex に `grid-gap` と書く動機もないので別名は入れない（A35）。
      // 代わりに写し先を案内する
      {"grid-gap: 4px", "`gap`"},
      {"grid-row-gap: 4px", "`row-gap`"},
      {"grid-column-gap: 4px", "`column-gap`"},
      {"max-width: 200px", "`width`"},
      {"min-width: 200px", "`width`"},
      {"max-height: 200px", "`height`"},
      {"min-height: 200px", "`height`"},
      {"background-image: url(x.png)", "`background-color`"},
      {"float: left", "display: flex"},
      {"position: absolute", "display: flex"},
      {"flex-wrap: wrap", "single-line"},
      {"text-combine-upright: all", "not implemented"},
  }};
  for (const Hint& test : cases) {
    SCOPED_TRACE(test.css);
    const Outcome outcome = collect_inline(test.css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_NE(outcome.errors.front().hint.find(test.needle), std::string::npos)
        << outcome.errors.front().hint;
  }
}

// (a) ベンダー接頭辞。外した名前が対応表にあれば「接頭辞を外す」と言う。
TEST(StyleError, VendorPrefixesHintToDropThePrefix) {
  for (const std::string_view css : {"-webkit-border-radius: 4px", "-moz-border-radius: 4px",
                                     "-ms-flex: 1", "-o-writing-mode: vertical-rl"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
    EXPECT_NE(error.hint.find("drop the vendor prefix"), std::string::npos) << error.hint;
  }
  // 外した名前を hint に書く（何と書き直せばよいかが分かる）
  EXPECT_NE(
      collect_inline("-webkit-border-radius: 4px").errors.front().hint.find("`border-radius`"),
      std::string::npos);
}

// 外しても対応外なら「接頭辞を外せ」とは言わない（間違った助言をしない）。
TEST(StyleError, VendorPrefixesOnUnsupportedNamesGetNoPrefixHint) {
  for (const std::string_view css : {"-webkit-line-clamp: 2", "-o-transform: rotate(3deg)"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_TRUE(outcome.errors.front().hint.empty()) << outcome.errors.front().hint;
  }
}

// (b) 削ると危険な組（docs/guide/writing-html-for-shashoku.md §5）。
// `background-clip: text` だけ消して `color: transparent` を残すと**文字が消える**。
TEST(StyleError, BackgroundClipWarnsAboutTheDangerousPair) {
  for (const std::string_view css : {"background-clip: text", "-webkit-background-clip: text"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
    EXPECT_NE(error.hint.find("color: transparent"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("disappear"), std::string::npos) << error.hint;
  }
}

// (c) inline への箱プロパティ。`display: block` でも通るが文の流れが切れるので、
// **宣言を削る**ほうを勧める（A46 の 2。検証 C の観察）。A53 のあとは「独立した箱なら
// flex アイテムにする」という**成立条件つきの代替**も添える。
TEST(StyleError, InlineBoxPropertyHintPrefersDroppingTheDeclaration) {
  const Outcome outcome =
      collect(test_root(test_element("span", {test_attr("style", "padding: 4px")})));
  ASSERT_EQ(outcome.errors.size(), 1U);
  const RenderError& error = outcome.errors.front();
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(error.hint.find("drop the declaration"), std::string::npos) << error.hint;
  EXPECT_NE(error.hint.find("display: block"), std::string::npos) << error.hint;
  // 条件つきの代替（tag / pill / badge なら flex アイテムにする）とガイドの節番号
  EXPECT_NE(error.hint.find("display: flex"), std::string::npos) << error.hint;
  EXPECT_NE(error.hint.find("guide §3-(4)"), std::string::npos) << error.hint;
  // message のほうには代替を書かない
  EXPECT_EQ(error.message.find("drop the declaration"), std::string::npos) << error.message;
}

// ---- 条件つきの hint（A53 / A48 の追記）---------------------------------------------
//
// ユーザーの方針: **hint は無条件の置き換えにしない**。見た目が変わる代替（gradient → 単色、
// border → 1px の div）と、同じ結果を保てる修正を区別し、成立条件を添える。宣言だけから
// 条件を判定できないときは代替を断定せず、ガイドの節番号（`docs/guide/…` の §4.3 など）を示す。

// (a) 片側だけの `border-*`。罫は 1px の div で置ける（レイアウトを 1px 食う）が、
// 箱の枠の一辺には等価な書き方が無い。
TEST(StyleError, PerSideBorderPropertiesHintAtTheOnePixelDivider) {
  for (const std::string_view css :
       {"border-top: 1px solid red", "border-right: 1px solid red", "border-bottom-width: 1px",
        "border-left-style: solid", "border-top-color: red"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
    EXPECT_NE(error.hint.find("height: 1px"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("guide §4.3"), std::string::npos) << error.hint;
    // 等価でない側は断定しない
    EXPECT_NE(error.hint.find("no equivalent"), std::string::npos) << error.hint;
  }
}

// 角の丸め（`border-top-left-radius`）は別物なので、罫の助言を当てない。
TEST(StyleError, PerSideBorderHintDoesNotLeakIntoCornerRadius) {
  for (const std::string_view css :
       {"border-top-left-radius: 4px", "border-bottom-right-radius: 4px"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_TRUE(outcome.errors.front().hint.empty()) << outcome.errors.front().hint;
  }
}

// (b) min / max。`min-height` だけは「親が既定の flex なら削ってよい」という条件を添える。
TEST(StyleError, MinHeightHintNamesTheFlexStretchCondition) {
  const Outcome outcome = collect_inline("min-height: 80px");
  ASSERT_EQ(outcome.errors.size(), 1U);
  const RenderError& error = outcome.errors.front();
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
  EXPECT_NE(error.hint.find("align-items: stretch"), std::string::npos) << error.hint;
  EXPECT_NE(error.hint.find("`height`"), std::string::npos) << error.hint;
  EXPECT_NE(error.hint.find("drop it"), std::string::npos) << error.hint;
}

TEST(StyleError, OtherMinMaxHintsOfferAFixedSizeOrDropping) {
  for (const std::string_view css : {"max-height: 80px", "min-width: 80px", "max-width: 80px"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_NE(error.hint.find("no min/max sizes"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("drop it"), std::string::npos) << error.hint;
    // `min-height` だけの条件はここには出ない
    EXPECT_EQ(error.hint.find("align-items: stretch"), std::string::npos) << error.hint;
  }
}

// (c) グラデーション。プロパティ名では分からないので**値レベルの hint**（A53）。
TEST(StyleError, GradientValuesCarryAValueLevelHint) {
  for (const std::string_view css :
       {"background: linear-gradient(90deg, #fff, #000)",
        "background-color: radial-gradient(#fff, #000)", "background: conic-gradient(#fff, #000)",
        "background: repeating-linear-gradient(45deg, #fff, #000)"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css, kStyleAttribute);
    ASSERT_EQ(outcome.errors.size(), 1U) << css;
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedValue) << error.message;
    EXPECT_EQ(error.location.value_or(SourceLocation{}), kStyleAttribute);
    EXPECT_NE(error.hint.find("no gradients"), std::string::npos) << error.hint;
    // 見た目が変わることと、削ると危険な組を明記する
    EXPECT_NE(error.hint.find("flat"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("background-clip"), std::string::npos) << error.hint;
  }
  // gradient でない対応外の色には値レベルの hint を付けない
  const Outcome plain = collect_inline("background-color: hsl(0, 100%, 50%)");
  ASSERT_EQ(plain.errors.size(), 1U);
  EXPECT_TRUE(plain.errors.front().hint.empty()) << plain.errors.front().hint;
}

// `white-space` の `pre` 系は「ソース中の改行と空白をそのまま残す」ので、畳み込みを変えない
// shashoku では真似られない。プロパティ名では分からないので**値レベルの hint**（A53 / A58）。
TEST(StyleError, PreservingWhiteSpaceValuesCarryAValueLevelHint) {
  for (const std::string_view value : {"pre", "pre-wrap", "pre-line", "break-spaces"}) {
    SCOPED_TRACE(value);
    const Outcome outcome = collect_inline("white-space: " + std::string{value}, kStyleAttribute);
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedValue) << error.message;
    EXPECT_EQ(error.location.value_or(SourceLocation{}), kStyleAttribute);
    // 使える値と、shashoku で改行位置を決める唯一の手段（`<br>`）を示す
    EXPECT_NE(error.hint.find("nowrap"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("<br>"), std::string::npos) << error.hint;
  }
  // 知らない値には何も足さない（間違った助言をしない。A46 / A48 と同じ規則）
  const Outcome unknown = collect_inline("white-space: foo");
  ASSERT_EQ(unknown.errors.size(), 1U);
  EXPECT_EQ(unknown.errors.front().kind, ErrorKind::UnsupportedValue);
  EXPECT_TRUE(unknown.errors.front().hint.empty()) << unknown.errors.front().hint;
  // 値のエラーは「何が使えるか」を数え上げる
  for (const std::string_view supported : {"normal", "nowrap"}) {
    EXPECT_NE(unknown.errors.front().message.find(supported), std::string::npos)
        << unknown.errors.front().message;
  }
}

TEST(StyleError, BackgroundImageHintsAtASolidColorOrAnImgTag) {
  const Outcome outcome = collect_inline("background-image: url(x.png)");
  ASSERT_EQ(outcome.errors.size(), 1U);
  const RenderError& error = outcome.errors.front();
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
  EXPECT_NE(error.hint.find("background-color"), std::string::npos) << error.hint;
  EXPECT_NE(error.hint.find("--image"), std::string::npos) << error.hint;
}

// (d) grid。プロパティ（`grid-template-*` など）と値（`display: grid`）で同じ hint。
TEST(StyleError, GridPropertiesAndTheGridDisplayValueShareOneHint) {
  for (const std::string_view css :
       {"grid-template-columns: 1fr 1fr", "grid-template-rows: auto", "grid-template-areas: \"a\"",
        "grid-area: a", "grid-column: 1 / 3", "grid-row: 1", "grid-auto-flow: row",
        "grid-auto-rows: 1fr", "grid-auto-columns: 1fr"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty) << error.message;
    EXPECT_NE(error.hint.find("flex: 1 1 0"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("guide §4.2"), std::string::npos) << error.hint;
    EXPECT_NE(error.hint.find("no equivalent"), std::string::npos) << error.hint;
  }
  const Outcome value = collect_inline("display: grid", kStyleAttribute);
  ASSERT_EQ(value.errors.size(), 1U);
  EXPECT_EQ(value.errors.front().kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(value.errors.front().location.value_or(SourceLocation{}), kStyleAttribute);
  EXPECT_NE(value.errors.front().hint.find("flex: 1 1 0"), std::string::npos)
      << value.errors.front().hint;
}

// legacy gap の 3 つは写し先が対応済みなので、grid の hint に飲み込まれない（A35）。
TEST(StyleError, LegacyGapPropertiesKeepTheirOwnHint) {
  for (const std::string_view css :
       {"grid-gap: 4px", "grid-row-gap: 4px", "grid-column-gap: 4px"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_NE(outcome.errors.front().hint.find("legacy name"), std::string::npos)
        << outcome.errors.front().hint;
  }
}

// (e) `display: inline-block`（値）。flex の中かどうかで書き方が変わるので両方を言う。
TEST(StyleError, InlineBlockValueHintsAtTheFlexItem) {
  const Outcome outcome = collect_inline("display: inline-block", kStyleAttribute);
  ASSERT_EQ(outcome.errors.size(), 1U);
  const RenderError& error = outcome.errors.front();
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(error.location.value_or(SourceLocation{}), kStyleAttribute);
  EXPECT_NE(error.hint.find("flex: none"), std::string::npos) << error.hint;
  EXPECT_NE(error.hint.find("guide §3-(4)"), std::string::npos) << error.hint;
}

// 対応外だと分かっていない display の値には何も足さない（間違った助言をしない）。
TEST(StyleError, OtherDisplayValuesGetNoHint) {
  for (const std::string_view css : {"display: table", "display: contents", "display: blahblah"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedValue);
    EXPECT_TRUE(outcome.errors.front().hint.empty()) << outcome.errors.front().hint;
  }
}

// ベンダー接頭辞を外して引き直す経路は、足した規則にも効く（A48）。
TEST(StyleError, VendorPrefixesReachTheNewHintRulesToo) {
  EXPECT_NE(
      collect_inline("-webkit-border-top: 1px solid red").errors.front().hint.find("height: 1px"),
      std::string::npos);
  EXPECT_NE(collect_inline("-moz-min-height: 80px").errors.front().hint.find("align-items"),
            std::string::npos);
  // 既存の動作（`-webkit-background-clip`）を壊さない
  EXPECT_NE(collect_inline("-webkit-background-clip: text").errors.front().hint.find("disappear"),
            std::string::npos);
}

// 表に無い名前（綴り間違い・そもそも知らないプロパティ）には何も足さない。
// 間違った助言をするくらいなら、何も言わないほうがよい。
TEST(StyleError, UnknownPropertiesGetNoHint) {
  for (const std::string_view css : {"floatt: left", "-webkit-line-clamp: 2",
                                     "border-top-left-radius: 4px", "text-orientation: upright"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_inline(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedProperty);
    EXPECT_TRUE(outcome.errors.front().hint.empty()) << outcome.errors.front().hint;
    EXPECT_EQ(outcome.errors.front().message.find('('), std::string::npos)
        << outcome.errors.front().message;
  }
}

// `word-wrap` は `overflow-wrap` の legacy name alias（CSS Text 3 §5.4。issue #25）なので
// プロパティとしては通る。値が対応外のときだけ落ち、そのときは**著者が書いた綴り**と
// 宣言の位置で報告する（CSSOM を持たない shashoku で旧名が見える唯一の場所。A35）。
// ここは「文面そのものが仕様」なので、例外的に message を見る。
TEST(StyleError, WordWrapValueErrorsKeepTheAuthorSpellingAndLocation) {
  const SourceLocation attribute{.offset = 12, .line = 1, .column = 6};
  const Outcome outcome = collect_inline("word-wrap: foo", attribute);
  ASSERT_EQ(outcome.errors.size(), 1U);
  const RenderError& error = outcome.errors.front();
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(error.location.value_or(SourceLocation{}), attribute);
  EXPECT_TRUE(error.message.starts_with("`word-wrap: foo` is not supported")) << error.message;
  EXPECT_EQ(error.message.find("overflow-wrap"), std::string::npos) << error.message;
}

// ---- 対応外の値・単位 -----------------------------------------------------------

TEST(StyleError, UnsupportedUnits) {
  expect_errors({
      {"width: 2rem", ErrorKind::UnsupportedValue},
      {"width: 50vw", ErrorKind::UnsupportedValue},
      {"font-size: 12pt", ErrorKind::UnsupportedValue},
      {"width: calc(100% - 10px)", ErrorKind::UnsupportedValue},
      {"margin: 1ex", ErrorKind::UnsupportedValue},
      {"width: 10", ErrorKind::UnsupportedValue},  // 単位なしの非 0
  });
}

TEST(StyleError, UnsupportedKeywords) {
  expect_errors({
      {"display: grid", ErrorKind::UnsupportedValue},
      {"display: inline-block", ErrorKind::UnsupportedValue},
      {"flex-direction: row-reverse", ErrorKind::UnsupportedValue},
      {"flex-direction: column-reverse", ErrorKind::UnsupportedValue},
      {"align-items: baseline", ErrorKind::UnsupportedValue},
      {"justify-content: stretch", ErrorKind::UnsupportedValue},
      {"font-weight: bolder", ErrorKind::UnsupportedValue},
      {"font-weight: lighter", ErrorKind::UnsupportedValue},
      {"font-weight: 450", ErrorKind::UnsupportedValue},
      {"writing-mode: vertical-lr", ErrorKind::UnsupportedValue},
      {"line-break: anywhere", ErrorKind::UnsupportedValue},
      {"white-space: pre", ErrorKind::UnsupportedValue},
      {"white-space: pre-wrap", ErrorKind::UnsupportedValue},
      {"white-space: pre-line", ErrorKind::UnsupportedValue},
      {"white-space: break-spaces", ErrorKind::UnsupportedValue},
      {"text-align: justify-all", ErrorKind::UnsupportedValue},
      {"border-style: dashed", ErrorKind::UnsupportedValue},
      {"width: min-content", ErrorKind::UnsupportedValue},
      {"font-size: large", ErrorKind::UnsupportedValue},
  });
}

// 値のエラーは「何が使えるか」を数え上げる（hint を持たない代わりの手がかり）。
TEST(StyleError, UnsupportedValueMessageListsWhatIsSupported) {
  const Outcome outcome = collect_inline("display: grid");
  ASSERT_EQ(outcome.errors.size(), 1U);
  for (const std::string_view supported : {"block", "flex", "inline", "none"}) {
    EXPECT_NE(outcome.errors.front().message.find(supported), std::string::npos)
        << outcome.errors.front().message;
  }
}

TEST(StyleError, UnsupportedColors) {
  expect_errors({
      {"color: hsl(0, 100%, 50%)", ErrorKind::UnsupportedValue},
      {"color: hsla(0, 100%, 50%, 0.5)", ErrorKind::UnsupportedValue},
      {"color: lab(50% 40 59)", ErrorKind::UnsupportedValue},
      {"color: #12345", ErrorKind::UnsupportedValue},
      {"color: #gggggg", ErrorKind::UnsupportedValue},
      {"color: notacolor", ErrorKind::UnsupportedValue},
      {"color: rgb(1, 2)", ErrorKind::UnsupportedValue},
      {"color: rgb(1 2 3 4)", ErrorKind::UnsupportedValue},
      // currentColor は border-color だけ
      {"color: currentColor", ErrorKind::UnsupportedValue},
      {"background-color: currentColor", ErrorKind::UnsupportedValue},
  });
}

TEST(StyleError, PercentIsOnlyForWidthAndFlexBasis) {
  expect_errors({
      {"height: 50%", ErrorKind::UnsupportedValue},
      {"margin: 10%", ErrorKind::UnsupportedValue},
      {"padding: 10%", ErrorKind::UnsupportedValue},
      {"font-size: 50%", ErrorKind::UnsupportedValue},
      {"letter-spacing: 10%", ErrorKind::UnsupportedValue},
      {"gap: 10%", ErrorKind::UnsupportedValue},
      {"border-radius: 10%", ErrorKind::UnsupportedValue},
  });
}

// ---- 負の値（issue #26）----------------------------------------------------------
//
// 負を許さないプロパティを**総当たり**で並べる。DESIGN.md §10 の「テスト群が実質的な
// 仕様書」を値の検査にも当てるためで、ここから下の 5 つのテストを並べて読めば
// 「どのプロパティが負を許すか」が表から分かる。
//
// 表に無い項目は `to_length()` の引数（`allow_negative`）を 1 つ間違えただけで黙って
// 通るようになり、`font-size` が通れば送り 0 の「文字が消えた成功 PNG」になる
// （#19 が A36 で潰したのと同じ形）。判定そのものは前からあるので製品コードは変えていない。
//
// 種類は `UnsupportedValue`。表現範囲外（`1e39px`）は `LimitExceeded` で別物
// （A36 / #19。`tests/style/limit_test.cpp` の OutOfRangeNumbersAreLimitExceeded 以下）。

constexpr std::string_view kNegativeLength =
    "negative lengths are only allowed for `margin` and `letter-spacing`";
constexpr std::string_view kNegativePercent = "negative percentages are not allowed";
constexpr std::string_view kNonNegativeNumber = "expected a non-negative number";

struct ValueCase {
  std::string_view css;
  std::string_view detail;  // 文面に必ず出る手がかり（どの規則で止まったか）
};

void expect_rejected_with_location(const std::vector<ValueCase>& cases) {
  for (const ValueCase& test : cases) {
    SCOPED_TRACE(test.css);
    const Outcome outcome = collect_inline(test.css, kStyleAttribute);
    ASSERT_EQ(outcome.errors.size(), 1U) << "診断が 1 件のはず";
    const RenderError& error = outcome.errors.front();
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedValue) << error.message;
    EXPECT_EQ(error.location.value_or(SourceLocation{}), kStyleAttribute) << error.message;
    EXPECT_NE(error.message.find(test.detail), std::string::npos) << error.message;
  }
}

TEST(StyleError, NegativeValuesAreRejectedExceptForMarginAndLetterSpacing) {
  expect_rejected_with_location({
      // 長さ（px / em）。ショートハンドも longhand も同じ規則
      {"width: -10px", kNegativeLength},
      {"height: -10px", kNegativeLength},
      {"padding: -4px", kNegativeLength},
      {"padding-left: -4px", kNegativeLength},
      {"border-radius: -4px", kNegativeLength},
      {"border-width: -2px", kNegativeLength},
      {"border: -2px solid red", kNegativeLength},
      {"gap: -4px", kNegativeLength},
      {"row-gap: -4px", kNegativeLength},
      {"column-gap: -4px", kNegativeLength},
      {"flex-basis: -10px", kNegativeLength},
      // font-size は「通ると送り 0 の空の PNG になる」筆頭（#19 / #26）
      {"font-size: -16px", kNegativeLength},
      {"font-size: -1em", kNegativeLength},
      {"line-height: -16px", kNegativeLength},
      {"line-height: -1em", kNegativeLength},
      // `%`（width と flex-basis だけが `%` を取れるので、負の `%` もこの 2 つだけ）
      {"width: -10%", kNegativePercent},
      {"flex-basis: -10%", kNegativePercent},
      // 単位なしの数値
      {"flex-grow: -1", kNonNegativeNumber},
      {"flex-shrink: -1", kNonNegativeNumber},
      {"flex: -1", kNonNegativeNumber},
      {"flex: 1 -1", kNonNegativeNumber},
      {"line-height: -1.5", "`line-height` must not be negative"},
      {"font-weight: -400", "supported: normal (400), bold (700), or 100..900"},
  });
}

// `<img>` の寸法属性は CSS を通らない別経路なので、同じ表に並べて見る。
TEST(StyleError, NegativeImgSizeAttributesAreRejectedWithTheAttributeLocation) {
  for (const std::string_view name : {"width", "height"}) {
    SCOPED_TRACE(name);
    const SourceLocation attribute{.offset = 17, .line = 2, .column = 11};
    const Outcome outcome = collect(
        test_root(test_element("img", {test_attr("src", "x"), test_attr(name, "-5", attribute)})));
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedValue)
        << outcome.errors.front().message;
    EXPECT_EQ(outcome.errors.front().location.value_or(SourceLocation{}), attribute);
  }
}

// 表の「負が有効」側。ここが黙って 0 に丸められたりエラーになったりしないことを、
// **計算値の数値まで**見て固定する（CSS Box 3 §4: margin は負を取れる。
// CSS Text 3 §8.2: letter-spacing も負を取れる）。
TEST(StyleError, MarginAndLetterSpacingAcceptNegativeLengths) {
  const auto ok = [](std::string_view css) {
    const Result<ComputedStyle> style = inline_style(css);
    EXPECT_TRUE(style.has_value()) << css << ": " << (style ? "" : style.error().message);
    return style.value_or(ComputedStyle{});
  };
  EXPECT_EQ(ok("margin: -4px").margin.top, Dimension::px(-4));
  EXPECT_EQ(ok("margin: -4px").margin.left, Dimension::px(-4));
  EXPECT_EQ(ok("margin-left: -4px").margin.left, Dimension::px(-4));
  // em の基準は自分の font-size（既定の 16px）
  EXPECT_EQ(ok("margin: -1em").margin.top, Dimension::px(-16));
  EXPECT_FLOAT_EQ(ok("letter-spacing: -2px").letter_spacing, -2.0F);
  EXPECT_FLOAT_EQ(ok("letter-spacing: -2em").letter_spacing, -32.0F);
}

// `font-size: 0` は CSS Fonts 4 §2.5 の `<length-percentage [0,∞]>` で**有効**。
// 送りが 0 になるだけなので、エラーにも警告にもしない（負との境目はちょうどここ）。
TEST(StyleError, ZeroFontSizeIsValidAndMakesEverythingZero) {
  const Result<ComputedStyle> zero = inline_style("font-size: 0");
  ASSERT_TRUE(zero.has_value()) << (zero ? "" : zero.error().message);
  EXPECT_FLOAT_EQ(zero->font_size, 0.0F);

  // 組み合わせ: 0 を基準に em が解決される側もすべて 0 になる
  const Result<ComputedStyle> nested =
      inline_style("font-size: 0; padding: 2em; line-height: 1.5; letter-spacing: -0.05em");
  ASSERT_TRUE(nested.has_value()) << (nested ? "" : nested.error().message);
  EXPECT_FLOAT_EQ(nested->font_size, 0.0F);
  EXPECT_EQ(nested->padding, (Edges<float>{0, 0, 0, 0}));
  EXPECT_FLOAT_EQ(nested->letter_spacing, 0.0F);
  EXPECT_EQ(nested->line_height, (LineHeight{LineHeight::Kind::Number, 1.5F}));
}

// `-0` は `0` と等しいので CSS 上は有効。ただし**ダンプに `-0` を漏らさない**
// （同じ入力から同じバイト列、という約束の入口。DESIGN.md §3-5）。
TEST(StyleError, NegativeZeroIsAcceptedAndNeverPrintedAsMinusZero) {
  for (const std::string_view css :
       {"font-size: -0px", "font-size: -0", "padding: -0", "width: -0px", "margin-left: -0em",
        "letter-spacing: -0em", "font-size: 0; letter-spacing: -0.05em",
        "font-size: 0; padding: 2em; line-height: 1.5; letter-spacing: -0.05em"}) {
    SCOPED_TRACE(css);
    const html::Node tree = test_root(test_element("div", {test_attr("style", css)}));
    const Result<StyledNode> styled = resolve_for_test(tree);
    ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
    // プロパティ名の `-` の後ろは必ず英字なので、`-0` が出るのは数値だけ
    EXPECT_EQ(dump_json(*styled).find("-0"), std::string::npos) << dump_json(*styled);
  }
}

TEST(StyleError, ShorthandArity) {
  expect_errors({
      {"margin: 1px 2px 3px 4px 5px", ErrorKind::UnsupportedValue},
      {"padding: 1px 2px 3px 4px 5px", ErrorKind::UnsupportedValue},
      {"gap: 1px 2px 3px", ErrorKind::UnsupportedValue},
      {"flex: 1 2 3px 4px", ErrorKind::UnsupportedValue},
      {"border-radius: 4px 8px", ErrorKind::UnsupportedValue},
      {"border-radius: 50% / 20%", ErrorKind::UnsupportedValue},
      {"width: 10px 20px", ErrorKind::UnsupportedValue},
  });
}

TEST(StyleError, UnsetAndRevertAreNotSupported) {
  expect_errors({
      {"color: unset", ErrorKind::UnsupportedValue},
      {"color: revert", ErrorKind::UnsupportedValue},
      {"margin: unset", ErrorKind::UnsupportedValue},
  });
}

// ---- 構文エラー -----------------------------------------------------------------

TEST(StyleError, SyntaxErrorsInDeclarations) {
  expect_errors({
      {"color red", ErrorKind::CssParse},  // コロンなし
      {"color: ", ErrorKind::CssParse},    // 値なし
      {"color:", ErrorKind::CssParse},     // 値なし
      {": red", ErrorKind::CssParse},      // プロパティ名なし
      {"color: /* 閉じていない", ErrorKind::CssParse},
      {R"(font-family: "unterminated)", ErrorKind::CssParse},
      {"color: rgb(1, 2, 3", ErrorKind::CssParse},  // 閉じていない `(`
      {"color: red !important", ErrorKind::CssParse},
      {"color: red}", ErrorKind::CssParse},  // style 属性に `}`
  });
}

TEST(StyleError, SyntaxErrorsInStylesheets) {
  expect_sheet_errors({
      {"div { color: red", ErrorKind::CssParse},   // 閉じていない `{`
      {"div color: red }", ErrorKind::CssParse},   // `{` がない
      {"div { color red }", ErrorKind::CssParse},  // コロンなし
      {"div { /* 閉じていない }", ErrorKind::CssParse},
      {"} div { color: red }", ErrorKind::CssParse},  // いきなり `}`
      {"div { color: red !important }", ErrorKind::CssParse},
  });
}

TEST(StyleError, UnsupportedSelectors) {
  expect_sheet_errors({
      {"div p { color: red }", ErrorKind::CssParse},      // 子孫結合子
      {"div > p { color: red }", ErrorKind::CssParse},    // 子結合子
      {"div + p { color: red }", ErrorKind::CssParse},    // 隣接
      {"div ~ p { color: red }", ErrorKind::CssParse},    // 後続
      {"div:hover { color: red }", ErrorKind::CssParse},  // 擬似クラス
      {"div::before { color: red }", ErrorKind::CssParse},
      {"div[data-x] { color: red }", ErrorKind::CssParse},
      {"div, { color: red }", ErrorKind::CssParse},  // 空のセレクタ
      {". { color: red }", ErrorKind::CssParse},     // クラス名がない
      {"#a#b { color: red }", ErrorKind::CssParse},
  });
}

TEST(StyleError, SelectorErrorMessagesExplainWhy) {
  const Outcome descendant = collect_sheet("div p { color: red }");
  ASSERT_EQ(descendant.errors.size(), 1U);
  EXPECT_NE(descendant.errors.front().message.find("combinator"), std::string::npos)
      << descendant.errors.front().message;
  const Outcome pseudo = collect_sheet("a:hover { color: red }");
  ASSERT_EQ(pseudo.errors.size(), 1U);
  EXPECT_NE(pseudo.errors.front().message.find("pseudo"), std::string::npos)
      << pseudo.errors.front().message;
}

TEST(StyleError, AtRulesAreNotSupported) {
  expect_sheet_errors({
      {"@media screen { div { color: red } }", ErrorKind::CssParse},
      {"@import url(x.css);", ErrorKind::CssParse},
      {"@font-face { font-family: x }", ErrorKind::CssParse},
  });
  const Outcome outcome = collect_sheet("@media screen { div { color: red } }");
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_NE(outcome.errors.front().message.find("at-rule"), std::string::npos)
      << outcome.errors.front().message;
}

// ---- エラーの位置 ---------------------------------------------------------------

TEST(StyleError, InlineStyleErrorsPointAtTheAttribute) {
  const SourceLocation attribute{.offset = 42, .line = 3, .column = 6};
  const Outcome outcome = collect_inline("float: left", attribute);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().location.value_or(SourceLocation{}), attribute);
}

TEST(StyleError, StyleElementErrorsAddTheCssLineAndColumn) {
  // <style> のテキストは 5 行目 8 桁目から始まる、という想定
  const SourceLocation base{.offset = 100, .line = 5, .column = 8};
  const Outcome outcome = collect(test_root(test_style_element("div {\n  float: left;\n}", base)));
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedProperty);
  // CSS の 2 行目 3 桁目 → 入力の 6 行目 3 桁目（2 行目以降は桁がそのまま）
  const SourceLocation location = outcome.errors.front().location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 6U);
  EXPECT_EQ(location.column, 3U);
  EXPECT_EQ(location.offset, 100U + 8U);
}

TEST(StyleError, FirstLineOfCssKeepsTheAttributeColumnOffset) {
  const SourceLocation base{.offset = 10, .line = 2, .column = 4};
  const Outcome outcome = collect(test_root(test_style_element("div { float: left }", base)));
  ASSERT_EQ(outcome.errors.size(), 1U);
  const SourceLocation location = outcome.errors.front().location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 2U);
  EXPECT_EQ(location.column, 4U + 6U);  // `float` は CSS の 7 桁目
}

// ---- A46「一度に全部」: 集めて続行する ---------------------------------------------

// 1 つの `<style>` と複数の `style` 属性から、対応外が 1 回で全部集まる。
TEST(StyleError, AllProblemsAreCollectedInOnePass) {
  constexpr SourceLocation kSheet{.offset = 10, .line = 2, .column = 1};
  constexpr SourceLocation kFirst{.offset = 200, .line = 8, .column = 6};
  constexpr SourceLocation kSecond{.offset = 300, .line = 9, .column = 7};
  const html::Node tree =
      test_root(test_style_element("p { float: left; box-shadow: 0 0 4px black }\n"
                                   "div p { color: red }",
                                   kSheet),
                test_element("div", {test_attr("style", "position: absolute", kFirst)}),
                test_element("span", {test_attr("style", "padding: 4px", kSecond)}));
  const Outcome outcome = collect(tree);
  EXPECT_EQ(outcome.kinds(), (std::vector<ErrorKind>{
                                 ErrorKind::UnsupportedProperty,  // float（<style>）
                                 ErrorKind::UnsupportedProperty,  // box-shadow（<style>）
                                 ErrorKind::CssParse,             // div p（子孫結合子）
                                 ErrorKind::UnsupportedProperty,  // position（1 つ目の属性）
                                 ErrorKind::UnsupportedLayout,    // span への padding
                             }));
  EXPECT_FALSE(outcome.truncated);
  // 位置も全部付いている（`style` 属性の診断は属性を指す）
  for (const RenderError& error : outcome.errors) {
    EXPECT_TRUE(error.location.has_value()) << error.message;
  }
  EXPECT_EQ(outcome.errors.at(3).location.value_or(SourceLocation{}), kFirst);
  EXPECT_EQ(outcome.errors.at(4).location.value_or(SourceLocation{}), kSecond);
}

// 壊れた宣言は「書かれなかった」扱いで捨て、**後ろの宣言は効く**（計算値で確認する）。
TEST(StyleError, BrokenDeclarationsAreSkippedAndLaterOnesStillApply) {
  const Outcome inline_case =
      collect_inline("color: rgb(1, 2); background-color: blue; padding: 4px");
  ASSERT_EQ(inline_case.errors.size(), 1U);
  EXPECT_EQ(inline_case.errors.front().kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(inline_case.style.background_color, (Color{0, 0, 255, 255}));
  EXPECT_EQ(inline_case.style.padding, (Edges<float>{4, 4, 4, 4}));
  EXPECT_EQ(inline_case.style.color, ComputedStyle{}.color);  // 捨てた宣言は効かない

  // `!important` のように値の途中で落ちるものでも、次の宣言から読み直す
  const Outcome important = collect_inline("color: red !important; background-color: blue");
  ASSERT_EQ(important.errors.size(), 1U);
  EXPECT_EQ(important.errors.front().kind, ErrorKind::CssParse);
  EXPECT_EQ(important.style.background_color, (Color{0, 0, 255, 255}));

  // ショートハンドが途中まで展開されても残さない
  const Outcome shorthand = collect_inline("margin: 1px 2px 3px 4px 5px; padding: 8px");
  ASSERT_EQ(shorthand.errors.size(), 1U);
  EXPECT_EQ(shorthand.style.margin.top, Dimension::px(0));
  EXPECT_EQ(shorthand.style.padding, (Edges<float>{8, 8, 8, 8}));
}

// `<style>` の中でも同じ。宣言の単位・規則の単位で読み飛ばし、続きの規則は効く。
TEST(StyleError, StylesheetsRecoverPerDeclarationAndPerRule) {
  const Outcome per_declaration = collect_sheet("div { float: left; color: blue }");
  ASSERT_EQ(per_declaration.errors.size(), 1U);
  EXPECT_EQ(per_declaration.style.color, (Color{0, 0, 255, 255}));

  // セレクタが読めなければ規則ごと捨て、次の規則から読み直す
  const Outcome per_rule = collect_sheet("div p { color: red } div { color: blue }");
  ASSERT_EQ(per_rule.errors.size(), 1U);
  EXPECT_EQ(per_rule.errors.front().kind, ErrorKind::CssParse);
  EXPECT_EQ(per_rule.style.color, (Color{0, 0, 255, 255}));

  // `@` 規則はブロックごと飛ばす（中の宣言を二重に報告しない）
  const Outcome at_rule = collect_sheet("@media screen { div { color: red } } div { color: blue }");
  ASSERT_EQ(at_rule.errors.size(), 1U);
  EXPECT_EQ(at_rule.style.color, (Color{0, 0, 255, 255}));

  // 余分な `}` は 1 つ捨てて続ける
  const Outcome stray = collect_sheet("} div { color: blue }");
  ASSERT_EQ(stray.errors.size(), 1U);
  EXPECT_EQ(stray.style.color, (Color{0, 0, 255, 255}));
}

// 同じ入力からは同じ診断が同じ順で出る（DESIGN.md §3-5）。
TEST(StyleError, CollectedDiagnosticsAreDeterministic) {
  const auto run = [] {
    return collect(test_root(test_style_element("p { float: left; overflow: hidden }"),
                             test_element("div", {test_attr("style", "position: fixed")}),
                             test_element("span", {test_attr("style", "margin: 2px")})))
        .errors;
  };
  EXPECT_EQ(run(), run());
  EXPECT_EQ(run().size(), 4U);
}

// 上限（RenderLimits::max_diagnostics）に達したら記録をやめ、truncated を立てて解析は続ける。
TEST(StyleError, DiagnosticsAreTruncatedAtTheLimit) {
  const html::Node tree =
      test_root(test_element("div", {test_attr("style",
                                               "float: left; position: absolute; overflow: hidden; "
                                               "opacity: 0.5; transform: none")}));
  const Outcome all = collect(tree);
  EXPECT_EQ(all.errors.size(), 5U);
  EXPECT_FALSE(all.truncated);

  const Outcome limited = collect(tree, 2);
  EXPECT_EQ(limited.errors.size(), 2U);
  EXPECT_TRUE(limited.truncated);
}

// 致命（LimitExceeded）はその場で止める（unexpected のまま）。それまでに集めた診断は残る。
TEST(StyleError, LimitExceededStopsImmediatelyAndKeepsWhatWasCollected) {
  const html::Node tree = test_root(test_element("div", {test_attr("style", "float: left")}),
                                    test_element("div", {test_attr("style", "padding: 1e38em")}));
  const Resolved resolved = resolve_collect(tree);
  ASSERT_FALSE(resolved.tree.has_value()) << "上限の超過は止める";
  EXPECT_EQ(resolved.tree.error().kind, ErrorKind::LimitExceeded);
  ASSERT_EQ(resolved.errors.size(), 1U);
  EXPECT_EQ(resolved.errors.front().kind, ErrorKind::UnsupportedProperty);
}

// ---- inline 要素への箱の指定 -----------------------------------------------------

TEST(StyleError, BoxPropertiesOnInlineElements) {
  const std::vector<std::string_view> properties = {
      "width: 10px",           "height: 10px",      "margin: 1px",        "padding: 1px",
      "border: 1px solid red", "border-width: 1px", "border-radius: 2px",
  };
  for (const std::string_view property : properties) {
    SCOPED_TRACE(property);
    const Outcome outcome =
        collect(test_root(test_element("span", {test_attr("style", property)})));
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout)
        << outcome.errors.front().message;
  }
}

TEST(StyleError, InlineBoxCheckOnlyLooksAtAuthorDeclarations) {
  // UA が p に付ける margin は作者の宣言ではないので、display: inline にしても通る
  const html::Node tree = test_root(test_element("p", {test_attr("style", "display: inline")}));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->children.front().style.display, Display::Inline);
  // 継承も作者の宣言ではない
  const html::Node inherited = test_root(test_parent(
      "div", {test_attr("style", "color: red; border: 1px solid blue")}, test_element("span")));
  EXPECT_TRUE(resolve_for_test(inherited).has_value());
}

TEST(StyleError, ImgIsExemptFromTheInlineBoxCheck) {
  const html::Node tree = test_root(test_element(
      "img", {test_attr("src", "logo"), test_attr("style", "width: 10px; margin: 2px")}));
  EXPECT_TRUE(resolve_for_test(tree).has_value());
}

TEST(StyleError, BoxPropertyOnInlineIsFineWhenDisplayIsChanged) {
  const html::Node tree =
      test_root(test_element("span", {test_attr("style", "display: block; width: 10px")}));
  EXPECT_TRUE(resolve_for_test(tree).has_value());
}

// ---- A53: flex コンテナの直接の子は block 化されるので箱プロパティを取れる -------------
//
// 再現（`docs/benchmark/results_a46_2026-09-23.md` §2 の pill / タグ / バッジ 8 件）:
// `display: flex` の親の中の `<span>` に padding を付けると `unsupported-layout` になっていた。

TEST(StyleError, BoxPropertiesOnFlexChildrenAreAccepted) {
  const html::Node tree = test_root(test_parent(
      "div", {test_attr("style", "display: flex; gap: 8px")},
      test_parent("span", {test_attr("style", "padding: 4px 8px; border-radius: 8px")},
                  test_text("政策")),
      test_parent("span", {test_attr("style", "display: inline; margin: 2px; width: 40px")},
                  test_text("AI"))));
  const Outcome outcome = collect(tree);
  EXPECT_TRUE(outcome.errors.empty())
      << (outcome.errors.empty() ? "" : outcome.errors.front().message);
}

// 文中の inline（flex の子でない span）は今までどおり弾く。
TEST(StyleError, BoxPropertiesOnInlineElementsInRunningTextAreStillRejected) {
  const html::Node tree = test_root(
      test_parent("p", {}, test_text("文中の "),
                  test_parent("span", {test_attr("style", "padding: 4px")}, test_text("語")),
                  test_text(" のまわり")));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout);
}

// 孫は block 化されないので、flex の中でも文中の span は弾く。
TEST(StyleError, BoxPropertiesOnFlexGrandchildrenAreStillRejected) {
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "display: flex")},
                  test_parent("div", {}, test_text("前 "),
                              test_element("span", {test_attr("style", "padding: 4px")}))));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout);
}

// 例外の `<ruby>` は inline のままなので、箱プロパティは flex の中でも弾く。
TEST(StyleError, BoxPropertiesOnRubyInAFlexContainerAreStillRejected) {
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "display: flex")},
                  test_parent("ruby", {test_attr("style", "padding: 4px")}, test_text("漢"),
                              test_parent("rt", {}, test_text("かん")))));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout);
}

// `<img>` は前から例外（置換要素）。flex の中でも箱プロパティを取れる。
TEST(StyleError, ImgInAFlexContainerKeepsTakingBoxProperties) {
  const html::Node tree =
      test_root(test_parent("div", {test_attr("style", "display: flex")},
                            test_element("img", {test_attr("src", "x"),
                                                 test_attr("style", "width: 24px; margin: 2px")})));
  EXPECT_TRUE(resolve_for_test(tree).has_value());
}

// ---- 計算値の診断の重複（A48 の追記）-----------------------------------------------
//
// 1 つの規則に複数の要素が一致すると、計算値の検査が要素ごとに同じ診断を出していた
// （V2-fix の case03 / 06 / 09 で `unsupported-layout` が同一位置・同一文面で 2 件）。

TEST(StyleError, ComputedDiagnosticsFromOneRuleAreReportedOnce) {
  const html::Node tree =
      test_root(test_style_element(".tag { border: 1px solid #000; padding: 3px 12px }"),
                test_parent("p", {}, test_element("span", {test_attr("class", "tag")}),
                            test_element("span", {test_attr("class", "tag")})));
  const Outcome outcome = collect(tree);
  // 宣言ごとに 1 件（A55）で、**要素ごとには増えない**（重複除去が無ければ 2 要素 x 2 宣言 = 4 件）
  ASSERT_EQ(outcome.errors.size(), 2U) << "同じ (kind, 位置, 文面) は 1 件だけ";
  EXPECT_EQ(outcome.errors.at(0).kind, ErrorKind::UnsupportedLayout);
  EXPECT_EQ(outcome.errors.at(1).kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(outcome.errors.at(0).location.value_or(SourceLocation{}),
            outcome.errors.at(1).location.value_or(SourceLocation{}));
}

TEST(StyleError, ComputedDiagnosticsFromDifferentRulesAreBothReported) {
  const html::Node tree =
      test_root(test_style_element(".a { padding: 3px } .b { padding: 3px }"),
                test_parent("p", {}, test_element("span", {test_attr("class", "a")}),
                            test_element("span", {test_attr("class", "b")})));
  const Outcome outcome = collect(tree);
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedLayout, ErrorKind::UnsupportedLayout}));
  EXPECT_NE(outcome.errors.at(0).location.value_or(SourceLocation{}),
            outcome.errors.at(1).location.value_or(SourceLocation{}));
}

// 同じ宣言でも `style` 属性は要素ごとに位置が違うので、2 件とも出る。
TEST(StyleError, TheSameDeclarationInSeparateInlineStylesIsReportedTwice) {
  constexpr SourceLocation kFirst{.offset = 20, .line = 2, .column = 6};
  constexpr SourceLocation kSecond{.offset = 60, .line = 3, .column = 6};
  const html::Node tree = test_root(
      test_parent("p", {}, test_element("span", {test_attr("style", "padding: 4px", kFirst)}),
                  test_element("span", {test_attr("style", "padding: 4px", kSecond)})));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 2U);
  EXPECT_EQ(outcome.errors.at(0).location.value_or(SourceLocation{}), kFirst);
  EXPECT_EQ(outcome.errors.at(1).location.value_or(SourceLocation{}), kSecond);
}

// 重複を落としても、規則の単位の診断（対応外のプロパティ）は今までどおり 1 件のまま。
TEST(StyleError, DeduplicationDoesNotTouchDeclarationLevelDiagnostics) {
  const html::Node tree =
      test_root(test_style_element(".tag { float: left }"),
                test_parent("p", {}, test_element("span", {test_attr("class", "tag")}),
                            test_element("span", {test_attr("class", "tag")})));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedProperty);
}

// ---- writing-mode の規則（A1）----------------------------------------------------

TEST(StyleError, WritingModeOnTopLevelElementIsAdoptedByTheRoot) {
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "writing-mode: vertical-rl")}, test_element("div")));
  const Result<StyledNode> styled = resolve_for_test(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);
  EXPECT_EQ(styled->style.writing_mode, WritingMode::VerticalRl);
  EXPECT_EQ(styled->children.at(0).style.writing_mode, WritingMode::VerticalRl);
  EXPECT_EQ(styled->children.at(0).children.at(0).style.writing_mode, WritingMode::VerticalRl);
}

TEST(StyleError, TopLevelElementsMustAgreeOnWritingMode) {
  constexpr SourceLocation kSecond{.offset = 80, .line = 4, .column = 6};
  const html::Node tree =
      test_root(test_element("div", {test_attr("style", "writing-mode: vertical-rl")}),
                test_element("div", {test_attr("style", "writing-mode: horizontal-tb", kSecond)}));
  const Outcome outcome = collect(tree);
  ASSERT_FALSE(outcome.errors.empty());
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout);
  EXPECT_EQ(outcome.errors.front().location.value_or(SourceLocation{}), kSecond);
}

TEST(StyleError, WritingModeCannotChangeDeeperInTheTree) {
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "writing-mode: vertical-rl")},
                  test_element("div", {test_attr("style", "writing-mode: horizontal-tb")})));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout);
}

TEST(StyleError, RepeatingTheDocumentWritingModeDeeperIsAllowed) {
  // 値が同じなら「変更」ではない
  const html::Node tree = test_root(
      test_parent("div", {test_attr("style", "writing-mode: vertical-rl")},
                  test_element("div", {test_attr("style", "writing-mode: vertical-rl")})));
  EXPECT_TRUE(resolve_for_test(tree).has_value());
}

TEST(StyleError, WritingModeDeeperThanTopLevelIsRejected) {
  const html::Node tree = test_root(test_parent(
      "div", {}, test_element("div", {test_attr("style", "writing-mode: vertical-rl")})));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedLayout);
}

// トップレベルの先読み（文書の writing-mode を決める走査）で、同じ診断が 2 件にならない。
TEST(StyleError, TopLevelElementsAreNotDiagnosedTwice) {
  const html::Node tree =
      test_root(test_element("div", {test_attr("style", "float: left")}),
                test_element("div", {test_attr("style", "writing-mode: vertical-rl")}));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U) << "先読みと本番で 2 件になっていないか";
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedProperty);
}

// ---- img の属性 -------------------------------------------------------------------

TEST(StyleError, ImgRequiresSrc) {
  constexpr SourceLocation kElement{.offset = 5, .line = 1, .column = 6};
  const Outcome outcome = collect(test_root(test_element("img", {}, kElement)));
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedValue);
  EXPECT_EQ(outcome.errors.front().location.value_or(SourceLocation{}), kElement);
  EXPECT_NE(outcome.errors.front().message.find("src"), std::string::npos)
      << outcome.errors.front().message;
}

TEST(StyleError, ImgSizeAttributesMustBeNonNegativeNumbers) {
  const std::vector<std::string_view> bad = {"-1", "10px", "abc", "", "50%"};
  for (const std::string_view value : bad) {
    SCOPED_TRACE(value);
    const Outcome outcome =
        collect(test_root(test_element("img", {test_attr("src", "x"), test_attr("width", value)})));
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedValue)
        << outcome.errors.front().message;
  }
}

// width と height が両方おかしければ 2 件とも出る（A46「一度に全部」）。
TEST(StyleError, BothImgSizeAttributesAreReported) {
  const Outcome outcome = collect(test_root(test_element(
      "img", {test_attr("src", "x"), test_attr("width", "abc"), test_attr("height", "50%")})));
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedValue, ErrorKind::UnsupportedValue}));
}

// ---- 落とされる部分木の中でも黙らない ---------------------------------------------

TEST(StyleError, DeclarationsInsideDisplayNoneSubtreesAreStillChecked) {
  const html::Node tree =
      test_root(test_parent("div", {test_attr("style", "display: none")},
                            test_element("div", {test_attr("style", "float: left")})));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedProperty);
}

// 一致しなかったクラス / ID は「正常な選択の結果」なので報告しない（A55）。
// それでも中の宣言は読むので、対応外のプロパティは出る。
TEST(StyleError, RulesThatMatchNothingAreStillChecked) {
  for (const std::string_view css :
       {".nosuchclass { float: left }", "#nosuchid { float: left }", "* { float: left }"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_sheet(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedProperty);
  }
}

// ---- A55 (a): inline への箱プロパティは、作者が書いた宣言を全部報告する ----------------
//
// 再現（`docs/benchmark/results_a53_2026-09-24.md` §2 の case04 / case08）:
// `record_author_declaration()` が最初の 1 つしか覚えないので、border を直すと次は
// border-radius、その次は padding と 1 往復ずつ増えていた。

// 箱プロパティの診断が名指ししたプロパティ名（message の最初の `…` から拾う）。
std::vector<std::string> box_property_names(const Outcome& outcome) {
  std::vector<std::string> out;
  for (const RenderError& error : outcome.errors) {
    if (error.kind != ErrorKind::UnsupportedLayout) {
      continue;
    }
    const std::size_t open = error.message.find('`');
    if (open == std::string::npos) {
      continue;
    }
    const std::size_t close = error.message.find('`', open + 1);
    if (close == std::string::npos) {
      continue;
    }
    out.push_back(error.message.substr(open + 1, close - open - 1));
  }
  std::ranges::sort(out);
  return out;
}

// 1 宣言 = 1 件。ショートハンドは先頭の longhand の名前で 1 件だけ出す。
TEST(StyleError, EveryBoxDeclarationOnAnInlineElementIsReported) {
  constexpr SourceLocation kAttribute{.offset = 30, .line = 2, .column = 9};
  const html::Node tree = test_root(test_parent(
      "p", {}, test_text("文中の "),
      test_parent("span",
                  {test_attr("style",
                             "border: 1px solid #999; border-radius: 4px; padding: 2px 6px; "
                             "margin: 0 2px",
                             kAttribute)},
                  test_text("語"))));
  const Outcome outcome = collect(tree);
  EXPECT_EQ(box_property_names(outcome), (std::vector<std::string>{"border-radius", "border-width",
                                                                   "margin-top", "padding-top"}));
  ASSERT_EQ(outcome.errors.size(), 4U) << "4 つの宣言が 1 度に全部出る";
  for (const RenderError& error : outcome.errors) {
    EXPECT_EQ(error.kind, ErrorKind::UnsupportedLayout) << error.message;
    EXPECT_EQ(error.location.value_or(SourceLocation{}), kAttribute) << error.message;
    EXPECT_FALSE(error.hint.empty()) << error.message;
  }
}

// longhand を別々に書いたときも 1 つずつ出る（同じ組でもまとめない）。
TEST(StyleError, SeparateLonghandBoxDeclarationsAreAllReported) {
  const Outcome outcome = collect(
      test_root(test_element("span", {test_attr("style", "padding-top: 1px; padding-left: 2px")})));
  EXPECT_EQ(box_property_names(outcome), (std::vector<std::string>{"padding-left", "padding-top"}));
}

// `<style>` の規則でも全部出る。位置はそれぞれの宣言を指す。
TEST(StyleError, EveryBoxDeclarationFromAStylesheetIsReported) {
  const html::Node tree =
      test_root(test_style_element(".tag { border: 1px solid #999; padding: 2px 6px }"),
                test_parent("p", {}, test_element("span", {test_attr("class", "tag")})));
  const Outcome outcome = collect(tree);
  EXPECT_EQ(box_property_names(outcome), (std::vector<std::string>{"border-width", "padding-top"}));
  ASSERT_EQ(outcome.errors.size(), 2U);
  EXPECT_NE(outcome.errors.at(0).location.value_or(SourceLocation{}),
            outcome.errors.at(1).location.value_or(SourceLocation{}));
}

// flex の直接の子は block 化されるので、何件書いても診断は 0（A53）。
TEST(StyleError, BoxDeclarationsOnFlexChildrenStayQuiet) {
  const html::Node tree =
      test_root(test_parent("div", {test_attr("style", "display: flex")},
                            test_parent("span",
                                        {test_attr("style",
                                                   "border: 1px solid #999; border-radius: 4px; "
                                                   "padding: 2px 6px; margin: 0 2px")},
                                        test_text("政策"))));
  const Outcome outcome = collect(tree);
  EXPECT_TRUE(outcome.errors.empty())
      << (outcome.errors.empty() ? "" : outcome.errors.front().message);
}

// UA 由来は数えないまま（`<p>` の margin を inline にしても黙っている）。
TEST(StyleError, UserAgentBoxDeclarationsAreStillNotCounted) {
  const Outcome outcome =
      collect(test_root(test_element("p", {test_attr("style", "display: inline")})));
  EXPECT_TRUE(outcome.errors.empty())
      << (outcome.errors.empty() ? "" : outcome.errors.front().message);
}

// ---- A55 (b): 捨てる規則の宣言も、構文の診断だけは出す -------------------------------
//
// 再現（同 §2 の case01 / 03 / 06 / 09）: セレクタが読めない規則を丸ごと捨てていたので、
// 中の対応外プロパティ・値はセレクタを直した次の往復で初めて出ていた。

TEST(StyleError, DeclarationsOfARuleWithAnUnreadableSelectorAreReported) {
  const Outcome outcome = collect_sheet(".card > p { float: left; color: #000 }");
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::CssParse, ErrorKind::UnsupportedProperty}));
  ASSERT_EQ(outcome.errors.size(), 2U);
  EXPECT_FALSE(outcome.errors.at(1).hint.empty()) << "hint は通常どおり付く";
  // 読めた宣言（color）は診断も出ないし、適用もされない
  EXPECT_EQ(outcome.style.color, ComputedStyle{}.color);
}

// 「報告だけ」なので、その規則を適用した前提の計算値の検査は出さない。
TEST(StyleError, DroppedRulesDoNotProduceComputedValueDiagnostics) {
  const html::Node tree = test_root(test_style_element("p > span { padding: 4px }"),
                                    test_parent("p", {}, test_element("span")));
  const Outcome outcome = collect(tree);
  ASSERT_EQ(outcome.errors.size(), 1U) << "セレクタの 1 件だけ（unsupported-layout は出さない）";
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::CssParse);
}

// ブロックが安全に読めないときは今までどおり規則ごと捨てる（推測して報告しない）。
TEST(StyleError, BrokenBlocksOfDroppedRulesAreStillSkippedWhole) {
  for (const std::string_view css : {
           "div > p { float: left",                 // 閉じていない
           "div > p { @media x { float: left } }",  // 入れ子のブロック
           "div > p { /* 閉じていないコメント float: left }",
       }) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_sheet(css);
    ASSERT_EQ(outcome.errors.size(), 1U) << "セレクタの 1 件だけ";
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::CssParse);
  }
}

// 宣言ブロックの無い規則（`;` で終わる）は今までどおり。
TEST(StyleError, DroppedRulesWithoutABlockAreUnchanged) {
  const Outcome outcome = collect_sheet("div > p;\ndiv { color: blue }");
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::CssParse);
  EXPECT_EQ(outcome.style.color, (Color{0, 0, 255, 255}));  // 次の規則は効く
}

// 捨てた規則の宣言を報告しても、後ろの規則は今までどおり効く。
TEST(StyleError, RulesAfterADroppedRuleStillApply) {
  const Outcome outcome = collect_sheet(".card > p { float: left } div { color: blue }");
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::CssParse, ErrorKind::UnsupportedProperty}));
  EXPECT_EQ(outcome.style.color, (Color{0, 0, 255, 255}));
}

// ---- A55 (c): 対応外のタグを名指しするセレクタは決して一致しない ----------------------
//
// 再現（同 §3）: `<body>` を外すよう言われて外すと、`body { … }` が誰にも当たらず
// 背景・余白・本文色が黙って消えていた（a_plain 10 件中 7 件に同型の HTML）。

TEST(StyleError, SelectorsNamingUnsupportedTagsAreReported) {
  const Outcome outcome = collect_sheet("body { background: #fff }");
  ASSERT_EQ(outcome.errors.size(), 1U);
  const RenderError& error = outcome.errors.front();
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedTag);
  EXPECT_EQ(to_string(error.kind), "unsupported-tag");
  EXPECT_NE(error.message.find("body"), std::string::npos) << error.message;
  EXPECT_FALSE(error.hint.empty()) << "外側の div に移す案を添える";
  EXPECT_NE(error.hint.find("div"), std::string::npos) << error.hint;
}

// 位置はセレクタそのもの（規則ごとに 1 件）。
TEST(StyleError, UnsupportedTagSelectorPointsAtTheSelector) {
  constexpr SourceLocation kBase{.offset = 100, .line = 5, .column = 1};
  const Outcome outcome =
      collect(test_root(test_style_element("div { color: red }\nbody { color: blue }", kBase)));
  ASSERT_EQ(outcome.errors.size(), 1U);
  const SourceLocation location = outcome.errors.front().location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 6U);
  EXPECT_EQ(location.column, 1U);
  EXPECT_EQ(location.offset, 100U + 19U);
}

// その規則の宣言は (b) と同じく構文の診断だけ出し、適用しない。
TEST(StyleError, UnsupportedTagRulesReportTheirDeclarationsButDoNotApply) {
  const Outcome outcome = collect_sheet("body { color: blue; float: left }");
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedTag, ErrorKind::UnsupportedProperty}));
  EXPECT_EQ(outcome.style.color, ComputedStyle{}.color);
}

// 複合セレクタの中のタグ名も見る。
TEST(StyleError, CompoundSelectorsWithAnUnsupportedTagAreReported) {
  for (const std::string_view css : {"body.dark { color: blue }", "section#top { color: blue }"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_sheet(css);
    ASSERT_EQ(outcome.errors.size(), 1U);
    EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedTag);
  }
}

// カンマ区切りは対応外の部分だけ報告し、残りは通常どおり適用する。
TEST(StyleError, CommaSeparatedSelectorsReportOnlyTheUnsupportedPart) {
  const Outcome outcome = collect_sheet("body, div { color: blue }");
  ASSERT_EQ(outcome.errors.size(), 1U);
  EXPECT_EQ(outcome.errors.front().kind, ErrorKind::UnsupportedTag);
  EXPECT_EQ(outcome.style.color, (Color{0, 0, 255, 255})) << "div の分は効く";
}

// 対応外のタグが 2 つあれば 2 件（位置はそれぞれのセレクタ）。要素の数では増えない。
TEST(StyleError, EachUnsupportedTagInACommaListIsReportedOnce) {
  const html::Node tree = test_root(test_style_element("body, html, div { color: blue }"),
                                    test_element("div"), test_element("div"));
  const Outcome outcome = collect(tree);
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedTag, ErrorKind::UnsupportedTag}));
  ASSERT_EQ(outcome.errors.size(), 2U);
  EXPECT_NE(outcome.errors.at(0).location.value_or(SourceLocation{}),
            outcome.errors.at(1).location.value_or(SourceLocation{}));
}

// 対応タグのセレクタは今までどおり黙って当たる。
TEST(StyleError, SupportedTagSelectorsAreNotReported) {
  for (const std::string_view css : {"div { color: blue }", "p { color: blue }",
                                     "span, ruby, rt, rp, img, br, style, h1 { color: blue }"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_sheet(css);
    EXPECT_TRUE(outcome.errors.empty())
        << (outcome.errors.empty() ? "" : outcome.errors.front().message);
  }
}

// タグ名を書かなかった規則（`*` / クラス / ID）は対象外（一致しなくても正常）。
TEST(StyleError, TaglessSelectorsAreNeverReportedAsUnsupportedTags) {
  for (const std::string_view css :
       {"* { color: blue }", ".x { color: blue }", "#y { color: blue }", ".x#y { color: blue }"}) {
    SCOPED_TRACE(css);
    const Outcome outcome = collect_sheet(css);
    EXPECT_TRUE(outcome.errors.empty())
        << (outcome.errors.empty() ? "" : outcome.errors.front().message);
  }
}

// 対応外のタグ名 + 対応外のプロパティは 1 度に全部出る（A46 の「一度に全部」）。
TEST(StyleError, UnsupportedTagAndDeclarationDiagnosticsComeTogether) {
  const Outcome outcome = collect_sheet("nosuchtag { float: left }");
  EXPECT_EQ(outcome.kinds(),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedTag, ErrorKind::UnsupportedProperty}));
}

}  // namespace
}  // namespace shashoku::style
