#include "support/golden.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/result.hpp"
#include "png/png.hpp"
#include "shashoku/error.hpp"

namespace shashoku::test {
namespace {

constexpr Color kDiffMark{255, 0, 0, 255};

// 白の上にストレートアルファで合成する（差分画像はアルファなしで見せたいので）。
std::uint8_t over_white(std::uint8_t v, std::uint8_t a) {
  const int inverted = 255 - int{v};
  return static_cast<std::uint8_t>(255 - (((inverted * int{a}) + 127) / 255));
}

// 一致したピクセルは白に 3/4 寄せて薄くする。赤い差分が目立つように。
Color faded(Color c) {
  const auto fade = [](std::uint8_t v) {
    return static_cast<std::uint8_t>(255 - ((255 - int{v}) / 4));
  };
  return Color{fade(over_white(c.r, c.a)), fade(over_white(c.g, c.a)), fade(over_white(c.b, c.a)),
               255};
}

bool has_pixel(const Bitmap& bitmap, std::uint32_t x, std::uint32_t y) {
  return x < bitmap.width && y < bitmap.height;
}

std::string describe(Color c) { return std::format("rgba({}, {}, {}, {})", c.r, c.g, c.b, c.a); }

struct Difference {
  std::size_t count = 0;
  std::uint32_t first_x = 0;
  std::uint32_t first_y = 0;
  bool found = false;
};

Difference compare(const Bitmap& actual, const Bitmap& expected) {
  Difference diff;
  const std::uint32_t width = std::max(actual.width, expected.width);
  const std::uint32_t height = std::max(actual.height, expected.height);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const bool in_actual = has_pixel(actual, x, y);
      const bool in_expected = has_pixel(expected, x, y);
      const bool same = in_actual && in_expected && actual.pixel(x, y) == expected.pixel(x, y);
      if (!same) {
        ++diff.count;
        if (!diff.found) {
          diff.found = true;
          diff.first_x = x;
          diff.first_y = y;
        }
      }
    }
  }
  return diff;
}

std::filesystem::path png_path(const std::filesystem::path& dir, std::string_view name,
                               std::string_view suffix) {
  return dir / std::format("{}{}.png", name, suffix);
}

// 書き出しは「失敗しても本題（比較結果）を潰さない」ので、結果を文字列で返すだけにする。
std::string write_or_describe(const std::filesystem::path& path, const Bitmap& bitmap) {
  const Result<void> written = write_png_file(path, bitmap);
  if (!written) {
    return std::format("  (could not write {}: {})", path.string(), to_string(written.error()));
  }
  return std::format("  wrote {}", path.string());
}

bool update_requested() {
  // テストは単一スレッドで走り、環境変数を書き換えもしない。
  // NOLINTNEXTLINE(concurrency-mt-unsafe): 読むだけ、かつテスト起動時の 1 回きり
  const char* value = std::getenv("SHASHOKU_UPDATE_GOLDEN");
  return value != nullptr && std::string_view(value) == "1";
}

}  // namespace

GoldenOptions default_golden_options() {
  return GoldenOptions{.golden_dir = std::filesystem::path(SHASHOKU_GOLDEN_DIR),
                       .output_dir = std::filesystem::path(SHASHOKU_TEST_OUTPUT_DIR),
                       .update = update_requested()};
}

Result<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return fail(ErrorKind::ImageDecode, std::format("cannot open {} for reading", path.string()));
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size < 0) {
    return fail(ErrorKind::ImageDecode, std::format("cannot measure {}", path.string()));
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  in.seekg(0, std::ios::beg);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) {
      return fail(ErrorKind::ImageDecode, std::format("cannot read {}", path.string()));
    }
  }
  return bytes;
}

Result<void> write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return fail(ErrorKind::Internal,
                  std::format("cannot create {}: {}", path.parent_path().string(), ec.message()));
    }
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return fail(ErrorKind::Internal, std::format("cannot open {} for writing", path.string()));
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    return fail(ErrorKind::Internal, std::format("cannot write {}", path.string()));
  }
  return {};
}

