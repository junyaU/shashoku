#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"
#include "support/failure.hpp"

// フォントと画像の再利用 API（ARCHITECTURE.md A34 / issue #7）。
//
// 肝は 1 つだけ: **共有資源を使い回しても、毎回 FontSet / ImageSet から作り直したときと
// バイト単位で同じものが出る**こと。純粋関数の契約（DESIGN.md §3-5）は「過去の実行に
// 結果が影響されない」なので、共有資源に実行の痕跡が残らないことをここで固定する。
namespace shashoku::test {
namespace {

constexpr std::string_view kSample =
    R"(<div style="padding: 8px; font-size: 16px; line-height: 1.5">こんにちは、世界のみんな。<br>)"
    R"(<span style="font-weight: bold">ABC AVTo</span> と )"
    R"(<span style="color: #c00">混植</span>のテスト。</div>)";

// 豆腐（どのフォントにも無い絵文字）が出る入力。共有資源に「見た豆腐」が溜まって
// いないことを確かめるために挟む。
constexpr std::string_view kTofu =
    R"(<div style="font-size: 16px">絵文字 &#x1F600; と &#x1F601; を含む行。</div>)";

constexpr std::string_view kImageHtml =
    R"(<div style="padding: 6px"><img src="icon" style="width: 32px; height: 32px"> 図版</div>)";

LoadedFonts must_prepare_fonts(const FontSet& fonts) {
  std::expected<LoadedFonts, RenderError> loaded = LoadedFonts::prepare(fonts);
  if (!loaded) {
    ADD_FAILURE() << "LoadedFonts::prepare: " << to_string(loaded.error());
    return std::move(*LoadedFonts::prepare(japanese_fonts()));
  }
  return std::move(*loaded);
}

LoadedImages must_prepare_images(const ImageSet& images, const RenderLimits& limits = {}) {
  std::expected<LoadedImages, RenderError> loaded = LoadedImages::prepare(images, limits);
  if (!loaded) {
    ADD_FAILURE() << "LoadedImages::prepare: " << to_string(loaded.error());
    return std::move(*LoadedImages::prepare(ImageSet{}));
  }
  return std::move(*loaded);
}

// 2 つの結果が「同じ」= PNG のバイト列も警告も大きさも一致すること。
void expect_same(const std::expected<RenderResult, RenderFailure>& actual,
                 const std::expected<RenderResult, RenderFailure>& expected,
                 std::string_view what) {
  ASSERT_TRUE(expected.has_value()) << what << ": " << to_string(expected.error());
  ASSERT_TRUE(actual.has_value()) << what << ": " << to_string(actual.error());
  EXPECT_EQ(actual->png, expected->png) << what;
  EXPECT_EQ(actual->warnings, expected->warnings) << what;
  EXPECT_EQ(actual->width, expected->width) << what;
  EXPECT_EQ(actual->height, expected->height) << what;
}

// ---------------------------------------------------------------------------
// 1. 同じ共有資源で N 回 = 毎回作り直し
// ---------------------------------------------------------------------------

TEST(Reuse, ManyRendersMatchRebuildingEveryTime) {
  const FontSet fonts = latin_then_japanese();
  const ImageSet images = icon_images();
  const RenderOptions options = options_for(320);

  const LoadedFonts loaded_fonts = must_prepare_fonts(fonts);
  const LoadedImages loaded_images = must_prepare_images(images);
  EXPECT_EQ(loaded_fonts.size(), 2U);
  EXPECT_FALSE(loaded_fonts.empty());
  EXPECT_EQ(loaded_images.size(), 1U);

  for (int i = 0; i < 8; ++i) {
    // 毎回 FontSet / ImageSet を作り直す経路を基準にする（従来の render()）。
    expect_same(render(kSample, loaded_fonts, loaded_images, options),
                render(kSample, latin_then_japanese(), icon_images(), options),
                std::string("render #") + std::to_string(i));
  }
}

// 画像を渡さないオーバーロードも、空の ImageSet を渡したのと同じ結果になる。
TEST(Reuse, WithoutImagesMatchesAnEmptyImageSet) {
  const RenderOptions options = options_for(320);
  const LoadedFonts loaded_fonts = must_prepare_fonts(japanese_fonts());

  expect_same(render(kSample, loaded_fonts, options),
              render(kSample, japanese_fonts(), ImageSet{}, options), "no images");
  expect_same(render(kSample, loaded_fonts, options), render(kSample, japanese_fonts(), options),
              "no images (FontSet overload)");
}

// 豆腐の警告（A31）も一致する。Shaper / FontStore に「見た豆腐」が溜まっていたら、
// 2 回目以降で件数が変わってここが落ちる。
TEST(Reuse, WarningsMatchEveryTime) {
  const RenderOptions options = options_for(320);
  const LoadedFonts loaded_fonts = must_prepare_fonts(latin_then_japanese());

  const auto reference = render(kTofu, latin_then_japanese(), options);
  ASSERT_TRUE(reference.has_value()) << to_string(reference.error());
  ASSERT_EQ(reference->warnings.size(), 2U) << "この入力は豆腐が 2 種類出るはず";

  for (int i = 0; i < 4; ++i) {
    expect_same(render(kTofu, loaded_fonts, options), reference,
                std::string("tofu #") + std::to_string(i));
  }
}

// ---------------------------------------------------------------------------
// 2. 実行の混線がないこと（A → B → A）
// ---------------------------------------------------------------------------

TEST(Reuse, DifferentInputsDoNotLeakIntoEachOther) {
  const RenderOptions options = options_for(320);
  const LoadedFonts loaded_fonts = must_prepare_fonts(latin_then_japanese());
  const LoadedImages loaded_images = must_prepare_images(icon_images());

  const auto first_a = render(kSample, loaded_fonts, loaded_images, options);
  const auto b = render(kTofu, loaded_fonts, loaded_images, options);  // 豆腐が出る入力
  const auto image = render(kImageHtml, loaded_fonts, loaded_images, options);
  const auto second_a = render(kSample, loaded_fonts, loaded_images, options);

  ASSERT_TRUE(b.has_value()) << to_string(b.error());
  ASSERT_TRUE(image.has_value()) << to_string(image.error());
  EXPECT_FALSE(b->warnings.empty());
  expect_same(second_a, first_a, "A → B → A");
  // A には豆腐がない。B の警告が混ざっていないこと。
  ASSERT_TRUE(second_a.has_value());
  EXPECT_TRUE(second_a->warnings.empty());
}

// 逆順（豆腐が先）でも同じ。
TEST(Reuse, TofuFirstDoesNotChangeTheNextRender) {
  const RenderOptions options = options_for(320);
  const LoadedFonts loaded_fonts = must_prepare_fonts(latin_then_japanese());

  const auto tofu = render(kTofu, loaded_fonts, options);
  ASSERT_TRUE(tofu.has_value()) << to_string(tofu.error());
  expect_same(render(kSample, loaded_fonts, options),
              render(kSample, latin_then_japanese(), options), "after tofu");
}

// ---------------------------------------------------------------------------
// 3. dump() も同じ
// ---------------------------------------------------------------------------

TEST(Reuse, DumpMatchesForEveryStage) {
  const RenderOptions options = options_for(320);
  const LoadedFonts loaded_fonts = must_prepare_fonts(japanese_fonts());
  const LoadedImages loaded_images = must_prepare_images(icon_images());

  for (const DumpStage stage :
       {DumpStage::Dom, DumpStage::Style, DumpStage::Box, DumpStage::DisplayList, DumpStage::Svg}) {
    const auto expected = dump(kImageHtml, japanese_fonts(), icon_images(), options, stage);
    const auto actual = dump(kImageHtml, loaded_fonts, loaded_images, options, stage);
    ASSERT_TRUE(expected.has_value()) << to_string(stage) << ": " << to_string(expected.error());
    ASSERT_TRUE(actual.has_value()) << to_string(stage) << ": " << to_string(actual.error());
    EXPECT_EQ(*actual, *expected) << to_string(stage);
  }
}

// ---------------------------------------------------------------------------
// 4. prepare() のエラーと、ムーブ済みのオブジェクト
// ---------------------------------------------------------------------------

TEST(Reuse, PrepareRejectsAnEmptyFontSet) {
  const std::expected<LoadedFonts, RenderError> loaded = LoadedFonts::prepare(FontSet{});
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().kind, ErrorKind::NoFonts);
}

TEST(Reuse, PrepareRejectsBrokenFontBytes) {
  FontSet fonts;
  const std::vector<std::uint8_t> garbage(256, 0x41);
  fonts.add(garbage);
  const std::expected<LoadedFonts, RenderError> loaded = LoadedFonts::prepare(fonts);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().kind, ErrorKind::FontLoad);
  EXPECT_NE(loaded.error().message.find("font #0"), std::string::npos) << loaded.error().message;
}

