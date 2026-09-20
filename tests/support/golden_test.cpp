// ゴールデン比較ヘルパー自身のテスト。
// 期待画像はテスト用の一時ディレクトリに作り、tests/golden/ は一切触らない。

#include "support/golden.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"

namespace shashoku::test {
namespace {

// テストごとに専用のディレクトリ対を作る（tests/golden/ は一切触らない）。
GoldenOptions sandbox() {
  const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
  const std::filesystem::path root =
      std::filesystem::path(SHASHOKU_TEST_OUTPUT_DIR) / "golden_helper" / info->name();
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const GoldenOptions options{
      .golden_dir = root / "golden", .output_dir = root / "out", .update = false};
  std::filesystem::create_directories(options.golden_dir, ec);
  std::filesystem::create_directories(options.output_dir, ec);
  return options;
}

std::filesystem::path golden(const GoldenOptions& options, std::string_view name) {
  return options.golden_dir / (std::string(name) + ".png");
}

std::filesystem::path output(const GoldenOptions& options, std::string_view name) {
  return options.output_dir / (std::string(name) + ".png");
}

// AssertionResult::message() は const char* なので、find() のために std::string にする。
std::string message_of(const ::testing::AssertionResult& result) { return result.message(); }

Bitmap checkerboard(std::uint32_t w, std::uint32_t h) {
  Bitmap bitmap(w, h);
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      const bool light = (x + y) % 2 == 0;
      bitmap.set_pixel(x, y, light ? Color{220, 220, 220, 255} : Color{40, 60, 80, 128});
    }
  }
  return bitmap;
}

TEST(GoldenHelper, SucceedsOnAnExactMatch) {
  const GoldenOptions options = sandbox();
  const Bitmap image = checkerboard(8, 6);
  ASSERT_TRUE(write_png_file(golden(options, "match"), image).has_value());

  const ::testing::AssertionResult result = expect_golden(image, "match", options);
  EXPECT_TRUE(result) << result.message();
  // 一致したときは何も書き出さない
  EXPECT_FALSE(std::filesystem::exists(output(options, "match.actual")));
  EXPECT_FALSE(std::filesystem::exists(output(options, "match.diff")));
}

TEST(GoldenHelper, FailsAndWritesThreeImagesOnAMismatch) {
  const GoldenOptions options = sandbox();
  const Bitmap expected = checkerboard(8, 6);
  ASSERT_TRUE(write_png_file(golden(options, "mismatch"), expected).has_value());

  Bitmap actual = expected;
  actual.set_pixel(3, 2, Color{1, 2, 3, 4});
  actual.set_pixel(7, 5, Color{9, 9, 9, 255});

  const ::testing::AssertionResult result = expect_golden(actual, "mismatch", options);
  ASSERT_FALSE(result);
  const std::string message = result.message();
  EXPECT_NE(message.find("golden mismatch"), std::string::npos) << message;
  // 最初の不一致座標（行優先で先に来るのは (3, 2)）と不一致数
  EXPECT_NE(message.find("(3, 2)"), std::string::npos) << message;
  EXPECT_NE(message.find("2 of 48 pixel(s) differ"), std::string::npos) << message;

  EXPECT_TRUE(std::filesystem::exists(output(options, "mismatch.actual")));
  EXPECT_TRUE(std::filesystem::exists(output(options, "mismatch.expected")));
  EXPECT_TRUE(std::filesystem::exists(output(options, "mismatch.diff")));

  // 書き出した actual / expected はそれぞれ元の画像に戻る
  const Result<Bitmap> written_actual = read_png_file(output(options, "mismatch.actual"));
  ASSERT_TRUE(written_actual.has_value());
  EXPECT_EQ(*written_actual, actual);
  const Result<Bitmap> written_expected = read_png_file(output(options, "mismatch.expected"));
  ASSERT_TRUE(written_expected.has_value());
  EXPECT_EQ(*written_expected, expected);

  // 差分画像は違うピクセルだけが赤
  const Result<Bitmap> written_diff = read_png_file(output(options, "mismatch.diff"));
  ASSERT_TRUE(written_diff.has_value());
  EXPECT_EQ(written_diff->pixel(3, 2), (Color{255, 0, 0, 255}));
  EXPECT_EQ(written_diff->pixel(7, 5), (Color{255, 0, 0, 255}));
  EXPECT_NE(written_diff->pixel(0, 0), (Color{255, 0, 0, 255}));
  EXPECT_EQ(written_diff->pixel(0, 0).a, 255);  // 差分画像は不透明
}

