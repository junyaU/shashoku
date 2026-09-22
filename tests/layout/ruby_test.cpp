#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "layout/counters.hpp"
#include "layout/test_support.hpp"

// ルビ（DESIGN.md Phase 7 / §6-4、ARCHITECTURE.md §3.8）。
// 「親文字 + <rt>」1 組が linebreak::Item 1 個（Atomic）になり、組の内部では改行しない。
// ルビ文字はベースラインの違う TextFragment として出す（variant は増やさない）。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Display;

// 親文字 16px / ルビ 8px（UA スタイルの 50%）。
constexpr float kBase = 16;
constexpr float kRuby = 8;
// 「ちょうど行の端に接する」を許すための許容差（float の積み上げ誤差ぶん）。
constexpr float kTolerance = 1.0F / 1024.0F;

// 注意: 共有ヘルパーの line_texts() は行の全 TextFragment を描画順に連結するので、
// ルビ文字もそこに入る（「親文字 → ルビ」の順）。親文字だけを見たいときは
// base_fragments() を使う。
//
// 行内の TextFragment を「ベースラインが行のものと同じか」で分ける。
std::vector<const TextFragment*> ruby_fragments(const LineBox& line) {
  std::vector<const TextFragment*> out;
  for (const TextFragment* fragment : text_fragments(line)) {
    if (fragment->baseline != line.baseline) {
      out.push_back(fragment);
    }
  }
  return out;
}

std::vector<const TextFragment*> base_fragments(const LineBox& line) {
  std::vector<const TextFragment*> out;
  for (const TextFragment* fragment : text_fragments(line)) {
    if (fragment->baseline == line.baseline) {
      out.push_back(fragment);
    }
  }
  return out;
}

std::vector<float> glyph_positions_of(const std::vector<const TextFragment*>& fragments) {
  std::vector<float> out;
  for (const TextFragment* fragment : fragments) {
    for (const PositionedGlyph& glyph : fragment->glyphs) {
      out.push_back(glyph.inline_position);
    }
  }
  return out;
}

// 親文字のグリフのペン位置（ルビ文字はベースラインが違うので入らない）。
std::vector<float> base_glyph_positions(const LineBox& line) {
  return glyph_positions_of(base_fragments(line));
}

std::vector<float> ruby_glyph_positions(const LineBox& line) {
  return glyph_positions_of(ruby_fragments(line));
}

// ---- 1 組のルビ -------------------------------------------------------------------

TEST(LayoutRuby, SinglePair) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);

  const std::vector<const TextFragment*> base = base_fragments(*lines[0]);
  const std::vector<const TextFragment*> ruby = ruby_fragments(*lines[0]);
  ASSERT_EQ(base.size(), 1U);
  ASSERT_EQ(ruby.size(), 1U);
  EXPECT_EQ(base[0]->text, "漢");
  EXPECT_EQ(ruby[0]->text, "かん");
  EXPECT_FLOAT_EQ(base[0]->font_size, kBase);
  EXPECT_FLOAT_EQ(ruby[0]->font_size, kRuby);
  // 親文字 16 とルビ 16（8 × 2 文字）は同じ幅なので、どちらも先頭から
  EXPECT_FLOAT_EQ(base[0]->inline_start, 0);
  EXPECT_FLOAT_EQ(ruby[0]->inline_start, 0);
  // ルビのベースライン = 親文字の内容領域の上端 − ルビの descent
  EXPECT_FLOAT_EQ(ruby[0]->baseline, lines[0]->baseline - fake_ascent(kBase) - fake_descent(kRuby));
  // 行はルビのぶん上に広がる
  EXPECT_FLOAT_EQ(lines[0]->baseline,
                  fake_ascent(kBase) + fake_ascent(kRuby) + fake_descent(kRuby));
}

// ---- JLREQ 3.3.6 の配分（1:2:…:2:1）。issue #28 ----------------------------------------
//
// 組の送り W = max(B, R) は変えず、短い方の余り E = |B − R| を「端 E/(2n)、字間 E/n」で配る
// （JLREQ 3.3.6:「親文字の文字列の字間の空き量の大きさ 2 に対して、ルビ文字の文字列の先頭から
// 親文字の文字列の先頭までの空き量…を 1 の比率で空けると体裁がよい」）。端の空きはルビ文字
// サイズの全角が上限で、止めたぶんは字間に回す（同 3.3.6 の注）。クラスタが 1 つなら中央。

// 規則を（実装の式ではなく上の文から）もう一度素直に書いたもの。count 個・送り step の
// クラスタを、送り advance の組に配ったときのペン位置。cap は端の空きの上限。
std::vector<float> distributed_positions(std::size_t count, float step, float advance, float cap) {
  std::vector<float> out;
  if (count == 0) {
    return out;
  }
  const auto n = static_cast<float>(count);
  const float extra = advance - (n * step);
  float lead = 0;
  float gap = 0;
  if (extra > 0) {
    if (count == 1) {
      lead = extra / 2;  // 中央（上限は掛けない）
    } else {
      lead = extra / (2 * n);
      gap = extra / n;
      if (lead > cap) {
        lead = cap;
        gap = (extra - (2 * cap)) / (n - 1);
      }
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(lead + (static_cast<float>(i) * (step + gap)));
  }
  return out;
}

// ルビが親文字より長い: 親文字を「端 E/(2n)、字間 E/n」で配る（JLREQ 3.3.6。issue #28）。
TEST(LayoutRuby, LongerRubyDistributesTheBase) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("写植"), rt("しゃしょく")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // 組の送り = max(16 × 2, 8 × 5) = 40。余り E = 8 を 2 クラスタに配る（端 2 / 字間 4）
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{2, 22}));
  EXPECT_FLOAT_EQ(ruby_fragments(line)[0]->inline_start, 0);
  // 行内の送りは 40 のまま（次の文字がそこから始まる = 行分割は変わらない）
  const auto next = build({block({ruby({text("写植"), rt("しゃしょく")}), text("都")})});
  const auto tree2 = run_layout(next, 400, measurer);
  ASSERT_TRUE(tree2.has_value());
  const LineBox& line2 = *all_lines(*tree2)[0];
  const std::vector<const TextFragment*> base = base_fragments(line2);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_EQ(base[1]->text, "都");
  EXPECT_FLOAT_EQ(base[1]->inline_start, 40);
}

// ルビが親文字より短い: ルビを同じ比率で配る。
TEST(LayoutRuby, ShorterRubyIsDistributed) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("図書館"), rt("としょ")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // 組の送り = max(48, 24) = 48。余り E = 24 を 3 クラスタに配る（端 4 / 字間 8）
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{0, 16, 32}));
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{4, 20, 36}));
}

// クラスタが 1 つなら中央に置く（配分する先の字間が無い）。上限（規則 5）も掛けない。
TEST(LayoutRuby, SingleClusterIsCentred) {
  FakeMeasurer measurer;
  // 親文字が 1 文字: (40 − 16) / 2 = 12
  const auto base_one = build({block({ruby({text("東"), rt("とうきょう")})})});
  const auto a = run_layout(base_one, 400, measurer);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(base_glyph_positions(*all_lines(*a)[0]), (std::vector<float>{12}));
  // ルビが 1 文字: (64 − 8) / 2 = 28。端 28 はルビの全角 8 を超えるが、中央置きなので止めない
  const auto ruby_one = build({block({ruby({text("東京都府"), rt("と")})})});
  const auto b = run_layout(ruby_one, 400, measurer);
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(ruby_glyph_positions(*all_lines(*b)[0]), (std::vector<float>{28}));
}

// 端の空きはルビ文字サイズの全角が上限で、止めたぶんは字間に回る（JLREQ 3.3.6 の注）。
TEST(LayoutRuby, EdgeSpaceIsCappedAtOneRubyEm) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("図書館員"), rt("とし")}), text("へ")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // 組の送り = 64、余り E = 48、m = 2。端は 48/4 = 12 ではなく上限 8 で止まり、
  // 字間が (48 − 8 × 2) / (2 − 1) = 32 になる
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{8, 48}));
  // 組の送りは 64 のまま
  const std::vector<const TextFragment*> base = base_fragments(line);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_FLOAT_EQ(base[1]->inline_start, 64);
}

// 親文字とルビの送りが等しい組は両方ベタ（配分する余りが無い）。
TEST(LayoutRuby, EqualWidthsSetBothSolid) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東京"), rt("とうきょ")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{0, 16}));
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{0, 8, 16, 24}));
}