// 重複した名前は prepare() が弾く（従来は render() の中で弾いていた検査を前に出した）。
TEST(Reuse, PrepareRejectsDuplicateImageNames) {
  ImageSet images;
  images.add("icon", test_icon_bytes());
  images.add("icon", test_icon_bytes());
  const std::expected<LoadedImages, RenderError> loaded = LoadedImages::prepare(images);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().kind, ErrorKind::InvalidOption);
}

// ムーブ済みのオブジェクトを渡したら、黙って「フォント 0 本」として組まずに落とす。
TEST(Reuse, MovedFromResourcesAreRejected) {
  LoadedFonts fonts = must_prepare_fonts(japanese_fonts());
  LoadedImages images = must_prepare_images(icon_images());
  const LoadedFonts moved_fonts(std::move(fonts));
  const LoadedImages moved_images(std::move(images));

  EXPECT_EQ(fonts.size(), 0U);   // NOLINT(bugprone-use-after-move): ムーブ後の状態が主題
  EXPECT_TRUE(fonts.empty());    // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(images.size(), 0U);  // NOLINT(bugprone-use-after-move)

  const RenderOptions options = options_for(320);
  // NOLINTBEGIN(bugprone-use-after-move): ムーブ済みを渡したときの扱いが主題
  const auto without_fonts = render(kSample, fonts, options);
  ASSERT_FALSE(without_fonts.has_value());
  EXPECT_EQ(first_error(without_fonts.error()).kind, ErrorKind::InvalidOption);
  EXPECT_NE(first_error(without_fonts.error()).message.find("moved from"), std::string::npos)
      << first_error(without_fonts.error()).message;

  const auto without_images = render(kImageHtml, moved_fonts, images, options);
  ASSERT_FALSE(without_images.has_value());
  EXPECT_EQ(first_error(without_images.error()).kind, ErrorKind::InvalidOption);
  // NOLINTEND(bugprone-use-after-move)

  // ムーブ先は普通に使える。
  expect_same(render(kImageHtml, moved_fonts, moved_images, options),
              render(kImageHtml, japanese_fonts(), icon_images(), options), "moved-to");
}

