#include "support/golden.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/result.hpp"
#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// ゴールデンテスト（DESIGN.md §10-1 / ARCHITECTURE.md §4）。
// 期待画像は tests/golden/*.png。純粋関数なのでピクセル完全一致で比べられる。
// 更新は SHASHOKU_UPDATE_GOLDEN=1 で書き出したあと、**必ず目で見てから**コミットする。
namespace shashoku::test {
namespace {

// ---------------------------------------------------------------------------
// 1. 見出しと段落（背景色・padding・枠線・角丸のある箱）
// ---------------------------------------------------------------------------

TEST(Golden, HeadingAndParagraph) {
  constexpr std::string_view kHtml = R"(
<div style="background-color: #f6f2e8; border: 2px solid #b9a887; border-radius: 10px;
            padding: 20px; font-size: 16px; line-height: 1.7; color: #2b2419">
  <h1 style="font-size: 24px; margin-top: 0; margin-bottom: 12px; color: #7a2f1d">組版の話。</h1>
  <p style="margin-top: 0; margin-bottom: 0">
    活字を組み、紙に刷る。写真植字機がやっていたことを、いまはソフトウェアが引き受けています。
  </p>
</div>)";

  const Bitmap bitmap = render_bitmap(kHtml, japanese_fonts(), options_for(440));
  EXPECT_TRUE(expect_golden(bitmap, "heading_and_paragraph"));
}

// ---------------------------------------------------------------------------
// 2. 禁則のショーケース（DESIGN.md Phase 4 の受け入れ条件を絵で確かめる）
//
// 同じ和文・同じ幅で、追い出し / 追い込み / ぶら下げの 3 通り。
// 句読点・括弧・小書き仮名・「……」を含み、どの絵でも行頭に句読点が出ていないこと。
// 幅 360px はこの文で 3 つのポリシーがそれぞれ違う結果を出す値（PoliciesDiffer が見張る）。
// ---------------------------------------------------------------------------

constexpr int kKinsokuWidth = 360;

constexpr std::string_view kKinsokuHtml = R"(
<div style="background-color: #ffffff; padding: 16px; font-size: 19px; line-height: 1.8;
            color: #1c1a17">
  彼は言った。「ちょっと待って、それは違うよ」……だが、返事はなかった。
</div>)";

TEST(Golden, KinsokuOidashi) {
  RenderOptions options = options_for(kKinsokuWidth);
  options.line_break.overflow = OverflowPolicy::Oidashi;
  const Bitmap bitmap = render_bitmap(kKinsokuHtml, japanese_fonts(), options);
  EXPECT_TRUE(expect_golden(bitmap, "kinsoku_oidashi"));
}

TEST(Golden, KinsokuOikomi) {
  RenderOptions options = options_for(kKinsokuWidth);
  options.line_break.overflow = OverflowPolicy::Oikomi;
  const Bitmap bitmap = render_bitmap(kKinsokuHtml, japanese_fonts(), options);
  EXPECT_TRUE(expect_golden(bitmap, "kinsoku_oikomi"));
}

TEST(Golden, KinsokuBurasage) {
  RenderOptions options = options_for(kKinsokuWidth);
  options.line_break.overflow = OverflowPolicy::Burasage;
  const Bitmap bitmap = render_bitmap(kKinsokuHtml, japanese_fonts(), options);
  EXPECT_TRUE(expect_golden(bitmap, "kinsoku_burasage"));
}

// 3 つのポリシーは同じ入力に対して違う絵を出す（どれかが効いていない、を検出する）。
TEST(Golden, PoliciesDiffer) {
  const auto render_with = [](OverflowPolicy policy) {
    RenderOptions options = options_for(kKinsokuWidth);
    options.line_break.overflow = policy;
    return render(kKinsokuHtml, japanese_fonts(), options);
  };
  const auto oidashi = render_with(OverflowPolicy::Oidashi);
  const auto oikomi = render_with(OverflowPolicy::Oikomi);
  const auto burasage = render_with(OverflowPolicy::Burasage);
  ASSERT_TRUE(oidashi.has_value() && oikomi.has_value() && burasage.has_value());
  EXPECT_NE(oidashi->png, oikomi->png);
  EXPECT_NE(oidashi->png, burasage->png);
}

// ---------------------------------------------------------------------------
// 3. 和欧混植 + span によるスタイル切り替え + letter-spacing + text-align
// ---------------------------------------------------------------------------

TEST(Golden, MixedScriptsAndAlignment) {
  constexpr std::string_view kHtml = R"(
<div style="background-color: #fdfdfd; padding: 18px; font-size: 16px; line-height: 1.8;
            color: #23201c">
  <p style="margin-top: 0; text-align: center">
    <span style="font-size: 21px; font-weight: 700; color: #123a63">shashoku</span>
    <span style="letter-spacing: 3px">は写植の再発明</span>
  </p>
  <p style="text-align: right; color: #55504a">右寄せ。HTML から PNG へ。</p>
  <p style="text-align: justify; margin-bottom: 0">
    両端揃えでは、追い出しで生じた行末の空きを字間に配分します。これは日本語組版の基本動作です。
  </p>
</div>)";

  const Bitmap bitmap = render_bitmap(kHtml, japanese_fonts(), options_for(460));
  EXPECT_TRUE(expect_golden(bitmap, "mixed_scripts_and_alignment"));
}

