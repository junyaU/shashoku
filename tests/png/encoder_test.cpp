// encode の出力そのものを検査する: チャンク構造・IHDR の中身・CRC・フィルタ選択・決定性。
// 往復（encode → decode）は round_trip_test.cpp、decode 単体は decoder_test.cpp。

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/crc32.hpp"
#include "png/png.hpp"
#include "png/png_test_util.hpp"
#include "shashoku/error.hpp"

namespace shashoku::png {
namespace {

using test::ChunkView;
using test::extract_filters;
using test::list_chunks;

std::vector<std::uint8_t> encode_or_die(const Bitmap& bitmap) {
  Result<std::vector<std::uint8_t>> encoded = encode(bitmap);
  EXPECT_TRUE(encoded.has_value())
      << (encoded.has_value() ? std::string{} : to_string(encoded.error()));
  return encoded.value_or(std::vector<std::uint8_t>{});
}

std::uint32_t be32_at(std::span<const std::uint8_t> bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
         (static_cast<std::uint32_t>(bytes[at + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[at + 2]) << 8U) |
         static_cast<std::uint32_t>(bytes[at + 3]);
}

// ---- CRC32（自前実装）-------------------------------------------------------

TEST(PngCrc32, MatchesKnownVectors) {
  // 定番のテストベクタ（CRC-32/ISO-HDLC）。
  const std::string check = "123456789";
  const std::vector<std::uint8_t> bytes(check.begin(), check.end());
  EXPECT_EQ(crc32(bytes), 0xCBF43926U);
  EXPECT_EQ(crc32(std::span<const std::uint8_t>{}), 0x00000000U);

  // 空の IEND チャンク（型 "IEND" だけ）の CRC は PNG ファイルの末尾に必ず現れる値。
  const std::vector<std::uint8_t> iend{'I', 'E', 'N', 'D'};
  EXPECT_EQ(crc32(iend), 0xAE426082U);
}

TEST(PngCrc32, MatchesZlibForRandomBuffers) {
  // 自前のテーブルと zlib の実装が全長で一致すること（別実装との突き合わせ）。
  test::Rng rng(20260919);
  for (std::size_t length = 0; length < 300; ++length) {
    std::vector<std::uint8_t> bytes(length);
    for (std::uint8_t& b : bytes) {
      b = rng.byte();
    }
    const auto expected = static_cast<std::uint32_t>(
        ::crc32(::crc32(0, nullptr, 0), reinterpret_cast<const Bytef*>(bytes.data()),
                static_cast<uInt>(bytes.size())));
    ASSERT_EQ(crc32(bytes), expected) << "length=" << length;
  }
}

// ---- 入力の検証 -------------------------------------------------------------

TEST(PngEncode, RejectsZeroWidth) {
  Bitmap bitmap;
  bitmap.width = 0;
  bitmap.height = 4;
  const Result<std::vector<std::uint8_t>> encoded = encode(bitmap);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(encoded.error().message.find("zero width"), std::string::npos)
      << encoded.error().message;
}

TEST(PngEncode, RejectsZeroHeight) {
  Bitmap bitmap;
  bitmap.width = 4;
  bitmap.height = 0;
  const Result<std::vector<std::uint8_t>> encoded = encode(bitmap);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(encoded.error().message.find("zero height"), std::string::npos)
      << encoded.error().message;
}

TEST(PngEncode, RejectsRgbaOfTheWrongLength) {
  Bitmap bitmap(3, 2);
  bitmap.rgba.pop_back();
  const Result<std::vector<std::uint8_t>> encoded = encode(bitmap);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error().kind, ErrorKind::InvalidOption);
  // 何が食い違っているかを具体的に書く（fail loudly）
  EXPECT_NE(encoded.error().message.find("24"), std::string::npos) << encoded.error().message;
  EXPECT_NE(encoded.error().message.find("23"), std::string::npos) << encoded.error().message;
}

// ---- 出力の構造 -------------------------------------------------------------

TEST(PngEncode, StartsWithTheSignature) {
  const std::vector<std::uint8_t> png = encode_or_die(test::make_gradient(5, 3));
  ASSERT_GE(png.size(), kSignature.size());
  EXPECT_TRUE(std::equal(kSignature.begin(), kSignature.end(), png.begin()));
}

TEST(PngEncode, HasIhdrIdatIendInThatOrder) {
  const std::vector<std::uint8_t> png = encode_or_die(test::make_gradient(5, 3));
  const std::vector<ChunkView> chunks = list_chunks(png);
  ASSERT_EQ(chunks.size(), 3U);
  EXPECT_EQ(chunks[0].type, "IHDR");
  EXPECT_EQ(chunks[1].type, "IDAT");
  EXPECT_EQ(chunks[2].type, "IEND");
  EXPECT_TRUE(chunks[2].data.empty());

  // IEND のあとに何も続かない
  const ChunkView& last = chunks.back();
  EXPECT_EQ(last.offset + 12 + last.data.size(), png.size());
}

TEST(PngEncode, IhdrDescribesRgba8NonInterlaced) {
  const std::vector<std::uint8_t> png = encode_or_die(test::make_gradient(7, 13));
  const std::vector<ChunkView> chunks = list_chunks(png);
  ASSERT_FALSE(chunks.empty());
  const std::vector<std::uint8_t>& ihdr = chunks[0].data;
  ASSERT_EQ(ihdr.size(), 13U);
  EXPECT_EQ(be32_at(ihdr, 0), 7U);
  EXPECT_EQ(be32_at(ihdr, 4), 13U);
  EXPECT_EQ(ihdr[8], 8U);   // bit depth
  EXPECT_EQ(ihdr[9], 6U);   // color type: RGBA
  EXPECT_EQ(ihdr[10], 0U);  // compression: deflate
  EXPECT_EQ(ihdr[11], 0U);  // filter method: 適応フィルタ
  EXPECT_EQ(ihdr[12], 0U);  // interlace: なし
}

TEST(PngEncode, EveryChunkCrcMatches) {
  const std::vector<std::uint8_t> png = encode_or_die(test::make_noise(9, 6, 7));
  const std::vector<ChunkView> chunks = list_chunks(png);
  ASSERT_EQ(chunks.size(), 3U);
  for (const ChunkView& chunk : chunks) {
    // CRC の期待値は zlib の crc32（別実装）で作る
    std::vector<std::uint8_t> body(chunk.type.begin(), chunk.type.end());
    body.insert(body.end(), chunk.data.begin(), chunk.data.end());
    const auto expected = static_cast<std::uint32_t>(
        ::crc32(::crc32(0, nullptr, 0), reinterpret_cast<const Bytef*>(body.data()),
                static_cast<uInt>(body.size())));
    EXPECT_EQ(chunk.stored_crc, expected) << "chunk " << chunk.type;
  }
}

TEST(PngEncode, IdatIsAValidZlibStreamOfTheExpectedLength) {
  const Bitmap bitmap = test::make_gradient(11, 5);
  const std::vector<std::uint8_t> png = encode_or_die(bitmap);
  const std::vector<ChunkView> chunks = list_chunks(png);
  ASSERT_EQ(chunks.size(), 3U);

  const std::size_t stride = std::size_t{bitmap.width} * 4;
  uLongf raw_size = (stride + 1) * bitmap.height;
  std::vector<std::uint8_t> raw(raw_size);
  ASSERT_EQ(uncompress(reinterpret_cast<Bytef*>(raw.data()), &raw_size,
                       reinterpret_cast<const Bytef*>(chunks[1].data.data()),
                       static_cast<uLong>(chunks[1].data.size())),
            Z_OK);
  EXPECT_EQ(raw_size, (stride + 1) * bitmap.height);
}

// ---- 決定性（DESIGN.md §3-5）------------------------------------------------

TEST(PngEncode, IsDeterministic) {
  const Bitmap bitmap = test::make_noise(23, 17, 0xC0FFEE);
  const std::vector<std::uint8_t> first = encode_or_die(bitmap);
  const std::vector<std::uint8_t> second = encode_or_die(bitmap);
  EXPECT_EQ(first, second);

  // 別に作った同じ内容の Bitmap でも同じバイト列になる
  Bitmap copy(bitmap.width, bitmap.height);
  copy.rgba = bitmap.rgba;
  EXPECT_EQ(encode_or_die(copy), first);
}

// ---- フィルタ選択（PNG 仕様 §12.8 のヒューリスティック）---------------------

// 1 行だけの画像では Up は None と、Paeth は Sub と必ず一致する（上の行が無いので）。
// 同点は番号の小さい方を選ぶと決めてあるので、この場合 None が残る。
TEST(PngFilter, PicksNoneWhenRawBytesAreAlreadySmall) {
  Bitmap bitmap(8, 1);
  for (std::uint32_t x = 0; x < bitmap.width; ++x) {
    const std::uint8_t v = (x % 2 == 0) ? 0 : 2;
    bitmap.set_pixel(x, 0, Color{v, v, v, v});
  }
  const std::vector<std::uint8_t> filters = extract_filters(encode_or_die(bitmap));
  ASSERT_EQ(filters.size(), 1U);
  EXPECT_EQ(filters[0], 0U);
}

TEST(PngFilter, PicksSubForAHorizontallyConstantRow) {
  const Bitmap bitmap = test::make_solid(8, 1, Color{128, 128, 128, 128});
  const std::vector<std::uint8_t> filters = extract_filters(encode_or_die(bitmap));
  ASSERT_EQ(filters.size(), 1U);
  EXPECT_EQ(filters[0], 1U);
}

TEST(PngFilter, PicksUpWhenTheRowRepeatsThePreviousOne) {
  Bitmap bitmap(16, 3);
  test::Rng rng(1);
  for (std::uint32_t x = 0; x < bitmap.width; ++x) {
    const Color c{rng.byte(), rng.byte(), rng.byte(), rng.byte()};
    for (std::uint32_t y = 0; y < bitmap.height; ++y) {
      bitmap.set_pixel(x, y, c);
    }
  }
  const std::vector<std::uint8_t> filters = extract_filters(encode_or_die(bitmap));
  ASSERT_EQ(filters.size(), 3U);
  EXPECT_EQ(filters[1], 2U);
  EXPECT_EQ(filters[2], 2U);
}

TEST(PngFilter, PicksAverageWhenEachByteIsTheMeanOfItsLeftAndUpperNeighbour) {
  // Average の予測 floor((left + up) / 2) をそのまま値にする行を作ると、
  // その行の残差は全バイト 0 になり、他の 4 つには勝てない。
  constexpr std::uint32_t kWidth = 24;
  Bitmap bitmap(kWidth, 2);
  test::Rng rng(42);
  for (std::uint32_t x = 0; x < kWidth; ++x) {
    // 上の行は「符号つきで見た絶対値」が大きい値にして、None / Up を不利にする
    const auto v = static_cast<std::uint8_t>(96 + (rng.next() % 64U));
    bitmap.set_pixel(x, 0, Color{v, v, v, v});
  }
  const std::size_t stride = std::size_t{kWidth} * 4;
  for (std::size_t i = 0; i < stride; ++i) {
    const unsigned left = i >= 4 ? bitmap.rgba[stride + i - 4] : 0U;
    const unsigned up = bitmap.rgba[i];
    bitmap.rgba[stride + i] = static_cast<std::uint8_t>((left + up) / 2U);
  }
  const std::vector<std::uint8_t> filters = extract_filters(encode_or_die(bitmap));
  ASSERT_EQ(filters.size(), 2U);
  EXPECT_EQ(filters[1], 3U);
}

TEST(PngFilter, PicksPaethForA2dGradientWithNoise) {
  // Paeth が想定している絵（縦横どちらにも傾きがあり、少しノイズが乗っている）。
  // 左・上・左上のどれかを選べるぶん、Sub / Up / Average のどれよりも残差が小さくなる。
  constexpr std::uint32_t kWidth = 24;
  constexpr std::uint32_t kHeight = 6;
  Bitmap bitmap(kWidth, kHeight);
  test::Rng rng(4649);
  for (std::uint32_t y = 0; y < kHeight; ++y) {
    for (std::uint32_t x = 0; x < kWidth; ++x) {
      const int base = 40 + (3 * static_cast<int>(x)) + (7 * static_cast<int>(y));
      const int noise = static_cast<int>(rng.next() % 9U) - 4;
      const auto v = static_cast<std::uint8_t>(std::clamp(base + noise, 0, 255));
      bitmap.set_pixel(
          x, y,
          Color{v, static_cast<std::uint8_t>(255 - v), static_cast<std::uint8_t>(v / 2), 255});
    }
  }
  const std::vector<std::uint8_t> filters = extract_filters(encode_or_die(bitmap));
  ASSERT_EQ(filters.size(), kHeight);
  // 先頭行には上の行が無いので Paeth は Sub と同じ。2 行目以降が Paeth になる。
  for (std::uint32_t y = 1; y < kHeight; ++y) {
    EXPECT_EQ(filters[y], 4U) << "row " << y;
  }
}

TEST(PngFilter, AllFiveFiltersRoundTrip) {
  // 上の 5 つの入力を 1 枚ずつ往復させ、どのフィルタでも復元できることを確かめる。
  std::vector<Bitmap> images;
  {
    Bitmap none(8, 1);
    for (std::uint32_t x = 0; x < none.width; ++x) {
      const std::uint8_t v = (x % 2 == 0) ? 0 : 2;
      none.set_pixel(x, 0, Color{v, v, v, v});
    }
    images.push_back(none);
  }
  images.push_back(test::make_solid(8, 1, Color{128, 128, 128, 128}));
  images.push_back(test::make_noise(16, 4, 3));
  images.push_back(test::make_gradient(16, 16));

  for (const Bitmap& image : images) {
    const std::vector<std::uint8_t> png = encode_or_die(image);
    const Result<Bitmap> decoded = decode(png);
    ASSERT_TRUE(decoded.has_value())
        << (decoded.has_value() ? std::string{} : to_string(decoded.error()));
    EXPECT_EQ(*decoded, image);
  }
}

}  // namespace
}  // namespace shashoku::png
