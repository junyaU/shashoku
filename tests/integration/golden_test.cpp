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
// 7. flexbox の見本（DESIGN.md Phase 6）
//
// justify-content の 6 値・align-items の 4 値・gap・flex-grow の配分が一目で分かる形。
// 色つきの箱だけなのでフォントに依存する部分が少なく、配置の回帰を見つけやすい。
// ---------------------------------------------------------------------------

TEST(Golden, FlexAlignment) {
  constexpr std::string_view kHtml = R"(
<style>
  .sheet  { background: #ffffff; padding: 14px; font-size: 12px; color: #343a40; }
  .label  { margin-top: 9px; margin-bottom: 3px; color: #868e96; }
  .row    { display: flex; height: 30px; background: #f1f3f5; }
  .b      { width: 52px; height: 18px; background: #4c6ef5; }
  .s      { width: 52px; height: 10px; background: #4c6ef5; }
  .m      { width: 52px; height: 18px; background: #f76707; }
  .l      { width: 52px; height: 26px; background: #2f9e44; }
  .n      { width: 52px; background: #7048e8; }
  .start  { justify-content: flex-start; }
  .end    { justify-content: flex-end; }
  .center { justify-content: center; }
  .between{ justify-content: space-between; }
  .around { justify-content: space-around; }
  .evenly { justify-content: space-evenly; }
  .top    { align-items: flex-start; }
  .middle { align-items: center; }
  .bottom { align-items: flex-end; }
  .stretch{ align-items: stretch; }
  .gap    { gap: 16px; }
  .g1     { flex: 1; height: 18px; background: #4c6ef5; }
  .g2     { flex: 2; height: 18px; background: #f76707; }
</style>
<div class="sheet">
  <div class="label">justify-content: flex-start</div>
  <div class="row start"><div class="b"></div><div class="b"></div><div class="b"></div></div>
  <div class="label">justify-content: center</div>
  <div class="row center"><div class="b"></div><div class="b"></div><div class="b"></div></div>
  <div class="label">justify-content: flex-end</div>
  <div class="row end"><div class="b"></div><div class="b"></div><div class="b"></div></div>
  <div class="label">justify-content: space-between</div>
  <div class="row between"><div class="b"></div><div class="b"></div><div class="b"></div></div>
  <div class="label">justify-content: space-around</div>
  <div class="row around"><div class="b"></div><div class="b"></div><div class="b"></div></div>
  <div class="label">justify-content: space-evenly</div>
  <div class="row evenly"><div class="b"></div><div class="b"></div><div class="b"></div></div>
  <div class="label">align-items: flex-start / center / flex-end</div>
  <div class="row top gap"><div class="s"></div><div class="m"></div><div class="l"></div></div>
  <div class="row middle gap"><div class="s"></div><div class="m"></div><div class="l"></div></div>
  <div class="row bottom gap"><div class="s"></div><div class="m"></div><div class="l"></div></div>
  <div class="label">align-items: stretch（高さ未指定の子が伸びる）+ gap: 16px</div>
  <div class="row stretch gap"><div class="n"></div><div class="n"></div><div class="n"></div></div>
  <div class="label">flex-grow 1 : 2（残りを 1 対 2 で分ける）</div>
  <div class="row middle gap"><div class="g1"></div><div class="g2"></div></div>
</div>)";

  const Bitmap bitmap = render_bitmap(kHtml, japanese_fonts(), options_for(600));
  EXPECT_TRUE(expect_golden(bitmap, "flex_alignment"));
}

// ---------------------------------------------------------------------------
// 8. <img>（A12）
//
// 文中のインライン画像（ベースライン揃え）、border-radius による円形クリップ、
// display: block + margin: 0 auto の中央寄せ、枠線・padding・角丸つきの画像。
// ---------------------------------------------------------------------------

TEST(Golden, InlineImage) {
  constexpr std::string_view kHtml = R"(
<style>
  .sheet  { background: #ffffff; padding: 18px; font-size: 17px; line-height: 1.9; color: #212529; }
  .inline { width: 22px; height: 22px; }
  .round  { width: 40px; height: 40px; border-radius: 20px; }
  .framed { display: block; width: 96px; height: 96px; margin: 14px auto;
            border: 4px solid #1971c2; padding: 8px; border-radius: 20px;
            background-color: #e7f5ff; }
  p { margin-top: 0; margin-bottom: 0; }
</style>
<div class="sheet">
  <p>文中の画像 <img class="inline" src="icon"> はベースラインに揃います。丸い
     <img class="round" src="icon"> も同じ行に流れます。</p>
  <img class="framed" src="icon">
  <p>上は display: block と margin: 0 auto による中央寄せ（枠線・padding・角丸つき）。</p>
</div>)";

  const Bitmap bitmap = render_bitmap(kHtml, japanese_fonts(), icon_images(), options_for(480));
  EXPECT_TRUE(expect_golden(bitmap, "inline_image"));
}

// ---------------------------------------------------------------------------
// 9. OG カード（DESIGN.md Phase 6 の受け入れ条件: アイコン + タイトル + フッター）
//
// 実寸は 1200x630 だが、ゴールデンは scale 0.5 の 600x315 で持つ（リポジトリを太らせない）。
// ---------------------------------------------------------------------------

TEST(Golden, OgCard) {
  const Result<std::vector<std::uint8_t>> source =
      read_file(std::filesystem::path(SHASHOKU_EXAMPLES_DIR) / "og_card.html");
  ASSERT_TRUE(source.has_value()) << "examples/og_card.html を読めません";
  const std::string html(source->begin(), source->end());

  RenderOptions options = options_for(1200);
  options.viewport_height = 630;
  options.scale = 0.5F;

  const Bitmap bitmap = render_bitmap(html, japanese_fonts(), icon_images(), options);
  EXPECT_EQ(bitmap.width, 600U);
  EXPECT_EQ(bitmap.height, 315U);
  EXPECT_TRUE(expect_golden(bitmap, "og_card"));
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
