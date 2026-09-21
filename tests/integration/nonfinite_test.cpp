#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// 計算値が非有限（inf / NaN）になっても警告なしで成功した PNG が返る問題（issue #19）の
// end-to-end の検査。第 1 段階は style の出口の保証（ARCHITECTURE.md A-new）:
//
//   style が出す長さは、font-size を除いてすべて有限で、絶対値が RenderLimits::length_px 以内。
//
// 「成功したなら中間表現に非有限な数値が 1 つもない」を性質として書く。JSON は非有限を
// `null` にするので（core/json_writer.hpp）、ダンプに `null` が出ないことがそのまま
// 「全数値が有限」の検査になる。
//
// **第 2 段階（layout の出口の走査）が入ったら**、kCheckedStages に Box と DisplayList を
// 足すだけで同じ性質が box / display-list にも効くようにしてある。
namespace shashoku::test {
namespace {

// 第 1 段階で「非有限が 1 つもない」を保証できる段。第 2 段階で Box / DisplayList が増える。
constexpr std::array<DumpStage, 1> kCheckedStages = {DumpStage::Style};

RenderOptions viewport(int width, bool vertical = false) {
  RenderOptions options = options_for(width);
  if (vertical) {
    options.viewport_height = 400;  // 縦書きでは高さが要る（§3.8）
  }
  return options;
}

// エラーを期待し、エラーを返す（成功したらその場で落とす）。
RenderError render_failure(std::string_view html, const RenderOptions& options) {
  const auto result = render(html, japanese_fonts(), options);
  if (result) {
    ADD_FAILURE() << "エラーになるはずが成功した: " << html.substr(0, 96);
    return RenderError{};
  }
  return result.error();
}

// ---------------------------------------------------------------------------
// issue #19 の表: `em` の乗算で float をあふれる 11 プロパティ
// ---------------------------------------------------------------------------

struct PropertyCase {
  std::string_view name;
  std::string_view html;
};

const std::vector<PropertyCase>& em_overflow_cases() {
  static const std::vector<PropertyCase> cases = {
      {"padding", R"(<div style="padding:1e38em">A</div>)"},
      {"margin", R"(<div style="margin:1e38em">A</div>)"},
      {"margin-negative", R"(<div style="margin:-1e38em">A</div>)"},
      {"width", R"(<div style="width:1e38em">A</div>)"},
      {"height", R"(<div style="height:1e38em">A</div>)"},
      {"border-width", R"(<div style="border:1e38em solid #000">A</div>)"},
      {"border-radius", R"(<div style="border-radius:1e38em">A</div>)"},
      {"letter-spacing", R"(<div style="letter-spacing:1e38em">A</div>)"},
      {"letter-spacing-negative", R"(<div style="letter-spacing:-1e38em">A</div>)"},
      {"line-height", R"(<div style="line-height:1e38em">A</div>)"},
      {"row-gap", R"(<div style="display:flex;row-gap:1e38em"><div>A</div></div>)"},
      {"column-gap", R"(<div style="display:flex;column-gap:1e38em"><div>A</div></div>)"},
      {"flex-basis", R"(<div style="display:flex"><div style="flex-basis:1e38em">A</div></div>)"},
      // 唯一もともと止まっていたもの。種類と位置が変わっていないことを一緒に押さえる
      {"font-size", R"(<div style="font-size:1e38em">A</div>)"},
  };
  return cases;
}

TEST(NonFiniteLengths, EmOverflowFailsWithALocation) {
  for (const PropertyCase& test : em_overflow_cases()) {
    SCOPED_TRACE(test.name);
    const RenderError error = render_failure(test.html, viewport(300));
    EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
    ASSERT_TRUE(error.location.has_value()) << error.message;
    EXPECT_EQ(error.location.value_or(SourceLocation{}).line, 1U);
  }
}

// 位置は `style` 属性（= その宣言を書いた場所）を指す。要素の先頭ではない。
TEST(NonFiniteLengths, LocationPointsAtTheStyleAttribute) {
  constexpr std::string_view kHtml = R"(<div style="padding-left:1e38em">A</div>)";
  const RenderError error = render_failure(kHtml, viewport(300));
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column,
            static_cast<std::uint32_t>(kHtml.find("style=") + 1))
      << error.message;
}

// font-size だけは A25 の font_size_device_px が止める（種類と位置は従来どおり要素の先頭）。
TEST(NonFiniteLengths, FontSizeKeepsItsOldKindAndLocation) {
  const RenderError error =
      render_failure(R"(<div style="font-size:1e38em">A</div>)", viewport(300));
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 1U) << error.message;
  EXPECT_NE(error.message.find("font-size"), std::string::npos) << error.message;
}

// style では有限だが、以前は layout で非有限になっていたもののうち、第 1 段階で止まる 3 件
// （issue #19 の (a) padding:3e38px、(b) line-height:1e38 の倍率、(d) flex-shrink + width:1e30px。
// (d) は flex の比ではなく `width: 1e30px` が length_px を超えることで止まる）。
TEST(NonFiniteLengths, HugeButFiniteLengthsAreStoppedInStyle) {
  RenderOptions options = viewport(300);
  options.viewport_height = 100;  // 自動高さの入口の検査に当たらないようにする
  for (const std::string_view html :
       {R"(<div style="padding:3e38px">A</div>)", R"(<div style="line-height:1e38">A</div>)",
        R"(<div style="display:flex;width:10px"><div style="flex-shrink:1e38;width:1e30px">)"
        R"(A</div></div>)"}) {
    SCOPED_TRACE(html);
    const RenderError error = render_failure(html, options);
    EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
    EXPECT_NE(error.message.find("RenderLimits::length_px"), std::string::npos) << error.message;
    EXPECT_TRUE(error.location.has_value()) << error.message;
  }
}

// 縦書き・flex の中・ルビの中でも同じ（単独では通るのに組み合わせで落ちる類を潰す）。
TEST(NonFiniteLengths, SameInEveryContext) {
  struct ContextCase {
    std::string_view name;
    std::string_view html;
    bool vertical;
  };
  constexpr std::array<ContextCase, 4> kCases = {{
      {"block", R"(<div style="padding-left:1e38em">あ</div>)", false},
      {"vertical", R"(<div style="writing-mode:vertical-rl;padding-top:1e38em">あ</div>)", true},
      {"flex", R"(<div style="display:flex"><div style="padding:1e38em">あ</div></div>)", false},
      {"ruby", R"(<div><ruby style="letter-spacing:1e38em">東<rt>とう</rt></ruby></div>)", false},
  }};
  for (const ContextCase& test : kCases) {
    SCOPED_TRACE(test.name);
    const RenderError error = render_failure(test.html, viewport(300, test.vertical));
    EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;
    EXPECT_TRUE(error.location.has_value()) << error.message;
  }
}

// `--dump-stage svg` は、PNG に描かれないグリフを原点に描いてはいけない。
// エラーになるのでそもそもダンプが出ない、が期待（issue #19 の受け入れ条件）。
TEST(NonFiniteLengths, SvgDumpDoesNotDisagreeWithThePng) {
  RenderOptions options = viewport(300);
  options.viewport_height = 100;
  const auto svg = dump(R"(<div style="padding-left:1e38em">A</div>)", japanese_fonts(), ImageSet{},
                        options, DumpStage::Svg);
  ASSERT_FALSE(svg.has_value()) << "SVG ダンプが出てしまった（PNG と食い違う）";
  EXPECT_EQ(svg.error().kind, ErrorKind::LimitExceeded) << to_string(svg.error());
}

// ---------------------------------------------------------------------------
// 性質テスト: 成功したケースには非有限な数値が 1 つもない
// ---------------------------------------------------------------------------

// 長さ（issue #19 の受け入れ条件の組み合わせ）。
constexpr std::array<std::string_view, 7> kValues = {"0",    "1",    "1e3",   "1e7",
                                                     "1e19", "1e30", "3.4e38"};
constexpr std::array<std::string_view, 3> kUnits = {"px", "em", "%"};
constexpr std::array<std::size_t, 3> kDepths = {1, 3, 10};
constexpr std::array<int, 3> kViewports = {200, 1000, 16384};

// 深さ N の入れ子を作る。
std::string nest(std::string_view declarations, std::size_t depth, std::string_view leaf) {
  std::string out;
  for (std::size_t i = 0; i < depth; ++i) {
    out += "<div style=\"";
    out += declarations;
    out += "\">";
  }
  out += leaf;
  for (std::size_t i = 0; i < depth; ++i) {
    out += "</div>";
  }
  return out;
}

// 成功したダンプに非有限（JSON では null）が現れないことを確かめる。
void expect_no_non_finite(std::string_view html, const RenderOptions& options) {
  for (const DumpStage stage : kCheckedStages) {
    const auto text = dump(html, FontSet{}, ImageSet{}, options, stage);
    if (!text) {
      // エラーになるのは構わない（対応外の値・上限超過）。黙って壊れないことが要件。
      EXPECT_FALSE(text.error().message.empty()) << html.substr(0, 96);
      continue;
    }
    EXPECT_EQ(text->find("null"), std::string::npos)
        << to_string(stage) << " に非有限な数値が出た: " << html.substr(0, 96);
  }
}

TEST(NonFiniteLengths, PropertyLengthsUnitsAndNesting) {
  for (const std::string_view value : kValues) {
    for (const std::string_view unit : kUnits) {
      for (const std::size_t depth : kDepths) {
        for (const int width : kViewports) {
          const std::string length = std::string{value} + std::string{unit};
          for (const std::string_view property : {"padding", "width", "margin", "font-size"}) {
            const std::string html = nest(std::string{property} + ":" + length, depth, "あ");
            SCOPED_TRACE(html.substr(0, 96));
            expect_no_non_finite(html, viewport(width));
          }
        }
      }
    }
  }
}

TEST(NonFiniteLengths, PropertyLineHeightMultipliers) {
  for (const std::string_view value : kValues) {
    for (const std::size_t depth : kDepths) {
      const std::string html = nest(std::string{"line-height:"} + std::string{value}, depth, "あ");
      SCOPED_TRACE(html.substr(0, 96));
      expect_no_non_finite(html, viewport(1000));
    }
  }
}

TEST(NonFiniteLengths, PropertyFlexFactors) {
  for (const std::string_view grow : {"0", "1", "1e30", "1e38"}) {
    for (const std::string_view shrink : {"0", "1", "1e30", "1e38"}) {
      for (const int width : kViewports) {
        const std::string html = std::string{R"(<div style="display:flex;width:10px">)"} +
                                 R"(<div style="flex-grow:)" + std::string{grow} +
                                 ";flex-shrink:" + std::string{shrink} + R"(;width:1e30px">あ)" +
                                 "</div></div>";
        SCOPED_TRACE(html.substr(0, 96));
        expect_no_non_finite(html, viewport(width));
      }
    }
  }
}

