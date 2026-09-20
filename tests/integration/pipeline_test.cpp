#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// パイプライン全体の性質: 決定性・出力サイズ・各段のダンプ・豆腐の警告。
namespace shashoku::test {
namespace {

constexpr std::string_view kSample =
    R"(<div style="padding: 8px; font-size: 16px; line-height: 1.5">こんにちは、世界のみんな。</div>)";

// DESIGN.md §3-5: 同じ入力 → バイト単位で同じ PNG。
TEST(Determinism, SameInputGivesSameBytes) {
  const FontSet fonts = japanese_fonts();
  const RenderOptions options = options_for(320);
  const auto first = render(kSample, fonts, options);
  const auto second = render(kSample, fonts, options);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(first->png, second->png);
  EXPECT_EQ(first->width, second->width);
  EXPECT_EQ(first->height, second->height);
}

// 別々に作った FontSet からでも同じ結果になる（グローバル状態を持たない）。
TEST(Determinism, FreshFontSetsGiveSameBytes) {
  const auto first = render(kSample, japanese_fonts(), options_for(320));
  const auto second = render(kSample, japanese_fonts(), options_for(320));
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(first->png, second->png);
}

// ---------------------------------------------------------------------------
// 出力サイズ（ARCHITECTURE.md §3.10）
// ---------------------------------------------------------------------------

TEST(OutputSize, HeightFollowsContent) {
  RenderOptions options = options_for(320);
  const auto one_line = render(R"(<div style="line-height: 20px">あ</div>)", japanese_fonts(),
                               options);
  const auto two_lines = render(R"(<div style="line-height: 20px">あ<br>い</div>)",
                                japanese_fonts(), options);
  ASSERT_TRUE(one_line.has_value()) << to_string(one_line.error());
  ASSERT_TRUE(two_lines.has_value()) << to_string(two_lines.error());
  EXPECT_EQ(one_line->width, 320);
  EXPECT_EQ(one_line->height, 20);
  EXPECT_EQ(two_lines->height, 40);
}

TEST(OutputSize, ExplicitHeightWins) {
  RenderOptions options = options_for(320);
  options.viewport_height = 100;
  const auto result = render(R"(<div style="line-height: 20px">あ</div>)", japanese_fonts(),
                             options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->width, 320);
  EXPECT_EQ(result->height, 100);
}

// デバイスピクセル = ceil(CSS px * scale)。
TEST(OutputSize, ScaleMultipliesDevicePixels) {
  RenderOptions options = options_for(101);
  options.viewport_height = 51;
  options.scale = 2.0F;
  const auto result = render(R"(<div style="line-height: 20px">あ</div>)", japanese_fonts(),
                             options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->width, 202);
  EXPECT_EQ(result->height, 102);
}

// 内容が空で高さの指定もなければ「描くものがない」（InvalidOption）。
TEST(OutputSize, NothingToRender) {
  const auto result = render("", japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(result.error().message.find("nothing to render"), std::string::npos);
}

TEST(OutputSize, EmptyDocumentWithExplicitHeightIsFine) {
  RenderOptions options = options_for(320);
  options.viewport_height = 40;
  const auto result = render("", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->height, 40);
}

// ---------------------------------------------------------------------------
// 豆腐（DESIGN.md §3-6 の唯一の例外: エラーではなく警告で続行）
// ---------------------------------------------------------------------------

TEST(Warnings, MissingGlyphIsReportedAndRenderingContinues) {
  const auto result = render(R"(<div style="font-size: 20px">ABC😀あ</div>)",
                             latin_then_japanese(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 1U);
  EXPECT_EQ(result->warnings[0].kind, WarningKind::MissingGlyph);
  EXPECT_EQ(result->warnings[0].codepoint, U'\U0001F600');
  EXPECT_EQ(result->warnings[0].detail, "no font has a glyph for U+1F600");
  EXPECT_FALSE(result->png.empty());
}

// 警告の並びはコードポイント昇順に固定する（出現順ではない）。
TEST(Warnings, SortedByCodepoint) {
  const auto result = render(R"(<div style="font-size: 20px">😀あ😃</div>)", japanese_fonts(),
                             options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 2U);
  EXPECT_EQ(result->warnings[0].codepoint, U'\U0001F600');
  EXPECT_EQ(result->warnings[1].codepoint, U'\U0001F603');
}

TEST(Warnings, NoneForOrdinaryJapanese) {
  const auto result = render(kSample, japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(result->warnings.empty());
}

// ---------------------------------------------------------------------------
// --dump-stage（DESIGN.md §3-3）
// ---------------------------------------------------------------------------

TEST(DumpStages, EveryStageProducesOutput) {
  const FontSet fonts = japanese_fonts();
  const RenderOptions options = options_for(320);
  for (const DumpStage stage :
       {DumpStage::Dom, DumpStage::Style, DumpStage::Box, DumpStage::DisplayList}) {
    const auto dumped = dump(kSample, fonts, ImageSet{}, options, stage);
    ASSERT_TRUE(dumped.has_value()) << to_string(stage) << ": " << to_string(dumped.error());
    EXPECT_TRUE(dumped->starts_with("{")) << to_string(stage) << ": " << *dumped;
    EXPECT_GT(dumped->size(), 16U) << to_string(stage);
  }

  const auto svg = dump(kSample, fonts, ImageSet{}, options, DumpStage::Svg);
  ASSERT_TRUE(svg.has_value()) << to_string(svg.error());
  EXPECT_NE(svg->find("<svg"), std::string::npos);
  EXPECT_NE(svg->find("</svg>"), std::string::npos);
}

// ダンプも決定的（同じ入力なら同じ文字列）。
TEST(DumpStages, Deterministic) {
  const auto first = dump(kSample, japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  const auto second = dump(kSample, japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, *second);
}

// ダンプのボックスツリーは、レイアウトが本当に走っていることを示す座標を持つ。
TEST(DumpStages, BoxContainsGlyphPositions) {
  const auto box = dump(kSample, japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  ASSERT_TRUE(box.has_value()) << to_string(box.error());
  EXPECT_NE(box->find("\"glyphs\""), std::string::npos);
  EXPECT_NE(box->find("\"baseline\""), std::string::npos);
  EXPECT_NE(box->find("こんにちは、世界のみんな。"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::test