TEST(GoldenHelper, FailsWhenTheGoldenImageIsMissing) {
  const GoldenOptions options = sandbox();
  const Bitmap image = checkerboard(4, 4);
  const ::testing::AssertionResult result = expect_golden(image, "absent", options);
  ASSERT_FALSE(result);
  EXPECT_NE(message_of(result).find("missing or unreadable"), std::string::npos)
      << result.message();
  EXPECT_NE(message_of(result).find("SHASHOKU_UPDATE_GOLDEN"), std::string::npos)
      << result.message();
  // 期待画像が無くても actual は残す（目で見て更新できるように）
  EXPECT_TRUE(std::filesystem::exists(output(options, "absent.actual")));
}

TEST(GoldenHelper, FailsWhenTheGoldenImageIsNotAPng) {
  const GoldenOptions options = sandbox();
  const std::vector<std::uint8_t> garbage{1, 2, 3, 4, 5};
  ASSERT_TRUE(write_file(golden(options, "garbage"), garbage).has_value());

  const ::testing::AssertionResult result = expect_golden(checkerboard(2, 2), "garbage", options);
  ASSERT_FALSE(result);
  EXPECT_NE(message_of(result).find("signature"), std::string::npos) << result.message();
}

TEST(GoldenHelper, FailsWhenTheSizeDiffers) {
  const GoldenOptions options = sandbox();
  ASSERT_TRUE(write_png_file(golden(options, "size"), checkerboard(4, 4)).has_value());

  const ::testing::AssertionResult result = expect_golden(checkerboard(4, 5), "size", options);
  ASSERT_FALSE(result);
  EXPECT_NE(message_of(result).find("expected 4x4"), std::string::npos) << result.message();
  EXPECT_NE(message_of(result).find("actual 4x5"), std::string::npos) << result.message();
  EXPECT_NE(message_of(result).find("(0, 4)"), std::string::npos) << result.message();
  EXPECT_NE(message_of(result).find("outside the image"), std::string::npos) << result.message();

  // 差分画像は大きい方の寸法で、はみ出した行はすべて赤
  const Result<Bitmap> diff = read_png_file(output(options, "size.diff"));
  ASSERT_TRUE(diff.has_value());
  EXPECT_EQ(diff->width, 4U);
  EXPECT_EQ(diff->height, 5U);
  EXPECT_EQ(diff->pixel(0, 4), (Color{255, 0, 0, 255}));
}

TEST(GoldenHelper, UpdateModeWritesTheGoldenImageAndSucceeds) {
  const GoldenOptions options = sandbox();
  GoldenOptions updating = options;
  updating.update = true;

  const Bitmap image = checkerboard(5, 3);
  const ::testing::AssertionResult result = expect_golden(image, "created", updating);
  EXPECT_TRUE(result) << result.message();
  ASSERT_TRUE(std::filesystem::exists(golden(options, "created")));

  const Result<Bitmap> written = read_png_file(golden(options, "created"));
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, image);

  // 更新したあとは通常モードでも一致する
  EXPECT_TRUE(expect_golden(image, "created", options));
}

TEST(GoldenHelper, UpdateModeOverwritesAnExistingGoldenImage) {
  const GoldenOptions options = sandbox();
  ASSERT_TRUE(write_png_file(golden(options, "overwrite"), checkerboard(4, 4)).has_value());

  GoldenOptions updating = options;
  updating.update = true;
  const Bitmap replacement = checkerboard(6, 2);
  EXPECT_TRUE(expect_golden(replacement, "overwrite", updating));

  const Result<Bitmap> written = read_png_file(golden(options, "overwrite"));
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(*written, replacement);
}

TEST(GoldenHelper, DefaultOptionsPointAtTheRepositoryGoldenDirectory) {
  const GoldenOptions defaults = default_golden_options();
  EXPECT_EQ(defaults.golden_dir.filename(), "golden");
  EXPECT_EQ(defaults.output_dir.filename(), "test_output");
  // 環境変数を立てていないテスト実行では更新モードにならない
  EXPECT_FALSE(defaults.update);
}

}  // namespace
}  // namespace shashoku::test
