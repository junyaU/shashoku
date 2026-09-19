#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

// テスト用フォントの置き場所は cmake/TestAssets.cmake が configure 時にダウンロードして
// SHASHOKU_TEST_FONT_DIR で渡す（リポジトリにはフォントを置かない）。

namespace shashoku::text::assets {

inline std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return {};
  }
  const std::streamsize size = input.tellg();
  input.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

// 4MB 超のフォントを毎テスト読み直さないようにキャッシュする（内容は不変）。
inline const std::vector<std::uint8_t>& noto_sans_jp_regular() {
  static const std::vector<std::uint8_t> bytes =
      read_file(std::string(SHASHOKU_TEST_FONT_DIR) + "/NotoSansJP-Regular.otf");
  return bytes;
}

inline const std::vector<std::uint8_t>& noto_sans_jp_bold() {
  static const std::vector<std::uint8_t> bytes =
      read_file(std::string(SHASHOKU_TEST_FONT_DIR) + "/NotoSansJP-Bold.otf");
  return bytes;
}

inline const std::vector<std::uint8_t>& noto_sans() {
  static const std::vector<std::uint8_t> bytes =
      read_file(std::string(SHASHOKU_TEST_FONT_DIR) + "/NotoSans-Regular.ttf");
  return bytes;
}

}  // namespace shashoku::text::assets
