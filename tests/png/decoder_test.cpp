// decode は信頼できない入力を受ける（<img> とゴールデン画像）。
// 前半は「正しく読めること」（各 color type / bit depth / tRNS / 補助チャンク）、
// 後半は「どんなバイト列でも落ちずにエラーを返すこと」（ARCHITECTURE.md §3.2）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/png.hpp"
#include "png/png_test_util.hpp"
#include "shashoku/error.hpp"

namespace shashoku::png {
namespace {

using test::build_png;
using test::ImageSpec;
using test::with_none_filter;

Bitmap decode_or_die(std::span<const std::uint8_t> bytes) {
  Result<Bitmap> decoded = decode(bytes);
  EXPECT_TRUE(decoded.has_value())
      << (decoded.has_value() ? std::string{} : to_string(decoded.error()));
  return decoded.value_or(Bitmap{});
}

// 失敗すること、その message に手掛かり（needle）が入っていることを確かめる。
void expect_decode_error(std::span<const std::uint8_t> bytes, std::string_view needle,
                         const char* what) {
  const Result<Bitmap> decoded = decode(bytes);
  ASSERT_FALSE(decoded.has_value()) << what << ": expected a failure";
  EXPECT_EQ(decoded.error().kind, ErrorKind::ImageDecode) << what;
  EXPECT_NE(decoded.error().message.find(needle), std::string::npos)
      << what << ": message was \"" << decoded.error().message << "\"";
}

// ---- 各 color type / bit depth ---------------------------------------------

TEST(PngDecode, Grayscale8) {
  ImageSpec spec;
  spec.width = 3;
  spec.height = 2;
  spec.color_type = 0;
  const std::vector<std::uint8_t> pixels{0, 128, 255, 10, 20, 30};
  spec.raw = with_none_filter(pixels, 3, 2);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  ASSERT_EQ(bitmap.width, 3U);
  ASSERT_EQ(bitmap.height, 2U);
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{0, 0, 0, 255}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{128, 128, 128, 255}));
  EXPECT_EQ(bitmap.pixel(2, 0), (Color{255, 255, 255, 255}));
  EXPECT_EQ(bitmap.pixel(0, 1), (Color{10, 10, 10, 255}));
}

TEST(PngDecode, Grayscale16TakesTheHighByte) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.color_type = 0;
  spec.bit_depth = 16;
  const std::vector<std::uint8_t> pixels{0xAB, 0xCD, 0x00, 0xFF};
  spec.raw = with_none_filter(pixels, 4, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{0xAB, 0xAB, 0xAB, 255}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{0x00, 0x00, 0x00, 255}));
}

TEST(PngDecode, GrayscaleWithAlpha8) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.color_type = 4;
  const std::vector<std::uint8_t> pixels{50, 255, 200, 0};
  spec.raw = with_none_filter(pixels, 4, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{50, 50, 50, 255}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{200, 200, 200, 0}));
}

TEST(PngDecode, GrayscaleWithAlpha16) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 4;
  spec.bit_depth = 16;
  const std::vector<std::uint8_t> pixels{0x12, 0x34, 0x80, 0x00};
  spec.raw = with_none_filter(pixels, 4, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{0x12, 0x12, 0x12, 0x80}));
}

TEST(PngDecode, Truecolor8) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 2;
  spec.color_type = 2;
  const std::vector<std::uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  spec.raw = with_none_filter(pixels, 6, 2);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{1, 2, 3, 255}));
  EXPECT_EQ(bitmap.pixel(1, 1), (Color{10, 11, 12, 255}));
}

TEST(PngDecode, Truecolor16) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 2;
  spec.bit_depth = 16;
  const std::vector<std::uint8_t> pixels{0x11, 0xFF, 0x22, 0xFF, 0x33, 0xFF};
  spec.raw = with_none_filter(pixels, 6, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{0x11, 0x22, 0x33, 255}));
}

TEST(PngDecode, Rgba16) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 6;
  spec.bit_depth = 16;
  const std::vector<std::uint8_t> pixels{0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00};
  spec.raw = with_none_filter(pixels, 8, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{1, 2, 3, 4}));
}