TEST(NonFiniteLengths, PropertyWritingModesFlexAndRuby) {
  struct Wrapper {
    std::string_view open;
    std::string_view close;
    bool vertical;
  };
  constexpr std::array<Wrapper, 4> kWrappers = {{
      {"<div>", "</div>", false},
      {R"(<div style="writing-mode:vertical-rl">)", "</div>", true},
      {R"(<div style="display:flex">)", "</div>", false},
      {"<div><ruby>", "<rt>とう</rt></ruby></div>", false},
  }};
  for (const Wrapper& wrapper : kWrappers) {
    for (const std::string_view value : kValues) {
      for (const std::string_view unit : kUnits) {
        const std::string html = std::string{wrapper.open} + R"(<span style="letter-spacing:)" +
                                 std::string{value} + std::string{unit} + R"(">東</span>)" +
                                 std::string{wrapper.close};
        SCOPED_TRACE(html.substr(0, 120));
        expect_no_non_finite(html, viewport(1000, wrapper.vertical));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 回帰: 常識的な値は従来どおり通り、数値が変わらない
// ---------------------------------------------------------------------------

TEST(NonFiniteLengths, OrdinaryValuesAreUnchanged) {
  struct OkCase {
    std::string_view html;
    std::string_view expect;  // style ダンプに必ず現れる数値
  };
  constexpr std::array<OkCase, 4> kCases = {{
      {R"(<div style="padding:1000px">あ</div>)", "1000"},
      {R"(<div style="width:5000px">あ</div>)", "5000"},
      {R"(<div style="font-size:100px"><div style="font-size:1em"><div style="padding:1em">)"
       R"(あ</div></div></div>)",
       "100"},
      {R"(<div style="letter-spacing:-2px">あ</div>)", "-2"},
  }};
  RenderOptions options = viewport(16000);
  options.limits.font_size_device_px = 4096;
  for (const OkCase& test : kCases) {
    SCOPED_TRACE(test.html);
    const auto text = dump(test.html, FontSet{}, ImageSet{}, options, DumpStage::Style);
    ASSERT_TRUE(text.has_value()) << (text ? std::string{} : to_string(text.error()));
    EXPECT_NE(text->find(test.expect), std::string::npos);
    EXPECT_EQ(text->find("null"), std::string::npos);
  }
}

// 上限は入力の一部（A25）。既定値を緩めれば通ることを押さえる（緩め方がメッセージに出る）。
TEST(NonFiniteLengths, RaisingTheLimitAllowsLargerLengths) {
  RenderOptions options = viewport(300);
  options.viewport_height = 100;
  const std::string_view html = R"(<div style="padding:3e7px">あ</div>)";

  const RenderError error = render_failure(html, options);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded) << error.message;

  options.limits.length_px = 1e8F;
  const auto text = dump(html, FontSet{}, ImageSet{}, options, DumpStage::Style);
  EXPECT_TRUE(text.has_value()) << (text ? std::string{} : to_string(text.error()));
}

// ---------------------------------------------------------------------------
// 第 2 段階（layout の出口の走査）に残るもの
// ---------------------------------------------------------------------------

// `%` の解決と flex の比（`factor / factors.scaled` が inf/inf）は包含ブロックが要るので
// style では判定できない（A5）。この 2 つはいまも **style を通り抜けて layout で非有限になる**。
//
// #19 の第 2 段階（layout の出口の走査）が入ったら、ここは失敗する。そうなったら
// `render_failure` に裏返し、kCheckedStages に Box / DisplayList を足すこと。
TEST(NonFiniteLengths, PendingSecondStageCasesStillPassStyle) {
  RenderOptions options = viewport(1000);
  options.viewport_height = 100;
  for (const std::string_view html :
       {R"(<div style="width:1e38%">あ</div>)",
        R"(<div style="display:flex;width:10px"><div style="flex-shrink:1e38;width:1e7px">)"
        R"(あ</div></div>)"}) {
    SCOPED_TRACE(html);
    const auto style_dump = dump(html, FontSet{}, ImageSet{}, options, DumpStage::Style);
    ASSERT_TRUE(style_dump.has_value())
        << "第 2 段階が入ったらここが失敗する（期待どおり）: "
        << (style_dump ? std::string{} : to_string(style_dump.error()));
    EXPECT_EQ(style_dump->find("null"), std::string::npos) << "style では有限";

    // layout ではまだ非有限になる（第 2 段階で塞ぐ）
    const auto box = dump(html, japanese_fonts(), ImageSet{}, options, DumpStage::Box);
    ASSERT_TRUE(box.has_value()) << (box ? std::string{} : to_string(box.error()));
    EXPECT_NE(box->find("null"), std::string::npos)
        << "layout で非有限にならなくなった = 第 2 段階が入った。このテストを裏返すこと";
  }
}

}  // namespace
}  // namespace shashoku::test
