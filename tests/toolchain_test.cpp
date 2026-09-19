// ツールチェーンと依存ライブラリの配線を確認するスモークテスト。
// 製品コードのテストではない。ここが落ちたらコードではなく環境
// （コンパイラ / 標準ライブラリ / FetchContent）を疑うこと。

#include <zlib.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "shashoku/version.hpp"

namespace {

std::expected<int, std::string> parse_digit(char c) {
  if (c < '0' || c > '9') {
    return std::unexpected(std::string("not a digit: ") + c);
  }
  return c - '0';
}

TEST(Toolchain, StdExpectedWorks) {
  EXPECT_EQ(parse_digit('7'), 7);

  const auto err = parse_digit('x');
  ASSERT_FALSE(err.has_value());
  EXPECT_EQ(err.error(), "not a digit: x");
}

TEST(Toolchain, LibraryLinks) { EXPECT_FALSE(shashoku::version().empty()); }

TEST(Toolchain, ZlibIsThePinnedVersion) {
  // ヘッダと実際にリンクされたライブラリが同じ版であること
  // （システムの zlib を誤って拾っていないこと）を確認する。
  EXPECT_EQ(std::string_view(zlibVersion()), ZLIB_VERSION);
  EXPECT_EQ(std::string_view(ZLIB_VERSION), "1.3.2");
}

TEST(Toolchain, ZlibRoundTrip) {
  constexpr std::string_view kInput = "shashoku shashoku shashoku shashoku";

  std::array<std::uint8_t, 128> compressed{};
  uLongf compressed_size = compressed.size();
  ASSERT_EQ(
      compress2(compressed.data(), &compressed_size, reinterpret_cast<const Bytef*>(kInput.data()),
                static_cast<uLong>(kInput.size()), Z_BEST_COMPRESSION),
      Z_OK);

  std::array<char, 128> restored{};
  uLongf restored_size = restored.size();
  ASSERT_EQ(uncompress(reinterpret_cast<Bytef*>(restored.data()), &restored_size, compressed.data(),
                       compressed_size),
            Z_OK);

  EXPECT_EQ(std::string_view(restored.data(), restored_size), kInput);
}

}  // namespace