TEST(PngDecode, Palette8) {
  ImageSpec spec;
  spec.width = 4;
  spec.height = 1;
  spec.color_type = 3;
  spec.palette = {255, 0, 0, 0, 255, 0, 0, 0, 255};
  const std::vector<std::uint8_t> pixels{0, 1, 2, 0};
  spec.raw = with_none_filter(pixels, 4, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{255, 0, 0, 255}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{0, 255, 0, 255}));
  EXPECT_EQ(bitmap.pixel(2, 0), (Color{0, 0, 255, 255}));
}

// ---- tRNS -------------------------------------------------------------------

TEST(PngDecode, PaletteWithTrns) {
  ImageSpec spec;
  spec.width = 3;
  spec.height = 1;
  spec.color_type = 3;
  spec.palette = {255, 0, 0, 0, 255, 0, 0, 0, 255};
  spec.has_trns = true;
  spec.trns = {0, 128};  // 3 番目は tRNS に無いので不透明
  const std::vector<std::uint8_t> pixels{0, 1, 2};
  spec.raw = with_none_filter(pixels, 3, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{255, 0, 0, 0}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{0, 255, 0, 128}));
  EXPECT_EQ(bitmap.pixel(2, 0), (Color{0, 0, 255, 255}));
}

TEST(PngDecode, GrayscaleWithTrns) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.color_type = 0;
  spec.has_trns = true;
  spec.trns = {0x00, 0x80};  // 16bit で表された「値 128 のサンプルは透明」
  const std::vector<std::uint8_t> pixels{128, 129};
  spec.raw = with_none_filter(pixels, 2, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{128, 128, 128, 0}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{129, 129, 129, 255}));
}

TEST(PngDecode, TruecolorWithTrns) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.color_type = 2;
  spec.has_trns = true;
  spec.trns = {0x00, 0x10, 0x00, 0x20, 0x00, 0x30};
  const std::vector<std::uint8_t> pixels{0x10, 0x20, 0x30, 0x10, 0x20, 0x31};
  spec.raw = with_none_filter(pixels, 6, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{0x10, 0x20, 0x30, 0}));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{0x10, 0x20, 0x31, 255}));
}

TEST(PngDecode, Trns16BitComparesTheFullSample) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.color_type = 0;
  spec.bit_depth = 16;
  spec.has_trns = true;
  spec.trns = {0x12, 0x34};
  // 上位バイトは同じだが下位が違うので、透明になるのは片方だけ
  const std::vector<std::uint8_t> pixels{0x12, 0x34, 0x12, 0x35};
  spec.raw = with_none_filter(pixels, 4, 1);

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0).a, 0);
  EXPECT_EQ(bitmap.pixel(1, 0).a, 255);
}

// ---- フィルタの復元 ---------------------------------------------------------

// PNG 仕様 §9.4 の PaethPredictor（デコーダとは独立にテスト側で書く）。
int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

// RGBA8 の画像を、全行を同じ filter で符号化した「展開後の IDAT」にする。
std::vector<std::uint8_t> filter_all_rows(const Bitmap& image, std::uint8_t filter) {
  const std::size_t stride = std::size_t{image.width} * 4;
  std::vector<std::uint8_t> raw;
  raw.reserve((stride + 1) * image.height);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    raw.push_back(filter);
    for (std::size_t i = 0; i < stride; ++i) {
      const std::size_t at = (std::size_t{y} * stride) + i;
      const int a = i >= 4 ? int{image.rgba[at - 4]} : 0;
      const int b = y > 0 ? int{image.rgba[at - stride]} : 0;
      const int c = (i >= 4 && y > 0) ? int{image.rgba[at - stride - 4]} : 0;
      int pred = 0;
      switch (filter) {
        case 1:
          pred = a;
          break;
        case 2:
          pred = b;
          break;
        case 3:
          pred = (a + b) / 2;
          break;
        case 4:
          pred = paeth(a, b, c);
          break;
        default:
          pred = 0;
          break;
      }
      raw.push_back(static_cast<std::uint8_t>(int{image.rgba[at]} - pred));
    }
  }
  return raw;
}

TEST(PngDecode, ReconstructsEveryFilterType) {
  // 同じ画像を 5 種のフィルタで符号化した IDAT を作り、どれも同じ絵に戻ることを見る。
  const Bitmap original = test::make_gradient(5, 4);
  for (std::uint8_t filter = 0; filter <= 4; ++filter) {
    ImageSpec spec;
    spec.width = original.width;
    spec.height = original.height;
    spec.raw = filter_all_rows(original, filter);
    const Bitmap decoded = decode_or_die(build_png(spec));
    EXPECT_EQ(decoded, original) << "filter " << int{filter};
  }
}

