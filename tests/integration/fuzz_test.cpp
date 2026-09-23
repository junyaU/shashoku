#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/utf8.hpp"
#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"
#include "support/failure.hpp"

// end-to-end のファジング（DESIGN.md §10-4 / ARCHITECTURE.md §4）。
//
// 乱数の種を固定した「ランダム入力の性質テスト」として通常のテストに含める。
// ランダムな日本語まじりの文字列を、狭い幅・3 つのあふれ処理・横書き / 縦書きの
// 組み合わせで render() に流し、次の 4 つを検査する:
//
//   (a) 落ちない（asan プリセットで走らせたときに意味を持つ）
//   (b) 成功するかエラーかのどちらかで、エラーなら RenderError が空でない
//   (c) 同じ入力を 2 回流すとバイト単位で一致する（DESIGN.md §3-5）
//   (d) dump(..., DumpStage::Box) も同じ入力で成功する
//
// 「絶対に破綻させない」が製品の約束なので、**どんな文字列でも成功する**ことも見る
// （豆腐は警告であってエラーではない）。落ちるべきなのは未対応のタグ・CSS だけで、
// それはこのテストの生成器からは出てこない。
namespace shashoku::test {
namespace {

// 生成に使う文字の種。禁則・クラスタ・フォールバックのどれにも触るように選ぶ。
constexpr std::u32string_view kHiragana = U"あいうえおかきくけこさしすせそたちつてとなにぬねの";
constexpr std::u32string_view kKatakana = U"アイウエオカキクケコサシスセソタチツテトナニヌネノ";
constexpr std::u32string_view kKanji = U"日本語組版写植東京文字行頭禁則処理縦横書";
// 小書きの仮名と長音（UAX #14 の CJ。strictness で挙動が変わる）
constexpr std::u32string_view kSmall = U"ぁぃぅぇぉっゃゅょゎヵヶーゝゞ々";
// 約物: 行頭禁則・行末禁則・分離禁則の全クラス
constexpr std::u32string_view kPunctuation =
    U"、。，．・：；！？「」『』（）〔〕［］｛｝〈〉《》…‥—";
constexpr std::u32string_view kAscii = U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::u32string_view kDigits = U"0123456789";
constexpr std::u32string_view kSpaces = U"  \n\t";
// どのテストフォントにも無い文字（豆腐になる）
constexpr std::u32string_view kEmoji = U"😀😃🎌🗾";

// HTML のメタ文字は実体参照にする（パーサを壊すのが目的ではない。html のファジングは
// tests/html/fuzz_test.cpp の担当）。
std::string escaped(const std::u32string& text) {
  std::string out;
  for (const char32_t cp : text) {
    switch (cp) {
      case U'&':
        out += "&amp;";
        break;
      case U'<':
        out += "&lt;";
        break;
      case U'>':
        out += "&gt;";
        break;
      default:
        append_utf8(out, cp);
        break;
    }
  }
  return out;
}

class Generator {
 public:
  explicit Generator(std::uint32_t seed) : rng_(seed) {}

  std::size_t pick(std::size_t count) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng_);
  }

  bool chance(int percent) { return std::uniform_int_distribution<int>(1, 100)(rng_) <= percent; }

  char32_t character() {
    // 和文を厚めに、絵文字と結合文字は薄く混ぜる
    const std::size_t bucket = pick(100);
    if (bucket < 28) {
      return kHiragana[pick(kHiragana.size())];
    }
    if (bucket < 44) {
      return kKatakana[pick(kKatakana.size())];
    }
    if (bucket < 60) {
      return kKanji[pick(kKanji.size())];
    }
    if (bucket < 70) {
      return kPunctuation[pick(kPunctuation.size())];
    }
    if (bucket < 78) {
      return kSmall[pick(kSmall.size())];
    }
    if (bucket < 86) {
      return kAscii[pick(kAscii.size())];
    }
    if (bucket < 91) {
      return kDigits[pick(kDigits.size())];
    }
    if (bucket < 97) {
      return kSpaces[pick(kSpaces.size())];
    }
    return kEmoji[pick(kEmoji.size())];
  }

  // 1 クラスタ。結合文字・異体字セレクタ・ZWJ を後ろに付けることがある。
  std::u32string cluster() {
    std::u32string out(1, character());
    if (chance(6)) {
      out.push_back(U'\u3099');  // 濁点（結合文字）
    }
    if (chance(4)) {
      out.push_back(U'\uFE00');  // 異体字セレクタ VS1
    }
    if (chance(3)) {
      out.push_back(U'\u200D');  // ZWJ
      out.push_back(character());
    }
    return out;
  }