// ---------------------------------------------------------------------------
// 4. フォールバックと豆腐（DESIGN.md §6-5, §6-6）
//
// FontSet は [欧文, 和文] の順。欧文は欧文フォント、和文はフォールバックで和文フォント、
// 絵文字はどちらにも無いので □ を描いて警告を返す。
// ---------------------------------------------------------------------------

TEST(Golden, FallbackAndTofu) {
  constexpr std::string_view kHtml = R"(
<div style="background-color: #ffffff; padding: 18px; font-size: 20px; line-height: 1.8;
            color: #17140f">
  Hello, 世界のみんな。ABC 123 😀 は豆腐になります。
</div>)";

  const auto result = render(kHtml, latin_then_japanese(), options_for(460));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());

  // 絵文字 1 文字だけが豆腐として報告される
  ASSERT_EQ(result->warnings.size(), 1U);
  EXPECT_EQ(result->warnings[0].kind, WarningKind::MissingGlyph);
  EXPECT_EQ(result->warnings[0].codepoint, U'\U0001F600');

  const Bitmap bitmap = render_bitmap(kHtml, latin_then_japanese(), options_for(460));
  EXPECT_TRUE(expect_golden(bitmap, "fallback_and_tofu"));
}

// ---------------------------------------------------------------------------
// 5. scale（同じ HTML の 1 倍と 2 倍。寸法はちょうど 2 倍になる）
// ---------------------------------------------------------------------------

constexpr std::string_view kScaleHtml = R"(
<div style="background-color: #eef3f7; border: 1px solid #9ab; border-radius: 8px;
            padding: 14px; font-size: 18px; line-height: 1.6; color: #10202c">
  高解像度でも同じ組版。
</div>)";

TEST(Golden, ScaleOne) {
  const Bitmap bitmap = render_bitmap(kScaleHtml, japanese_fonts(), options_for(400));
  EXPECT_TRUE(expect_golden(bitmap, "scale_1x"));
}

TEST(Golden, ScaleTwo) {
  RenderOptions options = options_for(400);
  options.scale = 2.0F;
  const Bitmap bitmap = render_bitmap(kScaleHtml, japanese_fonts(), options);
  EXPECT_TRUE(expect_golden(bitmap, "scale_2x"));

  // 2 倍の絵はちょうど 2 倍の大きさ（組版は CSS px のまま、ラスタライズだけが細かくなる）
  const Bitmap one = render_bitmap(kScaleHtml, japanese_fonts(), options_for(400));
  EXPECT_EQ(bitmap.width, one.width * 2);
  EXPECT_EQ(bitmap.height, one.height * 2);
}

// ---------------------------------------------------------------------------
// 6. <style> とクラスセレクタ、<br>、line-height、マージンの相殺（A10）
// ---------------------------------------------------------------------------

TEST(Golden, StylesheetAndClasses) {
  constexpr std::string_view kHtml = R"(
<style>
  .sheet { background-color: #fffef9; padding: 16px; color: #241f18; font-size: 16px; }
  p { margin-top: 12px; margin-bottom: 12px; line-height: 1.5; }
  .tight { line-height: 1.1; color: #6b5f4d; }
  #headline { font-size: 20px; font-weight: 700; color: #1d4a2e; margin-top: 0; }
</style>
<div class="sheet">
  <p id="headline">セレクタも効きます。</p>
  <p>隣り合う段落の margin は相殺されます（上下 12px が重なって 12px）。</p>
  <p class="tight">行間を詰めた段落。<br>ここは br で改行しました。</p>
</div>)";

  const Bitmap bitmap = render_bitmap(kHtml, japanese_fonts(), options_for(440));
  EXPECT_TRUE(expect_golden(bitmap, "stylesheet_and_classes"));
}

// ---------------------------------------------------------------------------
// README のサンプル（DESIGN.md Phase 5 の受け入れ条件）
// ---------------------------------------------------------------------------

TEST(Golden, ReadmeExample) {
  const Result<std::vector<std::uint8_t>> source =
      read_file(std::filesystem::path(SHASHOKU_EXAMPLES_DIR) / "hello.html");
  ASSERT_TRUE(source.has_value()) << "examples/hello.html を読めません";
  const std::string html(source->begin(), source->end());

  const Bitmap bitmap = render_bitmap(html, japanese_fonts(), options_for(600));
  EXPECT_TRUE(expect_golden(bitmap, "hello"));
}

}  // namespace
}  // namespace shashoku::test
