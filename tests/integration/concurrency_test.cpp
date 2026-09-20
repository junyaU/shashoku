#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// 同じ共有資源を複数スレッドから同時に使う（ARCHITECTURE.md A34 / issue #7）。
//
// 見たいのは 2 つ:
//   1. 同時に使っても、単スレッドで 1 本ずつ組んだときとバイト単位で同じ PNG が出ること
//   2. データ競合がないこと（それを検出するのは TSan の仕事。`tsan` プリセットで
//      同じテストを回す。TSan なしでも 1. の検査にはなる）
//
// 共有してよいのは `LoadedFonts` / `LoadedImages` だけで、FreeType のハンドルも
// HarfBuzz のバッファも実行ごとに作られる。この前提が崩れると、ここが落ちるか
// TSan が競合を報告する。
namespace shashoku::test {
namespace {

constexpr int kThreads = 8;
constexpr int kRepeats = 4;

// 入力を 3 種類に散らす（どのスレッドも同じ入力だと、たまたま同じ順で触っているだけの
// 状態を見逃す）。2 つ目は豆腐が出る入力。
const std::vector<std::string>& inputs() {
  static const std::vector<std::string> table = {
      R"(<div style="padding: 8px; font-size: 16px">こんにちは、世界のみんな。ABC AVTo</div>)",
      R"(<div style="font-size: 18px">絵文字 &#x1F600; と記号 ★ を含む行。</div>)",
      R"(<div style="padding: 6px"><img src="icon" style="width: 32px; height: 32px"> 図版）」</div>)",
  };
  return table;
}

TEST(Concurrency, SharedResourcesGiveTheSameBytesFromManyThreads) {
  const RenderOptions options = options_for(320);

  std::expected<LoadedFonts, RenderError> fonts = LoadedFonts::prepare(latin_then_japanese());
  ASSERT_TRUE(fonts.has_value()) << to_string(fonts.error());
  std::expected<LoadedImages, RenderError> images = LoadedImages::prepare(icon_images());
  ASSERT_TRUE(images.has_value()) << to_string(images.error());

  // 基準: 単スレッドで 1 本ずつ組んだ結果。
  std::vector<std::vector<std::uint8_t>> expected_png;
  std::vector<std::vector<Warning>> expected_warnings;
  for (const std::string& html : inputs()) {
    const auto result = render(html, *fonts, *images, options);
    ASSERT_TRUE(result.has_value()) << to_string(result.error());
    expected_png.push_back(result->png);
    expected_warnings.push_back(result->warnings);
  }

  // 8 スレッド x 4 回。同じ LoadedFonts / LoadedImages を全員が参照する。
  struct Outcome {
    std::size_t input = 0;
    std::vector<std::uint8_t> png;
    std::vector<Warning> warnings;
    std::string error;  // 空なら成功
  };
  std::vector<std::vector<Outcome>> results(kThreads);
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t] {
      std::vector<Outcome>& mine = results[static_cast<std::size_t>(t)];
      for (int repeat = 0; repeat < kRepeats; ++repeat) {
        // スレッドごとに開始位置をずらす。
        const std::size_t index =
            (static_cast<std::size_t>(t) + static_cast<std::size_t>(repeat)) % inputs().size();
        const auto result = render(inputs()[index], *fonts, *images, options);
        Outcome outcome;
        outcome.input = index;
        if (result) {
          outcome.png = result->png;
          outcome.warnings = result->warnings;
        } else {
          outcome.error = to_string(result.error());
        }
        mine.push_back(std::move(outcome));
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }

  std::size_t checked = 0;
  for (const std::vector<Outcome>& thread_results : results) {
    for (const Outcome& outcome : thread_results) {
      ASSERT_TRUE(outcome.error.empty()) << outcome.error;
      EXPECT_EQ(outcome.png, expected_png[outcome.input]) << "input #" << outcome.input;
      EXPECT_EQ(outcome.warnings, expected_warnings[outcome.input]) << "input #" << outcome.input;
      ++checked;
    }
  }
  EXPECT_EQ(checked, static_cast<std::size_t>(kThreads) * kRepeats);
}

// dump() も同じ（共有資源を読むだけの経路がもう 1 本ある）。
TEST(Concurrency, DumpFromManyThreadsMatchesTheSingleThreadedResult) {
  const RenderOptions options = options_for(320);
  std::expected<LoadedFonts, RenderError> fonts = LoadedFonts::prepare(japanese_fonts());
  ASSERT_TRUE(fonts.has_value()) << to_string(fonts.error());
  std::expected<LoadedImages, RenderError> images = LoadedImages::prepare(ImageSet{});
  ASSERT_TRUE(images.has_value()) << to_string(images.error());

  const std::string_view html = inputs()[0];
  const auto expected = dump(html, *fonts, *images, options, DumpStage::Box);
  ASSERT_TRUE(expected.has_value()) << to_string(expected.error());

  std::vector<std::string> results(kThreads);
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t] {
      const auto dumped = dump(html, *fonts, *images, options, DumpStage::Box);
      results[static_cast<std::size_t>(t)] =
          dumped ? *dumped : ("error: " + to_string(dumped.error()));
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  for (const std::string& actual : results) {
    EXPECT_EQ(actual, *expected);
  }
}

}  // namespace
}  // namespace shashoku::test