// 配分は「組の内部の位置」だけを動かす。行分割器に渡る送り・行の範囲・固有寸法は変わらない。
TEST(LayoutRuby, DistributionDoesNotChangeTheLineBreaking) {
  FakeMeasurer measurer;
  // 組の送り 40（親文字 32 / ルビ 40）+ 「都」16 + 「市」16 = 72
  const auto make = [] {
    return std::vector<Tree>{ruby({text("写植"), rt("しゃしょく")}), text("都市")};
  };
  const auto tree = run_layout(build({block(make())}), 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_FLOAT_EQ(line.rect.inline_start, 0);
  EXPECT_FLOAT_EQ(line.rect.inline_size, 400);
  // 組の直後の文字 = 行分割器に渡した Atomic の送り（40）そのもの
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{2, 22, 40, 56}));

  // 固有寸法: max-content は組 40 + 2 文字 32 = 72、min-content は組の送り 40
  // （組は Atomic なので割れない）。flex の fit-content で測る（#5 と同じ手）
  const auto in_flex = [&make] {
    return build({flex({block(make())}, [](ComputedStyle& style) {
      style.flex_direction = style::FlexDirection::Column;
      style.align_items = style::AlignItems::FlexStart;
    })});
  };
  const auto wide = run_layout(in_flex(), 400, measurer);
  ASSERT_TRUE(wide.has_value());
  EXPECT_FLOAT_EQ(wide->root.blocks()->front().blocks()->front().rect.inline_size, 72);
  const auto narrow = run_layout(in_flex(), 20, measurer);
  ASSERT_TRUE(narrow.has_value());
  EXPECT_FLOAT_EQ(narrow->root.blocks()->front().blocks()->front().rect.inline_size, 40);
}

// 組み合わせの回帰テスト（issue #28）。
// {横書き, 縦書き} × {B < R, B > R, B == R} × {クラスタ 1 個, 2 個以上} × {字間なし, あり}。
// 期待値は規則から手で出したもの（実装の式を書き写さない）。親文字もルビも全角だけなので、
// B = 文字数 × (16 + 字間)、R = 文字数 × 8 で数えられる。
struct DistributionCase {
  std::string_view name;
  std::size_t base_chars = 0;
  std::size_t ruby_chars = 0;
  float letter_spacing = 0;
  std::vector<float> base;  // 親文字のペン位置
  std::vector<float> ruby;  // ルビのペン位置
};

std::vector<DistributionCase> distribution_cases() {
  return {
      // B == R: 両方ベタ
      {.name = "equal-single", .base_chars = 1, .ruby_chars = 2, .base = {0}, .ruby = {0, 8}},
      {.name = "equal-multi",
       .base_chars = 2,
       .ruby_chars = 4,
       .base = {0, 16},
       .ruby = {0, 8, 16, 24}},
      // B < R: 親文字を配る（n = 1 は中央）
      {.name = "longer-ruby-single",
       .base_chars = 1,
       .ruby_chars = 5,
       .base = {12},
       .ruby = {0, 8, 16, 24, 32}},
      {.name = "longer-ruby-two",  // E = 8, n = 2 → 端 2 / 字間 4
       .base_chars = 2,
       .ruby_chars = 5,
       .base = {2, 22},
       .ruby = {0, 8, 16, 24, 32}},
      {.name = "longer-ruby-three",  // E = 24, n = 3 → 端 4 / 字間 8
       .base_chars = 3,
       .ruby_chars = 9,
       .base = {4, 28, 52},
       .ruby = {0, 8, 16, 24, 32, 40, 48, 56, 64}},
      // B > R: ルビを配る（m = 1 は中央）
      {.name = "shorter-ruby-single",
       .base_chars = 2,
       .ruby_chars = 1,
       .base = {0, 16},
       .ruby = {12}},
      {.name = "shorter-ruby-three",  // E = 24, m = 3 → 端 4 / 字間 8
       .base_chars = 3,
       .ruby_chars = 3,
       .base = {0, 16, 32},
       .ruby = {4, 20, 36}},
      // 端の空きの上限（ルビ文字サイズの全角 = 8）で止まり、余りが字間に回る
      {.name = "capped-two",  // E = 48, m = 2 → 端 12 ではなく 8、字間 32
       .base_chars = 4,
       .ruby_chars = 2,
       .base = {0, 16, 32, 48},
       .ruby = {8, 48}},
      {.name = "capped-wide",  // E = 64, m = 2 → 端 8、字間 48
       .base_chars = 5,
       .ruby_chars = 2,
       .base = {0, 16, 32, 48, 64},
       .ruby = {8, 64}},
      // 字間あり（#16 / A37 と両立する。ルビ側に字間は入らない）
      {.name = "spacing-longer-ruby",  // B = 48, R = 56 → E = 8, n = 2 → 端 2 / 字間 4
       .base_chars = 2,
       .ruby_chars = 7,
       .letter_spacing = 8,
       .base = {2, 30},
       .ruby = {0, 8, 16, 24, 32, 40, 48}},
      {.name = "spacing-shorter-ruby",  // B = 48, R = 24 → E = 24, m = 3 → 端 4 / 字間 8
       .base_chars = 2,
       .ruby_chars = 3,
       .letter_spacing = 8,
       .base = {0, 24},
       .ruby = {4, 20, 36}},
      {.name = "spacing-single-base",  // B = 24, R = 32 → E = 8, n = 1 → 中央 4
       .base_chars = 1,
       .ruby_chars = 4,
       .letter_spacing = 8,
       .base = {4},
       .ruby = {0, 8, 16, 24}},
  };
}

void check_distribution(const DistributionCase& test_case, bool vertical) {
  const std::string label =
      std::string(test_case.name) + " vertical=" + std::to_string(static_cast<int>(vertical));
  constexpr std::string_view kBaseChars = "東京都府県市区町村";  // 全角 9 文字
  constexpr std::string_view kRubyChars = "あいうえおかきくけこ";
  FakeMeasurer measurer;
  std::vector<Tree> parts;
  for (std::size_t i = 0; i < test_case.base_chars; ++i) {
    parts.push_back(text(kBaseChars.substr(i * 3, 3)));  // 全角 1 文字 = UTF-8 で 3 バイト
  }
  parts.push_back(rt(kRubyChars.substr(0, test_case.ruby_chars * 3)));
  const float spacing = test_case.letter_spacing;
  std::vector<Tree> children{block({ruby(std::move(parts))}, [spacing](ComputedStyle& style) {
    style.letter_spacing = spacing;
  })};
  const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
  const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                             : run_layout(root, make_options(400), measurer);
  ASSERT_TRUE(tree.has_value()) << label;
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_EQ(base_glyph_positions(line), test_case.base) << label;
  EXPECT_EQ(ruby_glyph_positions(line), test_case.ruby) << label;

  // 組の送りは max(B, R) のまま（配分は組の内部だけを動かす）
  const float base_width = static_cast<float>(test_case.base_chars) * (kBase + spacing);
  const float ruby_width = static_cast<float>(test_case.ruby_chars) * kRuby;
  const float advance = std::max(base_width, ruby_width);
  // 前後の空きが同じ（配分は組の中で対称）
  EXPECT_NEAR(test_case.base.front(), advance - (test_case.base.back() + kBase + spacing),
              kTolerance)
      << label;
  EXPECT_NEAR(test_case.ruby.front(), advance - (test_case.ruby.back() + kRuby), kTolerance)
      << label;
}

TEST(LayoutRuby, DistributionCombinations) {
  for (const DistributionCase& test_case : distribution_cases()) {
    for (const bool vertical : {false, true}) {
      check_distribution(test_case, vertical);
    }
  }
}

// 親文字の font-size が混ざっていても、余りはクラスタの**間**に等しく配る（#17 と両立）。
// 行の高さは配分で変わらない（親文字の全クラスタの最大のまま）。
TEST(LayoutRuby, DistributionWithMixedFontSizesInTheBase) {
  const auto big = [](ComputedStyle& style) { style.font_size = 32; };
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    FakeMeasurer measurer;
    const auto make = [&big](std::string_view annotation) {
      return std::vector<Tree>{
          block({ruby({text("あ"), inline_box({text("い")}, big), rt(annotation)})})};
    };
    const auto options = vertical ? vertical_options(400, 400) : make_options(400);
    // 親文字 16 + 32 = 48、ルビ 7 × 8 = 56 → E = 8、n = 2（端 2 / 字間 4）
    const auto wide =
        vertical ? build_vertical(make("あいうえおかき")) : build(make("あいうえおかき"));
    const auto a = run_layout(wide, options, measurer);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(base_glyph_positions(*all_lines(*a)[0]), (std::vector<float>{2, 22}));
    // 配らない同じ親文字（ルビ 1 文字）と行の高さ・ベースラインが同じ
    const auto narrow = vertical ? build_vertical(make("あ")) : build(make("あ"));
    const auto b = run_layout(narrow, options, measurer);
    ASSERT_TRUE(b.has_value());
    EXPECT_FLOAT_EQ(all_lines(*a)[0]->rect.block_size, all_lines(*b)[0]->rect.block_size);
    EXPECT_FLOAT_EQ(all_lines(*a)[0]->baseline, all_lines(*b)[0]->baseline);
  }
}