// ---- チャンクの扱い ---------------------------------------------------------

TEST(PngDecode, SkipsAncillaryChunks) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{9, 8, 7, 6}, 4, 1);
  spec.before_idat = {{"gAMA", {0, 1, 0x86, 0xA0}},
                      {"pHYs", {0, 0, 0x0B, 0x13, 0, 0, 0x0B, 0x13, 1}},
                      {"tEXt", {'a', 0, 'b'}}};
  spec.after_idat = {{"tIME", {0x07, 0xEA, 9, 19, 12, 0, 0}}, {"zzZz", {1, 2, 3}}};

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(0, 0), (Color{9, 8, 7, 6}));
}

TEST(PngDecode, JoinsMultipleIdatChunks) {
  ImageSpec spec;
  spec.width = 8;
  spec.height = 8;
  spec.raw = with_none_filter(test::make_gradient(8, 8).rgba, 32, 8);
  spec.idat_pieces = 5;

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap, test::make_gradient(8, 8));
}

TEST(PngDecode, AcceptsAnEmptyIdatAmongOthers) {
  // 長さ 0 の IDAT は合法。連結した結果が正しければよい。
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}, 8, 1);
  spec.idat_pieces = 30;  // 中身より多く割れば長さ 0 の IDAT が混ざる

  const Bitmap bitmap = decode_or_die(build_png(spec));
  EXPECT_EQ(bitmap.pixel(1, 0), (Color{5, 6, 7, 8}));
}

// ---- 対応外の形式 -----------------------------------------------------------

TEST(PngDecode, RejectsInterlaced) {
  ImageSpec spec;
  spec.interlace = 1;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  expect_decode_error(build_png(spec), "interlaced", "interlace=1");
}

TEST(PngDecode, RejectsLowBitDepths) {
  for (const std::uint8_t depth : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{4}}) {
    ImageSpec spec;
    spec.color_type = 0;
    spec.bit_depth = depth;
    spec.raw = {0, 0};
    expect_decode_error(build_png(spec), "is not supported", "low bit depth");
  }
}

TEST(PngDecode, RejectsInvalidBitDepthForColorType) {
  ImageSpec spec;
  spec.color_type = 2;  // truecolor は 8 か 16 のみ
  spec.bit_depth = 4;
  spec.raw = {0, 0};
  expect_decode_error(build_png(spec), "invalid for color type", "rgb/4bit");

  ImageSpec palette16;
  palette16.color_type = 3;
  palette16.bit_depth = 16;
  palette16.palette = {1, 2, 3};
  palette16.raw = {0, 0};
  expect_decode_error(build_png(palette16), "invalid for color type", "palette/16bit");
}

TEST(PngDecode, RejectsUnknownColorType) {
  ImageSpec spec;
  spec.color_type = 5;
  spec.raw = {0, 0};
  expect_decode_error(build_png(spec), "invalid color type", "color type 5");
}

TEST(PngDecode, RejectsUnknownCompressionAndFilterMethods) {
  ImageSpec compression;
  compression.compression = 1;
  compression.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  expect_decode_error(build_png(compression), "compression method", "compression=1");

  ImageSpec filter;
  filter.filter_method = 1;
  filter.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  expect_decode_error(build_png(filter), "filter method", "filter method=1");
}

// ---- 壊れた入力 -------------------------------------------------------------

TEST(PngDecode, RejectsEmptyAndShortInput) {
  expect_decode_error({}, "signature", "empty");
  const std::vector<std::uint8_t> three{0x89, 0x50, 0x4E};
  expect_decode_error(three, "signature", "3 bytes");
}

// 1x1 の RGBA な最小の PNG。壊し方のテストの土台にする。
std::vector<std::uint8_t> minimal_png() {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  return build_png(spec);
}

TEST(PngDecode, RejectsWrongSignature) {
  std::vector<std::uint8_t> png = minimal_png();
  png[1] = 'X';
  expect_decode_error(png, "signature", "corrupt signature");
}