  std::string body() {
    std::string html;
    const std::size_t length = 8 + pick(70);
    for (std::size_t i = 0; i < length; ++i) {
      if (chance(4)) {
        html += "<br>";
        continue;
      }
      if (chance(5)) {
        // ルビ（親文字 1〜2 クラスタ + ルビ 1〜4 クラスタ）
        std::u32string base = cluster();
        if (chance(40)) {
          base += cluster();
        }
        std::u32string reading;
        const std::size_t reading_length = 1 + pick(4);
        for (std::size_t r = 0; r < reading_length; ++r) {
          reading += kHiragana[pick(kHiragana.size())];
        }
        html += "<ruby>" + escaped(base) + "<rt>" + escaped(reading) + "</rt></ruby>";
        continue;
      }
      if (chance(6)) {
        html += R"(<span style="color: #c92a2a">)" + escaped(cluster()) + "</span>";
        continue;
      }
      html += escaped(cluster());
    }
    return html;
  }

 private:
  std::mt19937 rng_;
};

struct Case {
  std::string html;
  RenderOptions options;
};

Case make_case(Generator& generator) {
  Case test_case;
  const bool vertical = generator.chance(30);

  RenderOptions options;
  options.viewport_width = static_cast<int>(60 + generator.pick(341));  // 60..400
  if (vertical) {
    // 縦書きは block 方向が横なので高さが要る
    options.viewport_height = static_cast<int>(60 + generator.pick(341));
  } else if (generator.chance(20)) {
    options.viewport_height = static_cast<int>(40 + generator.pick(200));
  }
  switch (generator.pick(3)) {
    case 0:
      options.line_break.overflow = OverflowPolicy::Oidashi;
      break;
    case 1:
      options.line_break.overflow = OverflowPolicy::Oikomi;
      break;
    default:
      options.line_break.overflow = OverflowPolicy::Burasage;
      break;
  }
  switch (generator.pick(3)) {
    case 0:
      options.line_break.strictness = LineBreakStrictness::Strict;
      break;
    case 1:
      options.line_break.strictness = LineBreakStrictness::Normal;
      break;
    default:
      options.line_break.strictness = LineBreakStrictness::Loose;
      break;
  }
  options.line_break.trim_line_start = generator.chance(30);
  options.line_break.trim_line_end = generator.chance(70);
  options.line_break.collapse_punctuation_spacing = generator.chance(70);

  const std::string style = vertical ? "writing-mode: vertical-rl; " : "";
  test_case.html = "<div style=\"" + style + "font-size: 17px; line-height: 1.8\"><p>" +
                   generator.body() + "</p></div>";
  test_case.options = options;
  return test_case;
}

// 実フォントのシェーピングは重い（1 回 40ms 前後）ので、全ケースで (a)(b) を見つつ、
// 3 回に 1 回だけ (c) 決定性と (d) ダンプも確かめる。dev で 15 秒前後に収まる。
constexpr int kCaseCount = 200;
constexpr int kDeepCheckEvery = 3;
constexpr std::uint32_t kSeed = 20260920;

TEST(Fuzz, RandomJapaneseNeverBreaks) {
  const FontSet fonts = japanese_fonts();
  Generator generator(kSeed);

  for (int i = 0; i < kCaseCount; ++i) {
    const Case test_case = make_case(generator);
    const auto message = [&test_case, i] {
      return "case #" + std::to_string(i) +
             " width=" + std::to_string(test_case.options.viewport_width) +
             " html=" + test_case.html;
    };

    // (a)(b) 落ちない。組版が破綻しないのが約束なので、そもそも成功するはず
    const auto result = render(test_case.html, fonts, test_case.options);
    if (!result) {
      ADD_FAILURE() << to_string(result.error()) << "\n" << message();
      EXPECT_FALSE(first_error(result.error()).message.empty());
      continue;
    }
    EXPECT_FALSE(result->png.empty()) << message();
    EXPECT_GT(result->width, 0) << message();
    EXPECT_GT(result->height, 0) << message();

    if (i % kDeepCheckEvery != 0) {
      continue;
    }

    // (c) 決定性
    const auto again = render(test_case.html, fonts, test_case.options);
    ASSERT_TRUE(again.has_value()) << message();
    EXPECT_EQ(result->png, again->png) << message();
    EXPECT_EQ(result->warnings.size(), again->warnings.size()) << message();

    // (d) 中間表現もダンプできる
    const auto box = dump(test_case.html, fonts, ImageSet{}, test_case.options, DumpStage::Box);
    ASSERT_TRUE(box.has_value()) << to_string(box.error()) << "\n" << message();
    EXPECT_TRUE(box->starts_with("{")) << message();
  }
}

}  // namespace
}  // namespace shashoku::test