Result<Bitmap> read_png_file(const std::filesystem::path& path) {
  Result<std::vector<std::uint8_t>> bytes = read_file(path);
  if (!bytes) {
    return std::unexpected(std::move(bytes).error());
  }
  Result<Bitmap> bitmap = png::decode(*bytes);
  if (!bitmap) {
    Error error = std::move(bitmap).error();
    error.message = std::format("{}: {}", path.string(), error.message);
    return std::unexpected(std::move(error));
  }
  return bitmap;
}

Result<void> write_png_file(const std::filesystem::path& path, const Bitmap& bitmap) {
  Result<std::vector<std::uint8_t>> encoded = png::encode(bitmap);
  if (!encoded) {
    return std::unexpected(std::move(encoded).error());
  }
  return write_file(path, *encoded);
}

Bitmap make_diff_image(const Bitmap& actual, const Bitmap& expected) {
  const std::uint32_t width = std::max(actual.width, expected.width);
  const std::uint32_t height = std::max(actual.height, expected.height);
  Bitmap diff(width, height, kDiffMark);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const bool in_actual = has_pixel(actual, x, y);
      const bool in_expected = has_pixel(expected, x, y);
      if (in_actual && in_expected && actual.pixel(x, y) == expected.pixel(x, y)) {
        diff.set_pixel(x, y, faded(expected.pixel(x, y)));
      }
    }
  }
  return diff;
}

::testing::AssertionResult expect_golden(const Bitmap& actual, std::string_view name) {
  return expect_golden(actual, name, default_golden_options());
}

::testing::AssertionResult expect_golden(const Bitmap& actual, std::string_view name,
                                         const GoldenOptions& options) {
  const std::filesystem::path expected_path = png_path(options.golden_dir, name, "");

  if (options.update) {
    const Result<void> written = write_png_file(expected_path, actual);
    if (!written) {
      return ::testing::AssertionFailure()
             << "SHASHOKU_UPDATE_GOLDEN: cannot write the golden image for \"" << name
             << "\": " << to_string(written.error());
    }
    return ::testing::AssertionSuccess()
           << "SHASHOKU_UPDATE_GOLDEN: wrote " << expected_path.string()
           << " (人間が差分を目視してからコミットすること)";
  }

  Result<Bitmap> expected = read_png_file(expected_path);
  if (!expected) {
    return ::testing::AssertionFailure()
           << "golden image for \"" << name
           << "\" is missing or unreadable: " << to_string(expected.error()) << "\n"
           << write_or_describe(png_path(options.output_dir, name, ".actual"), actual) << "\n"
           << "  SHASHOKU_UPDATE_GOLDEN=1 で走らせると期待画像を作れる（要目視確認）";
  }

  const Difference diff = compare(actual, *expected);
  if (!diff.found && actual.width == expected->width && actual.height == expected->height) {
    return ::testing::AssertionSuccess();
  }

  const std::uint64_t total = std::max(std::uint64_t{actual.width} * actual.height,
                                       std::uint64_t{expected->width} * expected->height);
  ::testing::AssertionResult result = ::testing::AssertionFailure();
  result << "golden mismatch for \"" << name << "\"\n"
         << "  expected " << expected->width << "x" << expected->height << " ("
         << expected_path.string() << "), actual " << actual.width << "x" << actual.height << "\n";
  if (diff.found) {
    const std::uint32_t x = diff.first_x;
    const std::uint32_t y = diff.first_y;
    result << "  first difference at (" << x << ", " << y << "): expected "
           << (has_pixel(*expected, x, y) ? describe(expected->pixel(x, y))
                                          : std::string("outside the image"))
           << ", actual "
           << (has_pixel(actual, x, y) ? describe(actual.pixel(x, y))
                                       : std::string("outside the image"))
           << "\n";
  }
  result << "  " << diff.count << " of " << total << " pixel(s) differ\n"
         << write_or_describe(png_path(options.output_dir, name, ".actual"), actual) << "\n"
         << write_or_describe(png_path(options.output_dir, name, ".expected"), *expected) << "\n"
         << write_or_describe(png_path(options.output_dir, name, ".diff"),
                              make_diff_image(actual, *expected));
  return result;
}

}  // namespace shashoku::test