TEST(PngDecode, RejectsTruncationAtEveryLength) {
  ImageSpec spec;
  spec.width = 4;
  spec.height = 4;
  spec.raw = with_none_filter(test::make_gradient(4, 4).rgba, 16, 4);
  const std::vector<std::uint8_t> png = build_png(spec);

  // どこで切っても落ちずにエラーになる
  for (std::size_t length = 0; length < png.size(); ++length) {
    const Result<Bitmap> decoded = decode(std::span<const std::uint8_t>(png).first(length));
    ASSERT_FALSE(decoded.has_value()) << "truncated to " << length << " byte(s)";
    EXPECT_EQ(decoded.error().kind, ErrorKind::ImageDecode) << "truncated to " << length;
  }
  EXPECT_TRUE(decode(png).has_value());
}

TEST(PngDecode, RejectsTrailingBytesAfterIend) {
  std::vector<std::uint8_t> png = minimal_png();
  png.push_back(0);
  expect_decode_error(png, "follow the IEND", "trailing byte");
}

TEST(PngDecode, RejectsMissingIend) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  spec.write_iend = false;
  expect_decode_error(build_png(spec), "IEND", "no IEND");
}

TEST(PngDecode, RejectsNonEmptyIend) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  spec.write_iend = false;
  std::vector<std::uint8_t> png = build_png(spec);
  test::append_chunk(png, "IEND", std::vector<std::uint8_t>{0});
  expect_decode_error(png, "IEND must be empty", "IEND with data");
}

TEST(PngDecode, RejectsAChunkBeforeIhdr) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  const std::vector<std::uint8_t> png = build_png(spec);

  std::vector<std::uint8_t> reordered(png.begin(), png.begin() + 8);
  test::append_chunk(reordered, "gAMA", std::vector<std::uint8_t>{0, 1, 0x86, 0xA0});
  reordered.insert(reordered.end(), png.begin() + 8, png.end());
  expect_decode_error(reordered, "first chunk must be IHDR", "gAMA first");
}

TEST(PngDecode, RejectsDuplicateIhdr) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  spec.before_idat = {{"IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0}}};
  expect_decode_error(build_png(spec), "duplicate IHDR", "two IHDRs");
}

TEST(PngDecode, RejectsNonConsecutiveIdat) {
  ImageSpec spec;
  spec.width = 4;
  spec.height = 4;
  spec.raw = with_none_filter(test::make_gradient(4, 4).rgba, 16, 4);
  spec.idat_pieces = 2;
  spec.between_idat = {{"tEXt", {'x', 0, 'y'}}};
  expect_decode_error(build_png(spec), "not consecutive", "IDAT tEXt IDAT");
}

TEST(PngDecode, RejectsMissingIdat) {
  // build_png は長さ 0 でも IDAT を書くので、IDAT を含まない PNG は手で組む
  std::vector<std::uint8_t> without(kSignature.begin(), kSignature.end());
  test::append_chunk(without, "IHDR",
                     std::vector<std::uint8_t>{0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0});
  test::append_chunk(without, "IEND", {});
  expect_decode_error(without, "no IDAT", "IHDR + IEND only");
}

TEST(PngDecode, RejectsCrcMismatch) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  std::vector<std::uint8_t> png = build_png(spec);
  // IHDR の CRC の最後のバイト（シグネチャ 8 + 長さ 4 + 型 4 + データ 13 + CRC 4）
  png[8 + 4 + 4 + 13 + 3] ^= 0xFFU;
  expect_decode_error(png, "CRC mismatch", "broken IHDR CRC");
}

TEST(PngDecode, RejectsAnInvalidChunkType) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  spec.before_idat = {{"1234", {1}}};
  expect_decode_error(build_png(spec), "four ASCII letters", "digits in chunk type");
}

TEST(PngDecode, RejectsUnknownCriticalChunk) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  spec.before_idat = {{"ABCD", {1, 2}}};
  expect_decode_error(build_png(spec), "unknown critical chunk", "critical ABCD");
}