// ---------------------------------------------------------------------------
// 5. prepare() と render() で RenderLimits が違うとき
// ---------------------------------------------------------------------------

// 用意するときは緩く、描くときは厳しく → render() 側の上限が効く（A34）。
TEST(Reuse, StricterLimitsAtRenderTimeStillApply) {
  RenderLimits generous;
  generous.image_pixels = std::uint64_t{1} << 24U;
  const LoadedFonts loaded_fonts = must_prepare_fonts(japanese_fonts());
  const LoadedImages loaded_images = must_prepare_images(icon_images(), generous);

  RenderOptions options = options_for(200);
  options.limits.image_pixels = 100;  // 64x64 = 4096 px は通らない

  const auto result = render(kImageHtml, loaded_fonts, loaded_images, options);
  ASSERT_FALSE(result.has_value()) << "render() 側の上限が効いていない";
  EXPECT_EQ(first_error(result.error()).kind, ErrorKind::LimitExceeded);
  EXPECT_NE(first_error(result.error()).message.find("image_pixels"), std::string::npos)
      << first_error(result.error()).message;
}

TEST(Reuse, StricterTotalPixelLimitAtRenderTimeStillApplies) {
  const LoadedFonts loaded_fonts = must_prepare_fonts(japanese_fonts());
  const LoadedImages loaded_images = must_prepare_images(icon_images());

  RenderOptions options = options_for(200);
  options.limits.total_image_pixels = 100;

  const auto result = render(kImageHtml, loaded_fonts, loaded_images, options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(first_error(result.error()).kind, ErrorKind::LimitExceeded);
  EXPECT_NE(first_error(result.error()).message.find("total_image_pixels"), std::string::npos)
      << first_error(result.error()).message;
}

// 枚数の上限（(a) の検査）も、デコード済みの共有資源に対して同じように効く。
TEST(Reuse, ImageCountLimitAppliesToPreparedImages) {
  const LoadedFonts loaded_fonts = must_prepare_fonts(japanese_fonts());
  const LoadedImages loaded_images = must_prepare_images(icon_images());

  RenderOptions options = options_for(200);
  options.limits.images = 0;

  const auto result = render(kImageHtml, loaded_fonts, loaded_images, options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(first_error(result.error()).kind, ErrorKind::LimitExceeded);
  EXPECT_NE(first_error(result.error()).message.find("RenderLimits::images"), std::string::npos)
      << first_error(result.error()).message;
}

// 用意するときに厳しければ、そこで落ちる（デコードの前に判定する = (c) の約束）。
TEST(Reuse, StricterLimitsAtPrepareTimeFailEarly) {
  RenderLimits strict;
  strict.image_pixels = 100;
  const std::expected<LoadedImages, RenderError> loaded =
      LoadedImages::prepare(icon_images(), strict);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().kind, ErrorKind::LimitExceeded);
}

}  // namespace
}  // namespace shashoku::test