// 親文字の一部を覆う background-color は、配分後のクラスタ位置に追従する（#16 と両立）。
// 配分で入れた空きは letter-spacing の字間と同じ扱いで、手前のクラスタの背景が覆う
// （隣り合う span の背景の間に隙間を開けない）。組の端に接するスコープは組の箱の端まで
// （A37。中央寄せのときと同じ扱い）。
TEST(LayoutRuby, BackgroundFollowsTheDistributedCluster) {
  const auto filled = [](ComputedStyle& style) { style.background_color = Color{0, 255, 0, 255}; };
  // 親文字 4 × 16 = 64、ルビ 12 × 8 = 96 → E = 32、n = 4（端 4 / 字間 8）
  // → 親文字は 4 / 28 / 52 / 76（中央置きのままなら 16 / 32 / 48 / 64）
  struct Case {
    std::size_t wrapped = 0;  // background-color を掛ける親文字の位置
    float inline_start = 0;
    float inline_end = 0;
  };
  for (const Case& test_case :
       {Case{.wrapped = 0, .inline_start = 0, .inline_end = 28},   // 組の頭に接する
        Case{.wrapped = 1, .inline_start = 28, .inline_end = 52},  // 真ん中（16 + 字間 8）
        Case{.wrapped = 3, .inline_start = 76, .inline_end = 96}}) {  // 組の末尾に接する
    for (const bool vertical : {false, true}) {
      SCOPED_TRACE(std::to_string(test_case.wrapped) +
                   " vertical=" + std::to_string(static_cast<int>(vertical)));
      FakeMeasurer measurer;
      const std::vector<std::string> base_chars{"写", "植", "機", "械"};
      std::vector<Tree> parts;
      for (std::size_t i = 0; i < base_chars.size(); ++i) {
        parts.push_back(i == test_case.wrapped ? inline_box({text(base_chars[i])}, filled)
                                               : text(base_chars[i]));
      }
      parts.push_back(rt("しゃしょくきかいですかね"));  // 12 文字
      std::vector<Tree> children{block({ruby(std::move(parts))})};
      const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
      const auto options = vertical ? vertical_options(400, 400) : make_options(400);
      const auto tree = run_layout(root, options, measurer);
      ASSERT_TRUE(tree.has_value());
      const LineBox& line = *all_lines(*tree)[0];
      EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{4, 28, 52, 76}));
      const std::vector<const InlineBackground*> painted = backgrounds(line);
      ASSERT_EQ(painted.size(), 1U);
      EXPECT_FLOAT_EQ(painted[0]->rect.inline_start, test_case.inline_start);
      EXPECT_FLOAT_EQ(painted[0]->rect.inline_end(), test_case.inline_end);
    }
  }
}

// モノルビ（1 つの <ruby> に 2 組）は組ごとに独立して配る。
TEST(LayoutRuby, MonorubyDistributesEachPairIndependently) {
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    FakeMeasurer measurer;
    std::vector<Tree> children{
        block({ruby({text("写植"), rt("しゃしょく"), text("機械"), rt("きか")})})};
    const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
    const auto options = vertical ? vertical_options(400, 400) : make_options(400);
    const auto tree = run_layout(root, options, measurer);
    ASSERT_TRUE(tree.has_value());
    const LineBox& line = *all_lines(*tree)[0];
    // 1 組目: B = 32 < R = 40 → 親文字を配る（端 2 / 字間 4）。送り 40
    // 2 組目: B = 32 > R = 16 → ルビを配る（端 4 / 字間 8）。送り 32
    EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{2, 22, 40, 56}));
    EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{0, 8, 16, 24, 32, 44, 60}));
  }
}

// text-align: justify でも組の内部の配分は変わらない（組は 1 アイテムで、広がるのは組の外側）。
// 組の後ろは漢字にしてある: 仮名だとルビの掛け（#28(b)）が効いて配分が変わるため。
TEST(LayoutRuby, JustifyKeepsTheDistributionInsideThePair) {
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    FakeMeasurer measurer;
    std::vector<Tree> children{
        block({ruby({text("写植"), rt("しゃしょく")}), text("東京都府県市区町村")},
              [](ComputedStyle& style) { style.text_align = style::TextAlign::Justify; })};
    const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
    const auto options = vertical ? vertical_options(400, 150) : make_options(150);
    const auto tree = run_layout(root, options, measurer);
    ASSERT_TRUE(tree.has_value());
    const std::vector<const LineBox*> lines = all_lines(*tree);
    ASSERT_GE(lines.size(), 2U);  // 最終行でない = 両端揃えが効く行
    const std::vector<float> base = base_glyph_positions(*lines[0]);
    ASSERT_GE(base.size(), 2U);
    // 行頭の組: 端 2 / 字間 4 のまま（均等割りの空きは組の外側にだけ入る）
    EXPECT_FLOAT_EQ(base[0], 2);
    EXPECT_FLOAT_EQ(base[1], 22);
    EXPECT_EQ(ruby_glyph_positions(*lines[0]), (std::vector<float>{0, 8, 16, 24, 32}));
  }
}

// 禁則で組が行頭・行末に来ても、組の内部の配分は行の中での位置に付いていく。
// 前後の文字は漢字にしてある: 仮名だとルビの掛け（#28(b)）が効いて配分が変わるため。
TEST(LayoutRuby, DistributionFollowsThePairToTheLineEdges) {
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    FakeMeasurer measurer;
    const auto options = vertical ? vertical_options(400, 88) : make_options(88);
    // 行末: 東京都（48）+ 組（40）= 88 でちょうど収まる
    std::vector<Tree> tail{block({text("東京都"), ruby({text("写植"), rt("しゃしょく")})})};
    const auto at_end = vertical ? build_vertical(std::move(tail)) : build(std::move(tail));
    const auto a = run_layout(at_end, options, measurer);
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(all_lines(*a).size(), 1U);
    EXPECT_EQ(base_glyph_positions(*all_lines(*a)[0]),
              (std::vector<float>{0, 16, 32, 50, 70}));  // 組は 48 から。48 + 2 / 48 + 22
    EXPECT_EQ(ruby_glyph_positions(*all_lines(*a)[0]), (std::vector<float>{48, 56, 64, 72, 80}));
    // 行頭: 東京都府（64）+ 組（40）= 104 > 88 なので組が次の行の頭に落ちる
    std::vector<Tree> head{block({text("東京都府"), ruby({text("写植"), rt("しゃしょく")})})};
    const auto at_start = vertical ? build_vertical(std::move(head)) : build(std::move(head));
    const auto b = run_layout(at_start, options, measurer);
    ASSERT_TRUE(b.has_value());
    ASSERT_EQ(all_lines(*b).size(), 2U);
    EXPECT_EQ(base_glyph_positions(*all_lines(*b)[1]), (std::vector<float>{2, 22}));
    EXPECT_EQ(ruby_glyph_positions(*all_lines(*b)[1]), (std::vector<float>{0, 8, 16, 24, 32}));
  }
}

// 1 つの <ruby> に複数組。
TEST(LayoutRuby, MultiplePairsInOneRubyElement) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とう"), text("京"), rt("きょう")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const TextFragment*> ruby = ruby_fragments(line);
  ASSERT_EQ(ruby.size(), 2U);
  EXPECT_EQ(ruby[0]->text, "とう");
  EXPECT_EQ(ruby[1]->text, "きょう");
  // 1 組目は max(16, 16) = 16、2 組目は max(16, 24) = 24
  EXPECT_FLOAT_EQ(ruby[0]->inline_start, 0);
  EXPECT_FLOAT_EQ(ruby[1]->inline_start, 16);
  const std::vector<const TextFragment*> base = base_fragments(line);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_FLOAT_EQ(base[1]->inline_start, 16 + 4);  // (24 − 16) / 2
}

// <rt> が続かない親文字はルビなしの普通のテキスト。
TEST(LayoutRuby, BaseWithoutAnnotationStaysPlainText) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とう"), text("都")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_EQ(ruby_fragments(line).size(), 1U);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東とう都"}));
}

