#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "png/crc32.hpp"
#include "shashoku/shashoku.hpp"
#include "support/failure.hpp"

// 入力の上限（ARCHITECTURE.md A25 / issue #6）。
//
// 各上限について「ちょうど = 通る / 1 つ超える = LimitExceeded」を確かめる。上限は
// 入力の一部なので、境界のテストは既定値ではなく小さい値を入れて書ける（既定値そのものは
// DefaultsAreTheDocumentedOnes が押さえる）。
namespace shashoku::test {
namespace {

// 上限の検査はフォントを見るより前に済むので、ほとんどのケースは空の FontSet で
// dump() を通せる（速い）。
RenderOptions limited(int width = 320) {
  RenderOptions options;
  options.viewport_width = width;
  return options;
}

RenderError dump_failure(std::string_view html, const RenderOptions& options, DumpStage stage,
                         const FontSet& fonts = FontSet{}, const ImageSet& images = ImageSet{}) {
  const auto result = dump(html, fonts, images, options, stage);
  if (result) {
    ADD_FAILURE() << "エラーになるはずが成功した: " << html.substr(0, 64);
    return RenderError{};
  }
  return first_error(result.error());
}

void expect_dump_ok(std::string_view html, const RenderOptions& options, DumpStage stage,
                    const FontSet& fonts = FontSet{}) {
  const auto result = dump(html, fonts, ImageSet{}, options, stage);
  EXPECT_TRUE(result.has_value()) << (result ? std::string{} : to_string(result.error())) << " / "
                                  << html.substr(0, 64);
}

std::string repeat(std::string_view unit, std::size_t times) {
  std::string out;
  out.reserve(unit.size() * times);
  for (std::size_t i = 0; i < times; ++i) {
    out += unit;
  }
  return out;
}

// ---------------------------------------------------------------------------
// 既定値（変えるときは ARCHITECTURE.md A25 の表も直すこと）
// ---------------------------------------------------------------------------

TEST(RenderLimitsDefaults, DefaultsAreTheDocumentedOnes) {
  const RenderLimits limits;
  EXPECT_EQ(limits.html_bytes, 4U * 1024U * 1024U);
  EXPECT_EQ(limits.images, 64U);
  EXPECT_EQ(limits.nesting_depth, 256U);
  EXPECT_EQ(limits.dom_nodes, 20000U);
  EXPECT_EQ(limits.text_code_points, 50000U);
  EXPECT_EQ(limits.style_rules, 2000U);
  EXPECT_EQ(limits.font_size_device_px, 2048.0F);
  EXPECT_EQ(limits.length_px, 16777216.0F);  // 2^24
  EXPECT_EQ(limits.scale, 256.0F);
  EXPECT_EQ(limits.image_pixels, std::uint64_t{1} << 24U);
  EXPECT_EQ(limits.total_image_pixels, std::uint64_t{1} << 25U);
  EXPECT_EQ(limits.device_pixels, std::uint64_t{1} << 26U);
  EXPECT_EQ(RenderOptions{}.limits, limits);
}

// ---------------------------------------------------------------------------
// (a) 入力を受けた時点
// ---------------------------------------------------------------------------

TEST(RenderLimitsInput, HtmlBytes) {
  const std::string_view html = "<div>あ</div>";  // 14 バイト（あ は 3 バイト）
  ASSERT_EQ(html.size(), 14U);

  RenderOptions options = limited();
  options.limits.html_bytes = html.size();
  expect_dump_ok(html, options, DumpStage::Dom);

  options.limits.html_bytes = html.size() - 1;
  const RenderError error = dump_failure(html, options, DumpStage::Dom);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("14 bytes"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("the limit of 13"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::html_bytes"), std::string::npos) << error.message;
}

// パースより前に弾く: 壊れた HTML でも先に上限のエラーが返る。
TEST(RenderLimitsInput, HtmlBytesIsCheckedBeforeParsing) {
  RenderOptions options = limited();
  options.limits.html_bytes = 4;
  const RenderError error = dump_failure("<div><p>あ</div>", options, DumpStage::Dom);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("RenderLimits::html_bytes"), std::string::npos) << error.message;
}

TEST(RenderLimitsInput, ImageCount) {
  ImageSet images;
  for (int i = 0; i < 3; ++i) {
    images.add("icon" + std::to_string(i), test_icon_bytes());
  }

  RenderOptions options = limited();
  options.limits.images = 3;
  const auto ok = dump("<div>あ</div>", japanese_fonts(), images, options, DumpStage::Box);
  EXPECT_TRUE(ok.has_value()) << (ok ? std::string{} : to_string(ok.error()));

  options.limits.images = 2;
  const RenderError error =
      dump_failure("<div>あ</div>", options, DumpStage::Box, japanese_fonts(), images);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("3 images"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::images"), std::string::npos) << error.message;
}

// ---------------------------------------------------------------------------
// (b) 構造と計算値
// ---------------------------------------------------------------------------

TEST(RenderLimitsStructure, NestingDepth) {
  const auto nested = [](std::size_t depth) {
    return repeat("<div>", depth) + repeat("</div>", depth);
  };

  RenderOptions options = limited();
  options.limits.nesting_depth = 8;
  expect_dump_ok(nested(8), options, DumpStage::Dom);

  const RenderError error = dump_failure(nested(9), options, DumpStage::Dom);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("the maximum is 8"), std::string::npos) << error.message;
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 41U);  // 9 個目の `<div>`
}

TEST(RenderLimitsStructure, DomNodes) {
  // `<div></div>` が 4 つ = 要素 4 ノード（合成ルートは数えない）。
  const std::string html = repeat("<div></div>", 4);

  RenderOptions options = limited();
  options.limits.dom_nodes = 4;
  expect_dump_ok(html, options, DumpStage::Dom);

  options.limits.dom_nodes = 3;
  const RenderError error = dump_failure(html, options, DumpStage::Dom);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("4 nodes"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("the limit of 3"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::dom_nodes"), std::string::npos) << error.message;
  // 上限を超えた 4 つ目の `<div>`（1 組 11 桁）
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 34U);
}

// テキストノードも 1 ノードとして数える。
TEST(RenderLimitsStructure, DomNodesCountsTextNodes) {
  RenderOptions options = limited();
  options.limits.dom_nodes = 2;  // <div> と "あ"
  expect_dump_ok("<div>あ</div>", options, DumpStage::Dom);

  options.limits.dom_nodes = 1;
  EXPECT_EQ(dump_failure("<div>あ</div>", options, DumpStage::Dom).kind, ErrorKind::LimitExceeded);
}

TEST(RenderLimitsStructure, TextCodePoints) {
  const std::string_view html = "<div>あいう</div>";  // 3 コードポイント（9 バイト）

  RenderOptions options = limited();
  options.limits.text_code_points = 3;
  expect_dump_ok(html, options, DumpStage::Dom);

  options.limits.text_code_points = 2;
  const RenderError error = dump_failure(html, options, DumpStage::Dom);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("3 text code points"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::text_code_points"), std::string::npos)
      << error.message;
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 6U);  // `あ` の位置
}

// `<style>` の中身は「組む対象のテキスト」ではないので数えない（その量は html_bytes と
// style_rules が押さえる）。
TEST(RenderLimitsStructure, TextCodePointsIgnoresStyleContent) {
  RenderOptions options = limited();
  options.limits.text_code_points = 1;
  expect_dump_ok("<style>div { color: red; }</style><div>あ</div>", options, DumpStage::Dom);
}

TEST(RenderLimitsStructure, StyleRules) {
  const std::string html = "<style>" + repeat(".a{color:red}", 5) + "</style><div>あ</div>";

  RenderOptions options = limited();
  options.limits.style_rules = 5;
  expect_dump_ok(html, options, DumpStage::Style);

  options.limits.style_rules = 4;
  const RenderError error = dump_failure(html, options, DumpStage::Style);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("5 rules"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::style_rules"), std::string::npos) << error.message;
  EXPECT_TRUE(error.location.has_value());
}

// font-size はスタイル解決で初めて px に確定する（`<rt>` の 50% も含む）。
TEST(RenderLimitsComputed, FontSizeDevicePx) {
  const std::string_view html = R"(<div style="font-size: 64px">あ</div>)";

  RenderOptions options = limited();
  options.limits.font_size_device_px = 64;
  expect_dump_ok(html, options, DumpStage::Style);

  options.limits.font_size_device_px = 63;
  const RenderError error = dump_failure(html, options, DumpStage::Style);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("font-size 64 px"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::font_size_device_px"), std::string::npos)
      << error.message;
  ASSERT_TRUE(error.location.has_value());
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 1U);  // `<div>` の位置
}

// デバイスピクセルなので scale が掛かる。
TEST(RenderLimitsComputed, FontSizeIsMeasuredInDevicePixels) {
  const std::string_view html = R"(<div style="font-size: 64px">あ</div>)";

  RenderOptions options = limited();
  options.limits.font_size_device_px = 128;
  options.scale = 2.0F;
  expect_dump_ok(html, options, DumpStage::Style);

  options.scale = 2.5F;  // 64 * 2.5 = 160 > 128
  const RenderError error = dump_failure(html, options, DumpStage::Style);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("160"), std::string::npos) << error.message;
}

// ルビ（`<rt>` は UA スタイルで親の 50%）も、計算値なのでそのまま対象になる。
TEST(RenderLimitsComputed, FontSizeCoversRubyText) {
  const std::string_view html =
      R"(<div style="font-size: 40px"><ruby>東<rt style="font-size: 200px">とう</rt></ruby></div>)";

  RenderOptions options = limited();
  options.limits.font_size_device_px = 100;
  const RenderError error = dump_failure(html, options, DumpStage::Style);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("font-size 200 px"), std::string::npos) << error.message;
  ASSERT_TRUE(error.location.has_value());
  // `<rt>` の位置（親の `<div>` ではない）
  EXPECT_GT(error.location.value_or(SourceLocation{}).column, 30U);
}

TEST(RenderLimitsComputed, Scale) {
  RenderOptions options = limited();
  options.limits.scale = 4.0F;
  options.scale = 4.0F;
  expect_dump_ok("<div>あ</div>", options, DumpStage::Dom);

  options.scale = 4.5F;
  const RenderError error = dump_failure("<div>あ</div>", options, DumpStage::Dom);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("RenderLimits::scale"), std::string::npos) << error.message;
}

// ---------------------------------------------------------------------------
// (c) 大きな確保の直前
// ---------------------------------------------------------------------------

void push_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24U));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void push_chunk(std::vector<std::uint8_t>& out, std::string_view type,
                std::span<const std::uint8_t> data) {
  push_be32(out, static_cast<std::uint32_t>(data.size()));
  std::vector<std::uint8_t> typed;
  for (const char c : type) {
    typed.push_back(static_cast<std::uint8_t>(c));
  }
  typed.insert(typed.end(), data.begin(), data.end());
  out.insert(out.end(), typed.begin(), typed.end());
  push_be32(out, png::crc32(typed));
}

