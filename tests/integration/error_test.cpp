#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// fail loudly（DESIGN.md §3-6）: 未対応・不正な入力は、原因の入力位置つきでエラーになる。
// 各段のエラーが公開 API まで素通しで上がってくることを確かめる。
namespace shashoku::test {
namespace {

struct ErrorCase {
  std::string_view name;
  std::string_view html;
  ErrorKind kind;
  std::string_view message_contains;
  bool has_location = true;
};

// GoogleTest の既定はパラメータをバイト列として表示する。ポインタ値が混ざって
// テスト名が実行ごとに変わってしまうので（CTest の登録名が古くなる）、名前を出す。
std::ostream& operator<<(std::ostream& out, const ErrorCase& test_case) {
  return out << test_case.name;
}

class RenderErrorCase : public ::testing::TestWithParam<ErrorCase> {};

TEST_P(RenderErrorCase, IsReported) {
  const ErrorCase& test_case = GetParam();
  const auto result = render(test_case.html, japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value()) << "エラーになるはずが成功した: " << test_case.html;
  EXPECT_EQ(result.error().kind, test_case.kind) << to_string(result.error());
  EXPECT_NE(result.error().message.find(test_case.message_contains), std::string::npos)
      << to_string(result.error());
  EXPECT_EQ(result.error().location.has_value(), test_case.has_location)
      << to_string(result.error());
}

INSTANTIATE_TEST_SUITE_P(
    Pipeline, RenderErrorCase,
    ::testing::Values(
        // ① html
        ErrorCase{"unsupported-tag", "<table><div>あ</div></table>", ErrorKind::UnsupportedTag,
                  "table"},
        ErrorCase{"unclosed", "<div><p>あ</div>", ErrorKind::HtmlParse, "p"},
        ErrorCase{"unsupported-attribute", R"(<div onclick="x">あ</div>)",
                  ErrorKind::UnsupportedAttribute, "onclick"},
        // ② style
        ErrorCase{"unsupported-property", R"(<div style="float: left">あ</div>)",
                  ErrorKind::UnsupportedProperty, "float"},
        ErrorCase{"unsupported-value", R"(<div style="display: grid">あ</div>)",
                  ErrorKind::UnsupportedValue, "grid"},
        ErrorCase{"unsupported-unit", R"(<div style="font-size: 2rem">あ</div>)",
                  ErrorKind::UnsupportedValue, "rem"},
        // ③ layout
        ErrorCase{"inline-padding", R"(<span style="padding: 4px">あ</span>)",
                  ErrorKind::UnsupportedLayout, "padding"}),
    // 引数名を `info` にすると、GoogleTest のマクロが内部で使う同名の引数と衝突して
    // gcc の -Wshadow に掛かる（clang は検出しない）。
    [](const ::testing::TestParamInfo<ErrorCase>& param_info) {
      std::string name(param_info.param.name);
      for (char& c : name) {
        if (c == '-') {
          c = '_';
        }
      }
      return name;
    });

// 不正な UTF-8 は位置つきで弾く。
TEST(RenderErrors, InvalidUtf8) {
  const std::string html = std::string("<div>") + '\xff' + "</div>";
  const auto result = render(html, japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::InvalidUtf8);
}

// ---------------------------------------------------------------------------
// フォント・画像・オプション（入力位置を持たないエラー）
// ---------------------------------------------------------------------------

TEST(RenderErrors, NoFonts) {
  const auto result = render("<div>あ</div>", FontSet{}, options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::NoFonts);
  EXPECT_FALSE(result.error().location.has_value());
}

TEST(RenderErrors, BrokenFontSaysWhichOne) {
  FontSet fonts;
  fonts.add(text::assets::noto_sans_jp_regular());
  const std::vector<std::uint8_t> garbage(64, 0x7F);
  fonts.add(garbage);

  const auto result = render("<div>あ</div>", fonts, options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::FontLoad);
  // 何番目のフォントが壊れているかを言う
  EXPECT_NE(result.error().message.find("font #1"), std::string::npos) << result.error().message;
}

TEST(RenderErrors, BrokenImageSaysWhichOne) {
  ImageSet images;
  const std::vector<std::uint8_t> garbage(32, 0x00);
  images.add("broken", garbage);

  const auto result = render(R"(<img src="broken">)", japanese_fonts(), images, options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::ImageDecode);
  EXPECT_NE(result.error().message.find("broken"), std::string::npos) << result.error().message;
}

// ImageSet に無い名前は ImageNotFound（A12）。黙って空白を描いたりしない。
TEST(RenderErrors, UnknownImageName) {
  const auto result =
      render(R"(<img src="missing">)", japanese_fonts(), ImageSet{}, options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::ImageNotFound) << to_string(result.error());
  EXPECT_NE(result.error().message.find("missing"), std::string::npos) << result.error().message;
  EXPECT_TRUE(result.error().location.has_value());
}

TEST(RenderErrors, DuplicateImageName) {
  ImageSet images;
  const std::vector<std::uint8_t> bytes(8, 0x00);
  images.add("icon", bytes);
  images.add("icon", bytes);

  const auto result = render("<div>あ</div>", japanese_fonts(), images, options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(result.error().message.find("icon"), std::string::npos) << result.error().message;
}

struct OptionCase {
  std::string_view name;
  RenderOptions options;
  std::string_view message_contains;
  ErrorKind kind = ErrorKind::InvalidOption;
};

TEST(RenderErrors, InvalidOptions) {
  const auto make = [](int width, int height, float scale) {
    RenderOptions options;
    options.viewport_width = width;
    if (height != 0) {
      options.viewport_height = height;
    }
    options.scale = scale;
    return options;
  };
  const auto with_level = [](int level) {
    RenderOptions options;
    options.viewport_width = 320;
    options.compression_level = level;
    return options;
  };
  const std::vector<OptionCase> cases{
      {"zero-width", make(0, 0, 1.0F), "viewport width"},
      {"negative-width", make(-100, 0, 1.0F), "viewport width"},
      {"zero-height", make(320, -1, 1.0F), "viewport height"},
      {"zero-scale", make(320, 0, 0.0F), "scale"},
      {"negative-scale", make(320, 0, -2.0F), "scale"},
      // scale の上限だけは「不正な値」ではなく RenderLimits::scale の超過（A25）。
      {"huge-scale", make(320, 0, 1e9F), "scale", ErrorKind::LimitExceeded},
      {"nan-scale", make(320, 0, std::numeric_limits<float>::quiet_NaN()), "scale"},
      {"inf-scale", make(320, 0, std::numeric_limits<float>::infinity()), "scale"},
      // 圧縮レベルは 0〜9（A33）。範囲外は「不正な値」なので InvalidOption。
      {"level-below-range", with_level(-1), "compression level"},
      {"level-above-range", with_level(10), "compression level"},
  };
  for (const OptionCase& test_case : cases) {
    const auto result = render("<div>あ</div>", japanese_fonts(), test_case.options);
    ASSERT_FALSE(result.has_value()) << test_case.name;
    EXPECT_EQ(result.error().kind, test_case.kind) << test_case.name;
    EXPECT_NE(result.error().message.find(test_case.message_contains), std::string::npos)
        << test_case.name << ": " << result.error().message;
  }
}

// エラーの書式（error.hpp のコメント）: 位置があれば "at 行:桁" が入る。
TEST(RenderErrors, ToStringIncludesLocation) {
  const auto result =
      render("<div>\n<float>あ</float>\n</div>", japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  const std::string text = to_string(result.error());
  EXPECT_TRUE(text.starts_with("error[")) << text;
  EXPECT_NE(text.find(" at 2:"), std::string::npos) << text;
}

// ダンプでも同じエラーが返る（段が違っても経路は 1 本）。
TEST(RenderErrors, DumpPropagatesTheSameError) {
  const auto result = dump(R"(<div style="float: left">あ</div>)", japanese_fonts(), ImageSet{},
                           options_for(320), DumpStage::Box);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::UnsupportedProperty);
}

}  // namespace
}  // namespace shashoku::test