// 画素数の上限は「壊れた PNG」ではなく「大きすぎる入力」なので LimitExceeded（A25）。
// IHDR を読んだ時点で判定するので、この 65535x65535 は 1 バイトも確保されない。
TEST(PngDecode, RejectsHugeDimensions) {
  ImageSpec spec;
  spec.width = 0xFFFF;
  spec.height = 0xFFFF;
  spec.raw = {0};
  const Result<Bitmap> huge = decode(build_png(spec));
  ASSERT_FALSE(huge.has_value());
  EXPECT_EQ(huge.error().kind, ErrorKind::LimitExceeded);
  EXPECT_NE(huge.error().message.find("65535x65535"), std::string::npos) << huge.error().message;
  EXPECT_NE(huge.error().message.find("too large"), std::string::npos) << huge.error().message;

  ImageSpec zero;
  zero.width = 0;
  zero.raw = {0};
  expect_decode_error(build_png(zero), "zero-sized", "width 0");
}

// 上限は呼び出し側が決める（api は RenderLimits::image_pixels を渡す）。
// ちょうどは通り、1 画素でも超えたら LimitExceeded。
TEST(PngDecode, MaxPixelsIsAParameter) {
  ImageSpec spec;
  spec.width = 4;
  spec.height = 2;
  spec.color_type = 6;
  // 4x2 の RGBA = 1 行 16 バイト
  spec.raw = with_none_filter(std::vector<std::uint8_t>(std::size_t{16} * 2, 0x40), 16, 2);
  const std::vector<std::uint8_t> png = build_png(spec);

  const Result<Bitmap> exact = decode(png, 8);
  ASSERT_TRUE(exact.has_value()) << to_string(exact.error());
  EXPECT_EQ(exact->width, 4U);

  const Result<Bitmap> over = decode(png, 7);
  ASSERT_FALSE(over.has_value());
  EXPECT_EQ(over.error().kind, ErrorKind::LimitExceeded);
  EXPECT_NE(over.error().message.find("the limit is 7"), std::string::npos) << over.error().message;
}

TEST(PngDecode, RejectsTruncatedIdatData) {
  ImageSpec spec;
  spec.width = 4;
  spec.height = 4;
  // 最後の行が足りない
  spec.raw = with_none_filter(test::make_gradient(4, 4).rgba, 16, 3);
  expect_decode_error(build_png(spec), "truncated", "3 rows for a 4-row image");
}

TEST(PngDecode, RejectsOversizedIdatData) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.raw = with_none_filter(std::vector<std::uint8_t>(16, 0), 8, 2);  // 1 行多い
  expect_decode_error(build_png(spec), "more than", "2 rows for a 1-row image");
}

TEST(PngDecode, RejectsCorruptZlibStream) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 2;
  spec.raw = {0x78, 0x9C, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11};
  spec.raw_is_compressed = true;
  expect_decode_error(build_png(spec), "inflate", "garbage after the zlib header");

  ImageSpec not_zlib;
  not_zlib.width = 2;
  not_zlib.height = 2;
  not_zlib.raw = {0xFF, 0xFF, 0xFF, 0xFF};
  not_zlib.raw_is_compressed = true;
  expect_decode_error(build_png(not_zlib), "inflate", "not a zlib stream");
}

TEST(PngDecode, RejectsTrailingBytesInsideTheZlibStream) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.raw = test::zlib_compress(with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1));
  spec.raw.push_back(0x00);
  spec.raw_is_compressed = true;
  expect_decode_error(build_png(spec), "follow the end of the IDAT zlib stream", "padded IDAT");
}

TEST(PngDecode, RejectsAnInvalidFilterType) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.raw = {5, 1, 2, 3, 4, 5, 6, 7, 8};  // フィルタ 5 は未定義
  expect_decode_error(build_png(spec), "filter type 5", "filter 5");
}

TEST(PngDecode, RejectsAPaletteIndexOutOfRange) {
  ImageSpec spec;
  spec.width = 2;
  spec.height = 1;
  spec.color_type = 3;
  spec.palette = {1, 2, 3, 4, 5, 6};  // 2 エントリ
  spec.raw = with_none_filter(std::vector<std::uint8_t>{0, 7}, 2, 1);
  expect_decode_error(build_png(spec), "out of range", "index 7 of 2");
}

TEST(PngDecode, RejectsAPaletteImageWithoutPlte) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 3;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{0}, 1, 1);
  expect_decode_error(build_png(spec), "no PLTE", "palette without PLTE");
}

TEST(PngDecode, RejectsPlteOnGrayscale) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 0;
  spec.palette = {1, 2, 3};
  spec.raw = with_none_filter(std::vector<std::uint8_t>{0}, 1, 1);
  expect_decode_error(build_png(spec), "PLTE is not allowed", "grayscale with PLTE");
}