// IHDR で巨大な寸法を名乗るだけの、70 バイト足らずの PNG。
// デコーダは IHDR を読んだ時点で拒否するので、画素は 1 バイトも確保されない（ASan で確認）。
std::vector<std::uint8_t> png_claiming_size(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> out{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<std::uint8_t> ihdr;
  push_be32(ihdr, width);
  push_be32(ihdr, height);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(6);  // color type: RGBA
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  push_chunk(out, "IHDR", ihdr);
  const std::vector<std::uint8_t> idat{0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01};
  push_chunk(out, "IDAT", idat);
  push_chunk(out, "IEND", {});
  return out;
}

TEST(RenderLimitsAllocation, ImagePixelsIsCheckedBeforeAllocating) {
  ImageSet images;
  const std::vector<std::uint8_t> bytes = png_claiming_size(50000, 50000);  // 2.5e9 px
  ASSERT_LT(bytes.size(), 128U);  // 入力そのものは小さい

  images.add("huge", bytes);
  const RenderError error =
      dump_failure(R"(<img src="huge">)", limited(), DumpStage::Box, japanese_fonts(), images);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("50000x50000"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::image_pixels"), std::string::npos) << error.message;
}

TEST(RenderLimitsAllocation, ImagePixelsBoundary) {
  ImageSet images;
  images.add("icon", test_icon_bytes());  // 64x64 = 4096 px

  RenderOptions options = limited();
  options.limits.image_pixels = 4096;
  const auto ok = dump(R"(<img src="icon">)", japanese_fonts(), images, options, DumpStage::Box);
  EXPECT_TRUE(ok.has_value()) << (ok ? std::string{} : to_string(ok.error()));

  options.limits.image_pixels = 4095;
  const RenderError error =
      dump_failure(R"(<img src="icon">)", options, DumpStage::Box, japanese_fonts(), images);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("the limit is 4095"), std::string::npos) << error.message;
  EXPECT_NE(error.message.find("RenderLimits::image_pixels"), std::string::npos) << error.message;
}

// 1 枚ずつは上限内でも、合計で超えたら止まる（「1 枚 256MB x 枚数無制限」を塞ぐ）。
TEST(RenderLimitsAllocation, TotalImagePixels) {
  ImageSet images;
  images.add("a", test_icon_bytes());  // 4096 px
  images.add("b", test_icon_bytes());  // 4096 px

  RenderOptions options = limited();
  options.limits.image_pixels = 4096;
  options.limits.total_image_pixels = 8192;
  const auto ok =
      dump(R"(<img src="a"><img src="b">)", japanese_fonts(), images, options, DumpStage::Box);
  EXPECT_TRUE(ok.has_value()) << (ok ? std::string{} : to_string(ok.error()));

  options.limits.total_image_pixels = 8191;
  const RenderError error = dump_failure(R"(<img src="a"><img src="b">)", options, DumpStage::Box,
                                         japanese_fonts(), images);
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_NE(error.message.find("RenderLimits::total_image_pixels"), std::string::npos)
      << error.message;
  // 2 枚目を確保する前に止める（1 枚目の 4096 を引いた残り 4095 が上限になる）
  EXPECT_NE(error.message.find("the limit is 4095"), std::string::npos) << error.message;
}

TEST(RenderLimitsAllocation, DevicePixels) {
  RenderOptions options = limited(40);
  options.viewport_height = 10;
  options.limits.device_pixels = 400;
  const auto ok = render("<div>あ</div>", japanese_fonts(), options);
  ASSERT_TRUE(ok.has_value()) << (ok ? std::string{} : to_string(ok.error()));
  EXPECT_EQ(ok->width, 40);
  EXPECT_EQ(ok->height, 10);

  options.limits.device_pixels = 399;
  const auto over = render("<div>あ</div>", japanese_fonts(), options);
  ASSERT_FALSE(over.has_value());
  EXPECT_EQ(first_error(over.error()).kind, ErrorKind::LimitExceeded);
  EXPECT_NE(first_error(over.error()).message.find("40 x 10 = 400"), std::string::npos)
      << first_error(over.error()).message;
  EXPECT_NE(first_error(over.error()).message.find("RenderLimits::device_pixels"),
            std::string::npos)
      << first_error(over.error()).message;
}

// ---------------------------------------------------------------------------
// issue #6 の実測ケース
// ---------------------------------------------------------------------------

// 出力 100x100 に font-size: 30000px の 1 文字。以前は 1.2 GB を確保して成功していた。
TEST(RenderLimitsRegression, HugeFontSizeOnATinyCanvasIsRejected) {
  RenderOptions options = limited(100);
  options.viewport_height = 100;
  const auto result =
      render(R"(<div style="font-size:30000px;line-height:1">あ</div>)", japanese_fonts(), options);
  ASSERT_FALSE(result.has_value()) << "既定の上限で止まるはず";
  EXPECT_EQ(first_error(result.error()).kind, ErrorKind::LimitExceeded);
  EXPECT_NE(first_error(result.error()).message.find("font-size 30000 px"), std::string::npos)
      << first_error(result.error()).message;
  EXPECT_TRUE(first_error(result.error()).location.has_value());
}

// 上限ぎりぎり（既定の 2048 デバイス px）は通る。グリフのビットマップは 2048^2 = 4 MB 程度。
TEST(RenderLimitsRegression, TheLargestAllowedFontSizeStillRenders) {
  RenderOptions options = limited(256);
  options.viewport_height = 256;
  const auto result =
      render(R"(<div style="font-size:2048px;line-height:1">あ</div>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->width, 256);
  EXPECT_EQ(result->height, 256);
}

// ---------------------------------------------------------------------------
// メモリ不足（ARCHITECTURE.md A26）
// ---------------------------------------------------------------------------

// 上限を意図的に外すと、出力ビットマップの確保が std::vector の最大長を超える。
// 公開関数の境界で std::length_error を捕まえて OutOfMemory を返すことを確かめる
// （length_error は確保を試みる前に投げられるので、巨大なメモリは要求されない）。
//
// ASan では動かせない: この環境の libc++ / libc++abi の組み合わせでは、標準例外を
// 1 つ投げて捕まえるだけで alloc-dealloc-mismatch が報告される（logic_error の
// メッセージを libc++.so が operator new で確保し、libc++abi.so が free で解放する）。
// shashoku のコードとは関係なく、10 行のプログラムでも同じ報告が出る。
TEST(RenderLimitsOutOfMemory, LengthErrorBecomesOutOfMemory) {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  GTEST_SKIP() << "ASan では標準例外そのものが alloc-dealloc-mismatch を報告する（環境の問題）";
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
  GTEST_SKIP() << "ASan では標準例外そのものが alloc-dealloc-mismatch を報告する（環境の問題）";
#endif

  RenderOptions options;
  options.viewport_width = std::numeric_limits<int>::max();  // 約 2^31 デバイス px
  options.viewport_height = 1 << 30U;
  options.limits.device_pixels = std::numeric_limits<std::uint64_t>::max();
  options.limits.font_size_device_px = std::numeric_limits<float>::infinity();

  const auto result = render("<div>あ</div>", japanese_fonts(), options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(first_error(result.error()).kind, ErrorKind::OutOfMemory) << to_string(result.error());
  EXPECT_NE(first_error(result.error()).message.find("out of memory"), std::string::npos)
      << first_error(result.error()).message;
}

}  // namespace
}  // namespace shashoku::test