// 親文字の span（色・背景・フォールバック）は普通のテキストと同じに効く。
TEST(LayoutRuby, BaseKeepsSpanStyling) {
  FakeMeasurer measurer;
  measurer.fallback_chars = U"京";
  const auto root = build({block(
      {inline_box({ruby({text("東"), text("京"), rt("とうきょう")})},
                  [](ComputedStyle& style) { style.background_color = Color{0, 255, 0, 255}; })})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // フォールバックで親文字の断片が 2 つに分かれる
  const std::vector<const TextFragment*> base = base_fragments(line);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_EQ(base[0]->font, 0U);
  EXPECT_EQ(base[1]->font, 1U);
  // span の背景は組ぜんぶを覆う
  ASSERT_EQ(backgrounds(line).size(), 1U);
  EXPECT_FLOAT_EQ(backgrounds(line)[0]->rect.inline_size, 40);
}

// ---- 行の高さ ---------------------------------------------------------------------

// line-height に余裕があれば、ルビが付いても行の高さは変わらない（DESIGN.md §6-4）。
TEST(LayoutRuby, DoesNotGrowTheLineWhenLineHeightHasRoom) {
  FakeMeasurer measurer;
  const auto tall = [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Number, 3};
  };
  const auto plain = build({block({text("漢")}, tall)});
  const auto annotated = build({block({ruby({text("漢"), rt("かん")})}, tall)});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(annotated, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_FLOAT_EQ(all_lines(*a)[0]->rect.block_size, 48);
  EXPECT_FLOAT_EQ(all_lines(*b)[0]->rect.block_size, 48);
  EXPECT_FLOAT_EQ(all_lines(*a)[0]->baseline, all_lines(*b)[0]->baseline);
}

// line-height が小さければ、ルビのぶんだけ block-start 側に広がる。
TEST(LayoutRuby, GrowsTheLineWhenLineHeightIsTight) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const float above = fake_ascent(kBase) + fake_ascent(kRuby) + fake_descent(kRuby);
  EXPECT_FLOAT_EQ(line.baseline, above);
  EXPECT_FLOAT_EQ(line.rect.block_size, above + fake_descent(kBase));
}

// ---- 親文字のスタイルが混ざるときの行の高さ。issue #17 ---------------------------------
//
// CSS Ruby 1 §2: ruby base は inline box として扱う → 行の高さには親文字の**全区間**が
// 参加する（CSS 2.1 §10.8）。§3.4: 注釈（<rt>）は行の高さに参加しない。
// 行分割ポリシーの代表の文字（A28）から幾何を取ると、2 文字目以降の指定が絵から落ちる。

// ベースラインから block-start 側 / block-end 側への広がり。
float above_of(const LineBox& line) { return line.baseline - line.rect.block_start; }
float below_of(const LineBox& line) { return line.rect.block_end() - line.baseline; }

// 親文字の 2 文字目が大きいと、行もベースラインもそのぶん下がる（横書き）。
TEST(LayoutRuby, BiggerFontOnTheSecondBaseCharacterGrowsTheLine) {
  FakeMeasurer measurer;
  const auto big = [](ComputedStyle& style) { style.font_size = 80; };
  const auto plain = build({block({text("あ"), inline_box({text("い")}, big)})});
  const auto annotated =
      build({block({ruby({text("あ"), inline_box({text("い")}, big), rt("ab")})})});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(annotated, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  const LineBox& without = *all_lines(*a)[0];
  const LineBox& with = *all_lines(*b)[0];
  // ルビなしの行より低くならない。ベースラインは 80px の ascent 以上
  // （端にちょうど接する場合があるので、積み上げ誤差ぶんの許容差を引く）
  EXPECT_GE(with.rect.block_size, without.rect.block_size);
  EXPECT_GE(above_of(with), fake_ascent(80) - kTolerance);
  // block-end 側も 80px の descent ぶん空く（descent 側が落ちていた）
  EXPECT_GE(below_of(with), fake_descent(80) - kTolerance);
}

// line-height でも同じ。
TEST(LayoutRuby, LineHeightOnTheSecondBaseCharacterGrowsTheLine) {
  FakeMeasurer measurer;
  const auto tall = [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Px, 100};
  };
  for (const bool vertical : {false, true}) {
    std::vector<Tree> children{
        block({ruby({text("あ"), inline_box({text("い")}, tall), rt("ab")})})};
    const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
    const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                               : run_layout(root, make_options(400), measurer);
    ASSERT_TRUE(tree.has_value()) << vertical;
    EXPECT_GE(all_lines(*tree)[0]->rect.block_size, 100) << vertical;
  }
}

// 大きい文字が先頭でも末尾でも結果が同じ（代表の文字に依らない）。
TEST(LayoutRuby, LineHeightDoesNotDependOnTheOrderOfBaseStyles) {
  FakeMeasurer measurer;
  const auto big = [](ComputedStyle& style) { style.font_size = 80; };
  const auto first = build({block({ruby({inline_box({text("あ")}, big), text("い"), rt("ab")})})});
  const auto second = build({block({ruby({text("あ"), inline_box({text("い")}, big), rt("ab")})})});
  const auto a = run_layout(first, 400, measurer);
  const auto b = run_layout(second, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_FLOAT_EQ(all_lines(*a)[0]->rect.block_size, all_lines(*b)[0]->rect.block_size);
  EXPECT_FLOAT_EQ(above_of(*all_lines(*a)[0]), above_of(*all_lines(*b)[0]));
}

// A27 の不変条件をルビでも: 親文字を素の span で包んでも結果が変わらない。
TEST(LayoutRuby, WrappingTheBaseInAPlainSpanChangesNothing) {
  FakeMeasurer measurer;
  const auto big = [](ComputedStyle& style) { style.font_size = 80; };
  const auto bare = build({block({ruby({text("あ"), inline_box({text("い")}, big), rt("ab")})})});
  const auto wrapped = build({block(
      {ruby({inline_box({text("あ")}), inline_box({inline_box({text("い")}, big)}), rt("ab")})})});
  const auto a = run_layout(bare, 400, measurer);
  const auto b = run_layout(wrapped, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_FLOAT_EQ(all_lines(*a)[0]->rect.block_size, all_lines(*b)[0]->rect.block_size);
  EXPECT_FLOAT_EQ(above_of(*all_lines(*a)[0]), above_of(*all_lines(*b)[0]));
  EXPECT_EQ(base_glyph_positions(*all_lines(*a)[0]), base_glyph_positions(*all_lines(*b)[0]));
}

// 組み合わせの回帰テストの 1 ケース（親文字の 2 文字目に別のスタイルを当てる）。
struct HeightCase {
  std::string_view name;
  StyleFn second;
  float max_base_font_size = kBase;  // 親文字の最大 font-size（ルビの張り出しに効く）
};

std::vector<HeightCase> height_cases() {
  return {
      {.name = "font-size",
       .second = [](ComputedStyle& style) { style.font_size = 80; },
       .max_base_font_size = 80},
      {.name = "line-height",
       .second =
           [](ComputedStyle& style) {
             style.line_height = style::LineHeight{style::LineHeight::Kind::Px, 100};
           }},
      {.name = "font-weight", .second = [](ComputedStyle& style) { style.font_weight = 700; }},
      {.name = "font-family",
       .second = [](ComputedStyle& style) { style.font_family = {"Other"}; }},
  };
}

void check_line_height(const HeightCase& test_case, bool vertical) {
  const std::string label =
      std::string(test_case.name) + " vertical=" + std::to_string(static_cast<int>(vertical));
  FakeMeasurer measurer;
  const auto base_children = [&test_case] {
    return std::vector<Tree>{text("あ"), inline_box({text("い")}, test_case.second)};
  };
  std::vector<Tree> plain_children{block(base_children())};
  std::vector<Tree> ruby_parts = base_children();
  ruby_parts.push_back(rt("ab"));
  std::vector<Tree> ruby_children{block({ruby(std::move(ruby_parts))})};

  const auto plain =
      vertical ? build_vertical(std::move(plain_children)) : build(std::move(plain_children));
  const auto annotated =
      vertical ? build_vertical(std::move(ruby_children)) : build(std::move(ruby_children));
  const auto options = vertical ? vertical_options(400, 400) : make_options(400);
  const auto a = run_layout(plain, options, measurer);
  const auto b = run_layout(annotated, options, measurer);
  ASSERT_TRUE(a.has_value()) << label;
  ASSERT_TRUE(b.has_value()) << label;
  const LineBox& without = *all_lines(*a)[0];
  const LineBox& with = *all_lines(*b)[0];

  // ルビの張り出し（横は ascent ベース、縦は em ベース。書字方向で違うのは既知。§4）
  const float overhang = vertical ? (test_case.max_base_font_size / 2) + kRuby
                                  : fake_ascent(test_case.max_base_font_size) + fake_ascent(kRuby) +
                                        fake_descent(kRuby);
  // 行の高さは「ルビなしの同じ内容」から決まる: block-start 側だけがルビのぶん広がる
  EXPECT_FLOAT_EQ(above_of(with), std::max(above_of(without), overhang)) << label;
  EXPECT_FLOAT_EQ(below_of(with), below_of(without)) << label;
  EXPECT_GE(with.rect.block_size, without.rect.block_size) << label;
}

// 組み合わせの回帰テスト: {横書き, 縦書き} × {font-size 混在, line-height 混在,
// font-weight 混在, font-family 混在}。
TEST(LayoutRuby, BaseStyleCombinationsDecideTheLineHeight) {
  for (const HeightCase& test_case : height_cases()) {
    for (const bool vertical : {false, true}) {
      check_line_height(test_case, vertical);
    }
  }
}

// ---- 行分割 -----------------------------------------------------------------------

// 組の内部では割らない（Atomic）。収まらなければはみ出す（A4）。
TEST(LayoutRuby, DoesNotBreakInsideAPair) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東京"), rt("とうきょう")})})});
  const auto tree = run_layout(root, 20, measurer);
  ASSERT_TRUE(tree.has_value());
  ASSERT_EQ(all_lines(*tree).size(), 1U);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東京とうきょう"}));
}

// 組どうしの間では割れる。
TEST(LayoutRuby, BreaksBetweenPairs) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("東"), rt("とう"), text("京"), rt("きょう")})})});
  const auto tree = run_layout(root, 20, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東とう", "京きょう"}));
}