TEST(PngDecode, RejectsMalformedPlte) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 3;
  spec.palette = {1, 2, 3, 4};  // 3 の倍数でない
  spec.raw = with_none_filter(std::vector<std::uint8_t>{0}, 1, 1);
  expect_decode_error(build_png(spec), "multiple of 3", "PLTE of 4 bytes");
}

TEST(PngDecode, RejectsTrnsOnFormatsThatAlreadyHaveAlpha) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.has_trns = true;
  spec.trns = {0, 0, 0, 0, 0, 0};
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  expect_decode_error(build_png(spec), "tRNS is not allowed", "RGBA with tRNS");
}

TEST(PngDecode, RejectsMalformedTrns) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 0;
  spec.has_trns = true;
  spec.trns = {0, 0, 0};  // grayscale の tRNS は 2 バイト
  spec.raw = with_none_filter(std::vector<std::uint8_t>{0}, 1, 1);
  expect_decode_error(build_png(spec), "2 bytes for grayscale", "3-byte tRNS");
}

TEST(PngDecode, RejectsPaletteChunksAfterIdat) {
  ImageSpec spec;
  spec.width = 1;
  spec.height = 1;
  spec.color_type = 3;
  spec.palette = {1, 2, 3};
  spec.raw = with_none_filter(std::vector<std::uint8_t>{0}, 1, 1);
  spec.after_idat = {{"tRNS", {0}}};
  expect_decode_error(build_png(spec), "appears after IDAT", "tRNS after IDAT");
}

TEST(PngDecode, RejectsAnOversizedChunkLength) {
  ImageSpec spec;
  spec.raw = with_none_filter(std::vector<std::uint8_t>{1, 2, 3, 4}, 4, 1);
  std::vector<std::uint8_t> png = build_png(spec);
  // IHDR の長さを 0x80000000 に書き換える
  png[8] = 0x80;
  expect_decode_error(png, "above the limit", "length 2^31");
}

// ---- ファジング（種を固定したランダム破壊。ASan で意味を持つ）---------------

TEST(PngDecode, SurvivesRandomMutations) {
  const Bitmap original = test::make_gradient(16, 9);
  const Result<std::vector<std::uint8_t>> encoded = encode(original);
  ASSERT_TRUE(encoded.has_value());

  test::Rng rng(20260919);
  for (int round = 0; round < 3000; ++round) {
    std::vector<std::uint8_t> broken = *encoded;
    const std::uint32_t mutations = 1 + rng.below(6);
    for (std::uint32_t i = 0; i < mutations; ++i) {
      broken[rng.below(static_cast<std::uint32_t>(broken.size()))] = rng.byte();
    }
    if (rng.below(4) == 0) {
      broken.resize(rng.below(static_cast<std::uint32_t>(broken.size()) + 1));
    }
    // 落ちない・未定義動作を起こさないことが検査対象。成功しても構わないが、
    // 成功したなら Bitmap の不変条件は守られていること。
    const Result<Bitmap> decoded = decode(broken);
    if (decoded.has_value()) {
      EXPECT_EQ(decoded->rgba.size(),
                static_cast<std::size_t>(decoded->width) * decoded->height * 4)
          << "round " << round;
    } else {
      EXPECT_FALSE(decoded.error().message.empty()) << "round " << round;
    }
  }
}

TEST(PngDecode, SurvivesRandomGarbage) {
  test::Rng rng(31337);
  for (int round = 0; round < 2000; ++round) {
    std::vector<std::uint8_t> bytes(rng.below(64));
    for (std::uint8_t& b : bytes) {
      b = rng.byte();
    }
    // 先頭を正しいシグネチャにして、チャンク解析まで進む確率を上げる
    if (rng.below(2) == 0 && bytes.size() >= kSignature.size()) {
      std::copy(kSignature.begin(), kSignature.end(), bytes.begin());
    }
    const Result<Bitmap> decoded = decode(bytes);
    if (decoded.has_value()) {
      EXPECT_EQ(decoded->rgba.size(),
                static_cast<std::size_t>(decoded->width) * decoded->height * 4);
    }
  }
}

}  // namespace
}  // namespace shashoku::png
