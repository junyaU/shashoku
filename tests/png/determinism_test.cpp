// 決定性の保証範囲（issue #10-4a / DESIGN.md §3-5 / README「決定性」）。
//
// 「同じ入力 → バイト単位で同じ PNG」のうち、**環境をまたいでも同じ**と言い切れる部分を
// 期待値としてリポジトリに置く。CI は clang-18 + libc++ と gcc-14 + libstdc++ の
// 4 ジョブで同じテストを走らせるので、この期待値が両方で通れば「少なくともこの 2 環境
// （x86-64 Linux）ではエンコーダの出力がバイト単位で同じ」と言える。
//
// PNG のバイト列は 2 つの部分でできている:
//
//   (a) **shashoku 自身が決める部分** — シグネチャ / IHDR / 行ごとのフィルタの選択と
//       フィルタ後のバイト列 / チャンクの並びと CRC。すべて整数演算なので、環境をまたいで
//       1 ビットも変わってはいけない。この部分を下の kCases の checksum で固定する
//       （= 展開後の IDAT、つまり「フィルタ種別バイト + フィルタ後の走査線」の CRC-32）。
//
//   (b) **zlib の deflate 出力** — 版（cmake/Dependencies.cmake で 1.3.2 に固定）と
//       設定（encoder.cpp の compress2 + Z_BEST_COMPRESSION、ストラテジ / windowBits /
//       memLevel は既定）で決まる。**ここは固定していない。**
//
// なぜ (b) を固定しないか: 圧縮レベルを変えると出力バイト列は必ず変わる。実測では
// IDAT が 29 バイトの 16x16 の単色画像でも、レベル 6 と 9 でバイト列が違った。
// 圧縮レベルの既定を変える作業（issue #13）とこのテストが正面衝突するので、
// レベルに依らない (a) だけを固定してある。(b) まで固定したいなら「PNG 全体のバイト列の
// CRC-32」を 1 つ足せばよい（値は #13 が落ち着いてから採る。README「決定性」に申し送り）。
//
// 期待値を更新してよいのは、エンコーダの出力を**意図して**変えたときだけ
// （フィルタの選び方 = PNG 仕様 §12.8 の最小絶対値和、IHDR の内容、走査線の並び）。
// 圧縮レベルを変えただけでこの値が動いたら、それはバグ。

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/crc32.hpp"
#include "png/png.hpp"
#include "png/png_test_util.hpp"

namespace shashoku::png::test {
namespace {

std::vector<std::uint8_t> encode_or_fail(const Bitmap& bitmap) {
  const Result<std::vector<std::uint8_t>> encoded = encode(bitmap);
  if (!encoded) {
    ADD_FAILURE() << "png::encode: " << to_string(encoded.error());
    return {};
  }
  return *encoded;
}

// 展開後の IDAT（フィルタ種別バイト + フィルタ後の走査線）。圧縮レベルに依らない。
std::vector<std::uint8_t> inflate_idat(std::span<const std::uint8_t> png, std::uint32_t width,
                                       std::uint32_t height) {
  std::vector<std::uint8_t> compressed;
  for (const ChunkView& chunk : list_chunks(png)) {
    if (chunk.type == "IDAT") {
      compressed.insert(compressed.end(), chunk.data.begin(), chunk.data.end());
    }
  }
  const std::size_t stride = std::size_t{width} * 4;  // encode は常に RGBA8
  uLongf raw_size = (stride + 1) * height;
  std::vector<std::uint8_t> raw(raw_size == 0 ? 1 : raw_size);
  const int rc = uncompress(reinterpret_cast<Bytef*>(raw.data()), &raw_size,
                            reinterpret_cast<const Bytef*>(compressed.data()),
                            static_cast<uLong>(compressed.size()));
  if (rc != Z_OK) {
    ADD_FAILURE() << "zlib uncompress failed with code " << rc;
    return {};
  }
  raw.resize(raw_size);
  return raw;
}

struct Case {
  std::string_view name;
  Bitmap (*make)();
  std::uint32_t checksum;  // 展開後の IDAT の CRC-32（PNG 仕様 付録 D）
};

// 5 種のフィルタを満遍なく引くように選んだ 4 枚（下の UsesSeveralFilters が確かめる）。
constexpr Case kCases[] = {
    {"solid", [] { return make_solid(16, 16, Color{0x33, 0x66, 0xCC, 0xFF}); }, 0x47ACA152U},
    {"gradient", [] { return make_gradient(33, 17); }, 0xDB8C3D69U},
    {"noise", [] { return make_noise(24, 24, 0x5EED'1234U); }, 0x24AF3D95U},
    {"transparent", [] { return make_solid(7, 5, Color{0, 0, 0, 0}); }, 0x2BCB3AE7U},
};

TEST(PngDeterminism, FilteredPayloadMatchesThePinnedChecksum) {
  for (const Case& test_case : kCases) {
    const Bitmap bitmap = test_case.make();
    const std::vector<std::uint8_t> png = encode_or_fail(bitmap);
    ASSERT_FALSE(png.empty()) << test_case.name;
    const std::vector<std::uint8_t> raw = inflate_idat(png, bitmap.width, bitmap.height);
    ASSERT_FALSE(raw.empty()) << test_case.name;
    EXPECT_EQ(raw.size(), (std::size_t{bitmap.width} * 4 + 1) * bitmap.height) << test_case.name;
    const std::uint32_t actual = crc32(raw);
    EXPECT_EQ(actual, test_case.checksum)
        << test_case.name << ": 展開後の IDAT の CRC-32 が期待値と違う（期待 "
        << std::format("0x{:08X}", test_case.checksum) << " / 実際 "
        << std::format("0x{:08X}", actual)
        << "）。エンコーダの出力を意図して変えたのでなければ、環境間の差 = 決定性の破れ";
  }
}

// 上の checksum が「フィルタ 0 しか選ばれていない列」を固めているだけ、ということが
// ないように、4 枚で 3 種類以上のフィルタが実際に選ばれていることを確かめる。
TEST(PngDeterminism, UsesSeveralFilters) {
  std::vector<std::uint8_t> seen;
  for (const Case& test_case : kCases) {
    const std::vector<std::uint8_t> png = encode_or_fail(test_case.make());
    ASSERT_FALSE(png.empty()) << test_case.name;
    for (const std::uint8_t filter : extract_filters(png)) {
      if (std::ranges::find(seen, filter) == seen.end()) {
        seen.push_back(filter);
      }
    }
  }
  EXPECT_GE(seen.size(), 3U) << "フィルタの選択がほとんど働いていない";
}

// 同じ Bitmap からは同じバイト列（同一プロセス内。PngEncode.IsDeterministic の
// 補強として、デコード → 再エンコードでも同じになることまで見る）。
TEST(PngDeterminism, DecodeThenEncodeReproducesTheSameBytes) {
  for (const Case& test_case : kCases) {
    const std::vector<std::uint8_t> png = encode_or_fail(test_case.make());
    ASSERT_FALSE(png.empty()) << test_case.name;
    const Result<Bitmap> decoded = decode(png);
    ASSERT_TRUE(decoded.has_value()) << test_case.name << ": " << to_string(decoded.error());
    EXPECT_EQ(encode_or_fail(*decoded), png) << test_case.name;
  }
}

}  // namespace
}  // namespace shashoku::png::test