// ルビ組の直後の句読点は行頭に来ない（Atomic の後ろでも禁則が効く）。
TEST(LayoutRuby, PunctuationAfterAPairNeverStartsALine) {
  FakeMeasurer measurer;
  const auto root = build({block({text("あい"), ruby({text("漢"), rt("かん")}), text("、うえ")})});
  const auto tree = run_layout(root, 48, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<std::string> lines = line_texts(*tree);
  ASSERT_GE(lines.size(), 2U);
  for (const std::string& line : lines) {
    EXPECT_NE(line.rfind("、", 0), 0U) << line;
  }
}

// ルビを含む段落の両端揃え（A13）。組は 1 アイテムなので、その前後だけが広がる。
TEST(LayoutRuby, JustifyTreatsThePairAsOneItem) {
  FakeMeasurer measurer;
  const auto root = build(
      {block({text("あいうえお"), ruby({text("漢"), rt("かん")}), text("かきくけこさしすせそ")},
             [](ComputedStyle& style) { style.text_align = style::TextAlign::Justify; })});
  const auto tree = run_layout(root, 200, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_GE(lines.size(), 2U);
  // 最初の行は content の端までそろう
  const std::vector<float> positions = glyph_positions(*lines[0]);
  ASSERT_FALSE(positions.empty());
  EXPECT_FLOAT_EQ(positions.front(), 0);
}

// ---- 縦書きのルビ ------------------------------------------------------------------

// 縦書きではルビは親文字の右（block-start 側）に来る。
// block 座標は右端からの距離なので、block-start 側 = 値が小さい側（横書きの「上」と同じ向き）。
TEST(LayoutRuby, VerticalRubyGoesToTheBlockStartSide) {
  FakeMeasurer measurer;
  const auto root = build_vertical({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, vertical_options(400, 200), measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const TextFragment*> ruby = ruby_fragments(line);
  ASSERT_EQ(ruby.size(), 1U);
  // 中心軸から親文字の半分 + ルビの半分ぶん block-start 側（= 紙面の右）へ
  EXPECT_FLOAT_EQ(ruby[0]->baseline, line.baseline - (kBase / 2) - (kRuby / 2));
  EXPECT_LT(ruby[0]->baseline, line.baseline);
  // 行は中心軸から block-start 側に「親文字の半分 + ルビ」ぶん広がる
  EXPECT_FLOAT_EQ(line.baseline, (kBase / 2) + kRuby);
  // 空けた側と置いた側が同じ: ルビは行の block-start 端の内側に収まる
  EXPECT_GE(ruby[0]->baseline - (kRuby / 2), line.rect.block_start - kTolerance);
}

// 横書きのルビも block-start 側（= 上 = block 座標が小さい側）。
TEST(LayoutRuby, HorizontalRubyIsOnTheBlockStartSide) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const TextFragment*> ruby = ruby_fragments(line);
  ASSERT_EQ(ruby.size(), 1U);
  EXPECT_LT(ruby[0]->baseline, line.baseline);
  EXPECT_GE(ruby[0]->baseline - fake_ascent(kRuby), line.rect.block_start - kTolerance);
}

// line-height が小さいとき、行の block-start 端はルビを含むところまで広がる。
TEST(LayoutRuby, VerticalLineGrowsOnTheBlockStartSideForRuby) {
  FakeMeasurer measurer;
  const auto tight = [](ComputedStyle& style) {
    style.line_height = style::LineHeight{style::LineHeight::Kind::Px, 16};
  };
  const auto plain = build_vertical({block({text("漢")}, tight)});
  const auto annotated = build_vertical({block({ruby({text("漢"), rt("かん")})}, tight)});
  const auto a = run_layout(plain, vertical_options(400, 200), measurer);
  const auto b = run_layout(annotated, vertical_options(400, 200), measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  const LineBox& without = *all_lines(*a)[0];
  const LineBox& with = *all_lines(*b)[0];
  // ルビのぶん行が広がり、広がったのは block-start 側（中心軸が block-end 寄りに動く）
  EXPECT_GT(with.rect.block_size, without.rect.block_size);
  EXPECT_GT(with.baseline, without.baseline);
  EXPECT_FLOAT_EQ(with.rect.block_end() - with.baseline,
                  without.rect.block_end() - without.baseline);
}

// 前の行（block-start 側 = 右隣）とルビが重ならない。
TEST(LayoutRuby, VerticalRubyDoesNotReachIntoThePreviousLine) {
  FakeMeasurer measurer;
  // 1 行目は普通の文字、2 行目にルビ。行の長さ 32（全角 2 文字）で折り返す
  const auto root = build_vertical({block({text("あい"), br(), ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, vertical_options(400, 32), measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 2U);
  // 行は block 方向に隙間なく積まれる
  EXPECT_FLOAT_EQ(lines[0]->rect.block_end(), lines[1]->rect.block_start);
  const std::vector<const TextFragment*> ruby = ruby_fragments(*lines[1]);
  ASSERT_EQ(ruby.size(), 1U);
  // ルビの block-start 端が 1 行目に食い込まない
  EXPECT_GE(ruby[0]->baseline - (kRuby / 2), lines[1]->rect.block_start - kTolerance);
  EXPECT_GE(ruby[0]->baseline - (kRuby / 2), lines[0]->rect.block_end() - kTolerance);
}

// ---- 字間（letter-spacing）。issue #16 ------------------------------------------------
//
// CSS Ruby 1 §2: "ruby bases … are treated as inline boxes, and all properties that apply to
// inline boxes … also apply to them"。親文字は通常のインライン内容と同じに組む。
// つまり「計測（行分割器に渡す送り）」と「配置」が同じ 1 本の道を通る。

// 親文字のグリフ位置が、同じ指定の通常テキストと一致する。
TEST(LayoutRuby, BaseWithLetterSpacingIsPlacedLikePlainText) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto plain = build({block({text("東京都")}, spaced)});
  const auto annotated = build({block({ruby({text("東京都"), rt("と")})}, spaced)});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(annotated, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  // 親文字 3 × (16 + 8) = 72 はルビ 8 より長いので、組の中でのずらしは 0
  EXPECT_EQ(base_glyph_positions(*all_lines(*b)[0]), (std::vector<float>{0, 24, 48}));
  EXPECT_EQ(base_glyph_positions(*all_lines(*b)[0]), glyph_positions(*all_lines(*a)[0]));
}

// ルビ文字は「実際に配置された親文字」の中央に来る。
TEST(LayoutRuby, RubyIsCentredOverThePlacedBase) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto root = build({block({ruby({text("東京都"), rt("と")})}, spaced)});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<float> base = base_glyph_positions(line);
  const std::vector<float> ruby = ruby_glyph_positions(line);
  ASSERT_EQ(base.size(), 3U);
  ASSERT_EQ(ruby.size(), 1U);
  // 配置後の親文字が占める範囲は [最初のグリフ, 最後のグリフ + その送り]。
  // 送りは字間込み（通常テキストと同じ）なので、末尾の字間も範囲に入る
  const float base_centre = (base.front() + base.back() + kBase + 8) / 2;
  EXPECT_FLOAT_EQ(ruby.front() + (kRuby / 2), base_centre);
}

// 性質テスト: 行分割器に渡した送り（= 組の直後の文字が始まる位置）と、配置後の親文字の
// 範囲が一致する。計測と配置が別の式になっていると、ここが食い違う（#16 の本体）。
TEST(LayoutRuby, MeasuredAdvanceMatchesThePlacedBase) {
  for (const bool vertical : {false, true}) {
    for (const float spacing : {0.0F, 8.0F, -4.0F}) {
      FakeMeasurer measurer;
      const auto spaced = [spacing](ComputedStyle& style) { style.letter_spacing = spacing; };
      const auto children = [&spaced] {
        return std::vector<Tree>{block({ruby({text("東京"), rt("と")}), text("次")}, spaced)};
      };
      const auto root = vertical ? build_vertical(children()) : build(children());
      const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                                 : run_layout(root, make_options(400), measurer);
      ASSERT_TRUE(tree.has_value()) << vertical << " " << spacing;
      const LineBox& line = *all_lines(*tree)[0];
      const std::vector<float> base = base_glyph_positions(line);
      ASSERT_EQ(base.size(), 3U) << vertical << " " << spacing;  // 東 京 次
      // 親文字 2 × (16 + 字間) はルビ 8 より長いので、組の送り = 親文字の送り
      const float advance = 2 * (kBase + spacing);
      EXPECT_FLOAT_EQ(base[0], 0) << vertical << " " << spacing;
      EXPECT_FLOAT_EQ(base[1], kBase + spacing) << vertical << " " << spacing;
      // 配置後の親文字の終わり（最後のクラスタの送りまで）= 組の直後の文字の開始位置
      EXPECT_FLOAT_EQ(base[1] + kBase + spacing, advance) << vertical << " " << spacing;
      EXPECT_FLOAT_EQ(base[2], advance) << vertical << " " << spacing;
    }
  }
}

// 字間のあるルビでも、色だけの span はグリフ位置を動かさない（#8 / A27 を字間ありで）。
TEST(LayoutRuby, ColorSpanInTheBaseDoesNotMoveGlyphsWithLetterSpacing) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto red = [](ComputedStyle& style) { style.color = Color{255, 0, 0, 255}; };
  const auto plain = build({block({ruby({text("東京都"), rt("と")})}, spaced)});
  const auto split = build(
      {block({ruby({text("東"), inline_box({text("京")}, red), text("都"), rt("と")})}, spaced)});
  const auto a = run_layout(plain, 400, measurer);
  const auto b = run_layout(split, 400, measurer);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(base_fragments(*all_lines(*b)[0]).size(), 3U);  // 色の境界で断片は分かれる
  EXPECT_EQ(base_glyph_positions(*all_lines(*b)[0]), base_glyph_positions(*all_lines(*a)[0]));
}

// 親文字の一部だけを覆う background-color は、通常テキストと同じ矩形になる（#16 の決定 4）。
TEST(LayoutRuby, BackgroundOnPartOfTheBaseMatchesPlainText) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 8; };
  const auto filled = [](ComputedStyle& style) { style.background_color = Color{0, 255, 0, 255}; };
  struct Case {
    std::string_view name;
    bool wrap_first;  // true: 先頭 2 文字を包む / false: 真ん中の 1 文字だけ包む
  };
  for (const Case& test_case : {Case{"middle", false}, Case{"leading", true}}) {
    const auto pieces = [&](bool as_ruby) {
      std::vector<Tree> children;
      if (test_case.wrap_first) {
        children.push_back(inline_box({text("東"), text("京")}, filled));
        children.push_back(text("都"));
      } else {
        children.push_back(text("東"));
        children.push_back(inline_box({text("京")}, filled));
        children.push_back(text("都"));
      }
      if (as_ruby) {
        children.push_back(rt("と"));
        return std::vector<Tree>{block({ruby(std::move(children))}, spaced)};
      }
      return std::vector<Tree>{block(std::move(children), spaced)};
    };
    const auto a = run_layout(build(pieces(false)), 400, measurer);
    const auto b = run_layout(build(pieces(true)), 400, measurer);
    ASSERT_TRUE(a.has_value()) << test_case.name;
    ASSERT_TRUE(b.has_value()) << test_case.name;
    const std::vector<const InlineBackground*> plain = backgrounds(*all_lines(*a)[0]);
    const std::vector<const InlineBackground*> ruby = backgrounds(*all_lines(*b)[0]);
    ASSERT_EQ(plain.size(), 1U) << test_case.name;
    ASSERT_EQ(ruby.size(), 1U) << test_case.name;
    EXPECT_FLOAT_EQ(ruby[0]->rect.inline_start, plain[0]->rect.inline_start) << test_case.name;
    EXPECT_FLOAT_EQ(ruby[0]->rect.inline_size, plain[0]->rect.inline_size) << test_case.name;
  }
}

// <rt> の letter-spacing は適用しない（#16 の決定 3 / ARCHITECTURE.md §3.8）。
// 和文ルビで親文字の字間がルビ側にも掛かると、ルビが親文字より広がって不自然になる。
TEST(LayoutRuby, LetterSpacingInsideRtIsNotApplied) {
  FakeMeasurer measurer;
  const auto spaced = [](ComputedStyle& style) { style.letter_spacing = 10; };
  const auto root = build({block({ruby({text("東京"), rt("とうきょう", spaced)}), text("次")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // ルビ 5 文字 × 8 = 40（字間 10 が掛かれば 90 になる）
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{0, 8, 16, 24, 32}));
  // 組の送りも 40 のまま（親文字 32 より長い側が決める）
  const std::vector<float> base = base_glyph_positions(line);
  ASSERT_EQ(base.size(), 3U);
  EXPECT_FLOAT_EQ(base[2], 40);
}

// 組み合わせの回帰テストの 1 ケース。親文字は全角だけにしてあるので、送りは
// 「文字数 × (16 + 字間)」で手計算できる（横書きと縦書きで同じ値になる）。
struct SpacingCase {
  std::string_view name;
  std::vector<std::string> base;  // 1 要素 = 全角 1 文字
  std::string annotation;
  std::size_t annotation_chars = 0;
  bool split_span = false;  // 2 文字目を色だけの span で包む
};

std::vector<SpacingCase> spacing_cases() {
  return {
      {.name = "single-base", .base = {"東"}, .annotation = "とう", .annotation_chars = 2},
      {.name = "multi-base", .base = {"東", "京", "都"}, .annotation = "と", .annotation_chars = 1},
      {.name = "span-in-base",
       .base = {"東", "京", "都"},
       .annotation = "と",
       .annotation_chars = 1,
       .split_span = true},
      {.name = "longer-ruby", .base = {"東"}, .annotation = "とうきょう", .annotation_chars = 5},
  };
}

std::vector<Tree> ruby_children(const SpacingCase& test_case, float spacing) {
  const auto red = [](ComputedStyle& style) { style.color = Color{255, 0, 0, 255}; };
  std::vector<Tree> parts;
  for (std::size_t i = 0; i < test_case.base.size(); ++i) {
    if (test_case.split_span && i == 1) {
      parts.push_back(inline_box({text(test_case.base[i])}, red));
    } else {
      parts.push_back(text(test_case.base[i]));
    }
  }
  parts.push_back(rt(test_case.annotation));
  return {block({ruby(std::move(parts))},
                [spacing](ComputedStyle& style) { style.letter_spacing = spacing; })};
}

void check_letter_spacing(const SpacingCase& test_case, bool vertical, float spacing) {
  const std::string label = std::string(test_case.name) +
                            " vertical=" + std::to_string(static_cast<int>(vertical)) +
                            " spacing=" + std::to_string(spacing);
  FakeMeasurer measurer;
  std::vector<Tree> children = ruby_children(test_case, spacing);
  const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
  const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                             : run_layout(root, make_options(400), measurer);
  ASSERT_TRUE(tree.has_value()) << label;
  const LineBox& line = *all_lines(*tree)[0];

  const float step = kBase + spacing;
  const float base_width = static_cast<float>(test_case.base.size()) * step;
  const float rt_width = static_cast<float>(test_case.annotation_chars) * kRuby;
  const float advance = std::max(base_width, rt_width);

  // 短い方に余りを配る（JLREQ 3.3.6。#28）。端の上限はルビ文字サイズの全角
  const std::vector<float> expected_base =
      distributed_positions(test_case.base.size(), step, advance, kRuby);
  const std::vector<float> expected_ruby =
      distributed_positions(test_case.annotation_chars, kRuby, advance, kRuby);
  EXPECT_EQ(base_glyph_positions(line), expected_base) << label;
  EXPECT_EQ(ruby_glyph_positions(line), expected_ruby) << label;
  // 配分は組の中で対称（前後の空きが同じ）。親文字とルビの中心はどちらも組の中心に来る
  EXPECT_NEAR(expected_base.front(), advance - (expected_base.back() + step), kTolerance) << label;
  EXPECT_NEAR(expected_ruby.front(), advance - (expected_ruby.back() + kRuby), kTolerance) << label;
}

// 組み合わせの回帰テスト: {横書き, 縦書き} × {字間 0, 正, 負} ×
// {親文字 1 文字, 複数文字, 装飾 span 入り, ルビの方が長い}。
TEST(LayoutRuby, LetterSpacingCombinations) {
  for (const SpacingCase& test_case : spacing_cases()) {
    for (const bool vertical : {false, true}) {
      for (const float spacing : {0.0F, 8.0F, -4.0F}) {
        check_letter_spacing(test_case, vertical, spacing);
      }
    }
  }
}

// ---- JLREQ 3.3.8 のルビの掛け。issue #28 (b) -------------------------------------------
//
// ルビが親文字より長いとき、余り E のうち前後の文字に**はみ出してよい量**を掛ける。
// 掛けてよい相手は平仮名・片仮名（長音・小書きを含む。JLREQ の cl-15 / cl-16 / cl-10 /
// cl-11）だけで、漢字等（cl-19）・欧文・数字・約物には掛けない。掛ける量の上限は
// ルビ文字サイズの全角（<rt> の 1em）。行頭・行末では掛けない（版面の外に出さない）。
// 掛けたぶんだけ組の送りが縮むので、**改行位置が変わりうる**。
// 掛けきれずに残った余りは (a) の配分（1:2:…:2:1）で組の内部に配る。

// 組の前後に置く文字と、その結果の期待値（行の先頭からのペン位置）。
struct OverhangCase {
  std::string_view name;
  std::string_view lead;          // 組の前の文字（空なら組が段落の先頭）
  bool break_after_lead = false;  // lead の直後で改行する（組が行頭に来る）
  std::string_view trail;         // 組の後ろの文字（空なら組が段落の末尾）
  std::string_view annotation = "さくら";  // ルビ（全角 1 文字 = 8px）
  std::vector<float> base;                 // 親文字のペン位置
  std::vector<float> ruby;                 // ルビのペン位置
  float trail_start = 0;  // 組の後ろの文字のペン位置（trail が空なら見ない）
};

// 親文字は「桜」1 文字（16px）。ルビ 3 文字なら W = 24、余り E = 8。
std::vector<OverhangCase> overhang_cases() {
  return {
      // 行の中。掛けられる側の数で配り方が変わる
      {.name = "both-kana",  // 前後 4px ずつ → 組の送りは 16（= 親文字）になる
       .lead = "の",
       .trail = "の",
       .base = {16},
       .ruby = {12, 20, 28},
       .trail_start = 32},
      {.name = "lead-kana-only",  // 前だけ。上限 8px まで前に寄せる
       .lead = "の",
       .trail = "木",
       .base = {16},
       .ruby = {8, 16, 24},
       .trail_start = 32},
      {.name = "trail-kana-only",
       .lead = "木",
       .trail = "の",
       .base = {16},
       .ruby = {16, 24, 32},
       .trail_start = 32},
      {.name = "no-kana",  // 漢字等には掛けない → (a) の配分のまま（1 クラスタは中央）
       .lead = "木",
       .trail = "木",
       .base = {20},
       .ruby = {16, 24, 32},
       .trail_start = 40},
      {.name = "katakana-and-choon",  // 片仮名・長音も掛けてよい
       .lead = "ー",
       .trail = "ア",
       .base = {16},
       .ruby = {12, 20, 28},
       .trail_start = 32},
      {.name = "punctuation-is-not-kana",  // 約物・数字・欧文には掛けない
       .lead = "、",
       .trail = "A",
       .base = {20},
       .ruby = {16, 24, 32},
       .trail_start = 40},
      // 行頭・行末では掛けない。掛けきれなかった余りは組の内部に配る
      {.name = "line-start",  // 段落の先頭（前に文字が無い）
       .lead = "",
       .trail = "の",
       .base = {0},
       .ruby = {0, 8, 16},
       .trail_start = 16},
      {.name = "after-break",  // <br> は掛けてよい相手ではない（前は無しと同じ）
       .lead = "の",
       .break_after_lead = true,
       .trail = "の",
       .base = {0},
       .ruby = {0, 8, 16},
       .trail_start = 16},
      {.name = "line-end",  // 段落の末尾（後ろに文字が無い）
       .lead = "の",
       .trail = "",
       .base = {16},
       .ruby = {8, 16, 24}},
      // 上限（ルビ文字サイズの全角 = 8px）。掛けきれない余りは (a) の配分に回る
      {.name = "capped",  // E = 24 だが前後 8px ずつしか掛けられない
       .lead = "の",
       .trail = "の",
       .annotation = "さくらです",
       .base = {20},  // 残り 8 を 1 クラスタに配る = 中央
       .ruby = {8, 16, 24, 32, 40},
       .trail_start = 40},
      // 掛ける余りが無い組は動かない
      {.name = "equal-width",
       .lead = "の",
       .trail = "の",
       .annotation = "さく",
       .base = {16},
       .ruby = {16, 24},
       .trail_start = 32},
      {.name = "shorter-ruby",  // ルビの方が短い組は掛けない（はみ出していない）
       .lead = "の",
       .trail = "の",
       .annotation = "さ",
       .base = {16},
       .ruby = {20},
       .trail_start = 32},
  };
}

void check_overhang(const OverhangCase& test_case, bool vertical) {
  const std::string label =
      std::string(test_case.name) + " vertical=" + std::to_string(static_cast<int>(vertical));
  FakeMeasurer measurer;
  std::vector<Tree> children;
  if (!test_case.lead.empty()) {
    children.push_back(text(test_case.lead));
    if (test_case.break_after_lead) {
      children.push_back(br());
    }
  }
  children.push_back(ruby({text("桜"), rt(test_case.annotation)}));
  if (!test_case.trail.empty()) {
    children.push_back(text(test_case.trail));
  }
  std::vector<Tree> root_children{block(std::move(children))};
  const auto root =
      vertical ? build_vertical(std::move(root_children)) : build(std::move(root_children));
  const auto tree = vertical ? run_layout(root, vertical_options(400, 400), measurer)
                             : run_layout(root, make_options(400), measurer);
  ASSERT_TRUE(tree.has_value()) << label;
  const std::vector<const LineBox*> lines = all_lines(*tree);
  // 組のある行（改行を挟んだケースでは 2 行目）
  const LineBox& line = *lines[test_case.break_after_lead ? 1 : 0];

  std::vector<float> expected_base = test_case.base;
  if (!test_case.break_after_lead && !test_case.lead.empty()) {
    expected_base.insert(expected_base.begin(), 0);  // 前の文字は必ず 0 から
  }
  if (!test_case.trail.empty()) {
    expected_base.push_back(test_case.trail_start);
  }
  EXPECT_EQ(base_glyph_positions(line), expected_base) << label;
  EXPECT_EQ(ruby_glyph_positions(line), test_case.ruby) << label;
}

TEST(LayoutRuby, OverhangCombinations) {
  for (const OverhangCase& test_case : overhang_cases()) {
    for (const bool vertical : {false, true}) {
      check_overhang(test_case, vertical);
    }
  }
}

// issue #28 の再現: 前後の仮名に 4px ずつ掛かり、行の送りが 6 字ぶんに収まる。
TEST(LayoutRuby, OverhangPullsTheFollowingTextIn) {
  FakeMeasurer measurer;
  const auto root =
      build({block({text("あの"), ruby({text("桜"), rt("さくら")}), text("のえだ")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // あ 0・の 16・桜 32・の 48・え 64・だ 80（掛けが無ければ 桜 36・の 56 …）
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{0, 16, 32, 48, 64, 80}));
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{28, 36, 44}));
}

// 漢字等（cl-19）には掛けない。issue の「名桜木」の行。
TEST(LayoutRuby, DoesNotOverhangOntoKanji) {
  FakeMeasurer measurer;
  const auto root = build({block({text("名"), ruby({text("桜"), rt("さくら")}), text("木")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{0, 20, 40}));
}

// 親文字が複数クラスタの組: 掛けで余りが無くなれば親文字はベタに戻る（(a) の配分が消える）。
TEST(LayoutRuby, OverhangRemovesTheDistributionWhenTheExcessFits) {
  FakeMeasurer measurer;
  const auto root =
      build({block({text("の"), ruby({text("写植"), rt("しゃしょく")}), text("の")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // 前後 4px ずつ掛かり、親文字は自分の送り（32）にベタで収まる（配分は 0）
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{0, 16, 32, 48}));
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{12, 20, 28, 36, 44}));
}

// 行頭・行末の組は版面の外に出ない。
TEST(LayoutRuby, PairAtTheLineEdgesStaysInsideTheColumn) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({text("桜"), rt("さくら")}), text("あいうえお")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  // 組は [0, 24]: ルビは 0 から始まり、後ろの「あ」にだけ 8px 掛かる
  EXPECT_EQ(ruby_glyph_positions(line), (std::vector<float>{0, 8, 16}));
  EXPECT_EQ(base_glyph_positions(line), (std::vector<float>{0, 16, 32, 48, 64, 80}));
}

// 折り返しで組が行頭に来たら、前への掛けは落ちる（版面の外に出さない）。
// 落ちたぶんの余りは組の内部の配分（JLREQ 3.3.6）に戻る。
TEST(LayoutRuby, OverhangIsDroppedWhenThePairFallsToTheLineStart) {
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    FakeMeasurer measurer;
    // のののの（64）+ 組（24）+ のの: 幅 64 で組が 2 行目の先頭に落ちる
    std::vector<Tree> children{
        block({text("のののの"), ruby({text("桜"), rt("さくら")}), text("のの")})};
    const auto root = vertical ? build_vertical(std::move(children)) : build(std::move(children));
    const auto options = vertical ? vertical_options(400, 64) : make_options(64);
    const auto tree = run_layout(root, options, measurer);
    ASSERT_TRUE(tree.has_value());
    const std::vector<const LineBox*> lines = all_lines(*tree);
    ASSERT_EQ(lines.size(), 2U);
    // 2 行目: 前の掛け（4）は落ち、後ろの掛け（4）だけが効く。
    // 送りの範囲は [0, 20] なので、余り 4 が 1 クラスタの中央に戻って 桜 は 2
    EXPECT_EQ(ruby_glyph_positions(*lines[1]), (std::vector<float>{0, 8, 16}));
    EXPECT_EQ(base_glyph_positions(*lines[1]), (std::vector<float>{2, 20, 36}));
  }
}

// 掛けで行が縮むので、改行位置が変わりうる。
TEST(LayoutRuby, OverhangChangesWhereTheLineBreaks) {
  FakeMeasurer measurer;
  // の + 組（24）+ のの = 16 + 24 + 32 = 72。掛けで 8 縮んで 64 に収まる
  const auto make = [] {
    return build({block({text("の"), ruby({text("桜"), rt("さくら")}), text("のの")})});
  };
  const auto tree = run_layout(make(), 64, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(all_lines(*tree).size(), 1U);
  // 掛けられない相手（漢字）なら同じ幅で 2 行になる
  const auto kanji = build({block({text("木"), ruby({text("桜"), rt("さくら")}), text("木木")})});
  const auto kanji_tree = run_layout(kanji, 64, measurer);
  ASSERT_TRUE(kanji_tree.has_value());
  EXPECT_EQ(all_lines(*kanji_tree).size(), 2U);
}

// 組の箱（背景の矩形）は掛けを含まない送りの範囲で決める。
TEST(LayoutRuby, BackgroundIgnoresTheOverhang) {
  FakeMeasurer measurer;
  const auto filled = [](ComputedStyle& style) { style.background_color = Color{0, 255, 0, 255}; };
  const auto root = build(
      {block({text("の"), inline_box({ruby({text("桜"), rt("さくら")})}, filled), text("の")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const LineBox& line = *all_lines(*tree)[0];
  const std::vector<const InlineBackground*> painted = backgrounds(line);
  ASSERT_EQ(painted.size(), 1U);
  // ルビは [12, 36] にはみ出すが、背景は親文字の送りの範囲 [16, 32]
  EXPECT_FLOAT_EQ(painted[0]->rect.inline_start, 16);
  EXPECT_FLOAT_EQ(painted[0]->rect.inline_end(), 32);
}

// ---- flex / 固有寸法 ----------------------------------------------------------------

TEST(LayoutRuby, WorksInsideAFlexItem) {
  FakeMeasurer measurer;
  const auto root =
      build({flex({block({ruby({text("東"), rt("とうきょう")})})}, [](ComputedStyle& style) {
        style.flex_direction = style::FlexDirection::Column;
        style.align_items = style::AlignItems::FlexStart;
      })});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 1U);
  // 固有寸法は組の送り = max(16, 40)
  EXPECT_FLOAT_EQ(items[0].rect.inline_size, 40);
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"東とうきょう"}));
}

// <ruby> が flex コンテナの直接の子でも、テキストと同じ無名アイテムにまとまる。
TEST(LayoutRuby, RubyIsGroupedIntoTheAnonymousFlexItem) {
  FakeMeasurer measurer;
  const auto root = build({flex({text("あ"), ruby({text("漢"), rt("かん")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_TRUE(tree.has_value());
  const std::vector<BlockBox>& items = *tree->root.blocks()->front().blocks();
  ASSERT_EQ(items.size(), 1U);
  EXPECT_EQ(items[0].tag, "#anonymous");
  EXPECT_EQ(line_texts(*tree), (std::vector<std::string>{"あ漢かん"}));
}

// ---- エラーになる構造 ---------------------------------------------------------------

TEST(LayoutRuby, RtWithoutBaseIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({rt("かん"), text("漢")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("base text"), std::string::npos);
  EXPECT_TRUE(tree.error().location.has_value());
}

TEST(LayoutRuby, ElementInsideRtIsRejected) {
  FakeMeasurer measurer;
  Tree annotation = element("rt", Display::Inline, {inline_box({text("かん")})});
  const auto root = build({block({ruby({text("漢"), std::move(annotation)})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("<rt> may only contain text"), std::string::npos);
}

TEST(LayoutRuby, NestedRubyIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({ruby({text("漢"), rt("かん")}), rt("ふりがな")})})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("inside <ruby>"), std::string::npos);
}

TEST(LayoutRuby, ImageOrBreakInsideRubyIsRejected) {
  FakeMeasurer measurer;
  const ImageLookup images = image_table({{.src = "p", .id = 0, .width = 8, .height = 8}});
  const auto with_image = build({block({ruby({img("p"), rt("かん")})})});
  const auto image_tree = run_layout(with_image, 400, measurer, images);
  ASSERT_FALSE(image_tree.has_value());
  EXPECT_EQ(image_tree.error().kind, ErrorKind::UnsupportedLayout);
  const auto with_break = build({block({ruby({text("漢"), br(), rt("かん")})})});
  const auto break_tree = run_layout(with_break, 400, measurer);
  ASSERT_FALSE(break_tree.has_value());
  EXPECT_EQ(break_tree.error().kind, ErrorKind::UnsupportedLayout);
}

TEST(LayoutRuby, RtOutsideRubyIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({block({rt("かん")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("only allowed inside <ruby>"), std::string::npos);
}

// 親文字の中の色だけの span も、普通のテキストと同じでシェーピングを切らない（#8 / A27）。
// 断片は色の境界で分かれ、位置は 1 回のシェーピング結果のまま。
TEST(LayoutRuby, ColorOnlySpanInTheBaseDoesNotSplitShaping) {
  FakeMeasurer measurer;
  const auto root = build({block({ruby({
      text("東"),
      inline_box({text("京")}, [](ComputedStyle& style) { style.color = Color{255, 0, 0, 255}; }),
      rt("とうきょう"),
  })})});
  Counters counters;
  const auto tree = run_layout(root, make_options(400), measurer, counters);
  ASSERT_TRUE(tree.has_value());

  // 親文字で 1 回、ルビ文字で 1 回
  EXPECT_EQ(counters.shape_calls, 2U);
  const std::vector<const LineBox*> lines = all_lines(*tree);
  ASSERT_EQ(lines.size(), 1U);
  const std::vector<const TextFragment*> base = base_fragments(*lines[0]);
  ASSERT_EQ(base.size(), 2U);
  EXPECT_EQ(base[0]->color, kBlack);
  EXPECT_EQ(base[1]->color, (Color{255, 0, 0, 255}));
  // ルビの方が長い（8px × 5 = 40）ので、親文字 32px に余り 8 を配る（端 2 / 字間 4）
  constexpr float kExtra = (kRuby * 5) - (kBase * 2);
  EXPECT_FLOAT_EQ(base[0]->inline_start, kExtra / 4);
  EXPECT_FLOAT_EQ(base[1]->inline_start, (kExtra / 4) + kBase + (kExtra / 2));
}

// ルビはインラインの仕組みなので、ブロック級の箱にはできない。
TEST(LayoutRuby, BlockLevelRubyIsRejected) {
  FakeMeasurer measurer;
  const auto root = build({element("ruby", Display::Block, {text("漢")})});
  const auto tree = run_layout(root, 400, measurer);
  ASSERT_FALSE(tree.has_value());
  EXPECT_EQ(tree.error().kind, ErrorKind::UnsupportedLayout);
  EXPECT_NE(tree.error().message.find("block-level"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::layout::test
