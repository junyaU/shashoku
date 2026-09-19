#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "linebreak/break_class.hpp"
#include "linebreak/line_breaker.hpp"
#include "linebreak/test_support.hpp"

// 性質テスト（ARCHITECTURE.md §3.4 (6) / DESIGN.md §10-4）。
// 種を固定した乱数で日本語・約物・ASCII・空白・絵文字を混ぜた列を作り、
// ランダムな幅 × 全ポリシーで不変条件を検査する。乱数は入力の生成にだけ使い、
// 行分割器そのものは純粋関数のまま（同じ入力 → 同じ出力も検査する）。
namespace shashoku::linebreak {
namespace {

constexpr float kTolerance = 1.0F / 512.0F;

// Strictness を変えてもクラスが変わらない行頭禁則の文字だけを直接検査に使う。
// （！ ？ ・ 々 …… 小書き仮名は loose で ID に解決されるので、ここには入れない。
//   それらは「分割位置は break_opportunities() が true の位置」の検査で担保する。）
constexpr std::array<char32_t, 20> kAlwaysLineStartProhibited{
    U'。', U'、', U'，', U'．', U'」', U'』', U'）', U'〕', U'］', U'｝',
    U'》', U'】', U'〟', U')',  U',',  U'.',  U':',  U';',  U'/',  U'!',
};

// 行末禁則（始め括弧類）はどの Strictness でも OP のまま。
constexpr std::array<char32_t, 14> kAlwaysLineEndProhibited{
    U'「', U'『', U'（', U'〔', U'［', U'｛', U'〈', U'《', U'【', U'〖', U'〘', U'〝', U'(', U'[',
};

// 乱数で混ぜる文字。かな・漢字・約物・欧文・数字・空白・絵文字・結合文字。
constexpr auto kPool = std::to_array<char32_t>({
    U'あ',         U'い',     U'う',     U'か',     U'き',         U'ん',         U'漢',
    U'字',         U'語',     U'一',     U'っ',     U'ゃ',         U'ー',         U'々',
    U'ゝ',         U'。',     U'、',     U'「',     U'」',         U'『',         U'』',
    U'（',         U'）',     U'！',     U'？',     U'・',         U'：',         U'…',
    U'‥',          U'—',      U'〜',     U'‐',      U'A',          U'b',          U'Z',
    U'x',          U'0',      U'5',      U'9',      U' ',          U' ',          U'\n',
    U'-',          U',',      U'.',      U'(',      U')',          U'/',          U'%',
    U'$',          U'\u00A0', U'\u200B', U'\u0301', U'\U0001F600', U'\U0001F44D', U'\U0001F3FB',
    U'\U0001F1EF',
});

// 種を固定した乱数生成器。テストが毎回同じ入力列を作るために必要で、
// 暗号用途ではないので固定種の指摘（cert-msc32-c / cert-msc51-cpp）は抑制する。
std::mt19937 seeded_rng(std::uint32_t seed) {
  return std::mt19937(seed);  // NOLINT(cert-msc32-c,cert-msc51-cpp): テストの再現性のため
}

std::vector<Item> random_items(std::mt19937& rng, std::size_t count) {
  std::uniform_int_distribution<std::size_t> pick(0, kPool.size() - 1);
  std::uniform_int_distribution<int> rare(0, 39);
  std::string utf8;
  for (std::size_t i = 0; i < count; ++i) {
    utf8 += test::to_utf8(kPool[pick(rng)]);
  }
  std::vector<Item> items = test::items_of(utf8);
  for (Item& item : items) {
    if (rare(rng) == 0) {
      item.no_break_before = true;
    }
    if (rare(rng) == 1 && item.kind == ItemKind::Text) {
      item.kind = ItemKind::Atomic;
      item.advance = 32.0F;
    }
  }
  return items;
}

bool contains(std::span<const char32_t> set, char32_t cp) {
  return std::ranges::find(set, cp) != set.end();
}

std::string describe(std::span<const Item> items, const Breaks& breaks, std::size_t index) {
  std::string out;
  for (std::size_t i = 0; i < breaks.lines.size(); ++i) {
    out += (i == index ? " >>[" : " [");
    for (std::size_t j = breaks.lines[i].begin; j < breaks.lines[i].end; ++j) {
      out += items[j].kind == ItemKind::Text ? test::to_utf8(items[j].cp) : std::string("#");
    }
    out += breaks.lines[i].forced ? "]F" : "]";
  }
  return out;
}

void check_invariants(std::span<const Item> items, const Config& config, float width) {
  const LineBreaker breaker(config);
  const Breaks breaks = breaker.break_lines(items, width);
  const std::vector<bool> opportunities = breaker.break_opportunities(items);

  // 同じ入力には同じ出力（DESIGN.md §3-5 純粋関数）
  ASSERT_TRUE(breaks == breaker.break_lines(items, width));

  if (items.empty()) {
    EXPECT_TRUE(breaks.lines.empty());
    EXPECT_TRUE(breaks.spacing.empty());
    return;
  }
  ASSERT_FALSE(breaks.lines.empty());
  ASSERT_EQ(breaks.spacing.size(), items.size());

  // Spacing はアキを減らす方向にしか動かない
  for (const Spacing& spacing : breaks.spacing) {
    EXPECT_LE(spacing.before, 0.0F);
    EXPECT_LE(spacing.after, 0.0F);
  }

  std::size_t cursor = 0;
  for (std::size_t i = 0; i < breaks.lines.size(); ++i) {
    const Line& line = breaks.lines[i];
    SCOPED_TRACE("line " + std::to_string(i));
    // 全アイテムがちょうど 1 行に属する / 行は空でない
    ASSERT_EQ(line.begin, cursor);
    ASSERT_GT(line.end, line.begin);
    ASSERT_GE(line.content_end, line.begin);
    ASSERT_LE(line.content_end, line.end);
    ASSERT_LE(line.end, items.size());
    EXPECT_GE(line.width, 0.0F);
    EXPECT_GE(line.hang, 0.0F);
    cursor = line.end;

    // overflows でない行は幅に収まる
    if (!line.overflows) {
      EXPECT_LE(line.width, width + kTolerance);
    }
    // 分割位置は break_opportunities() が true の位置（break_anywhere を除く）
    if (i > 0 && !config.break_anywhere) {
      EXPECT_TRUE(opportunities[line.begin]);
    }
    if (config.break_anywhere || line.overflows || line.content_end == line.begin) {
      continue;
    }
    // 製品の存在理由: 行頭に句読点・終わり括弧が絶対に出ない。
    // ただし直前の行が強制改行で終わっていれば、その位置の改行は LB4 で必ず起きる
    // （非適合化できない規則なので禁則より強い）。
    if (i > 0 && !breaks.lines[i - 1].forced && items[line.begin].kind == ItemKind::Text) {
      EXPECT_FALSE(contains(kAlwaysLineStartProhibited, items[line.begin].cp))
          << "行頭禁則の文字が行頭に出た: " << describe(items, breaks, i);
    }
    // 行末に始め括弧が残らない（最終行と強制改行で終わる行は「次の行」がないので除く）
    if (i + 1 < breaks.lines.size() && !line.forced &&
        items[line.content_end - 1].kind == ItemKind::Text) {
      EXPECT_FALSE(contains(kAlwaysLineEndProhibited, items[line.content_end - 1].cp))
          << "行末禁則の文字が行末に出た: " << describe(items, breaks, i);
    }
  }
  EXPECT_EQ(cursor, items.size());
}

TEST(LineBreakProperty, InvariantsUnderRandomInput) {
  std::mt19937 rng = seeded_rng(20260919);  // 種は固定（テストは決定的）
  std::uniform_int_distribution<std::size_t> length(0, 40);
  std::uniform_real_distribution<float> width(1.0F, 220.0F);

  for (int iteration = 0; iteration < 400; ++iteration) {
    const std::vector<Item> items = random_items(rng, length(rng));
    const float available = width(rng);
    for (const Strictness strictness :
         {Strictness::Strict, Strictness::Normal, Strictness::Loose}) {
      for (const OverflowPolicy overflow :
           {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
        for (const bool anywhere : {false, true}) {
          Config config;
          config.strictness = strictness;
          config.overflow = overflow;
          config.break_anywhere = anywhere;
          SCOPED_TRACE("iteration " + std::to_string(iteration) + " strictness " +
                       std::to_string(static_cast<int>(strictness)) + " overflow " +
                       std::to_string(static_cast<int>(overflow)) + " anywhere " +
                       std::to_string(static_cast<int>(anywhere)) + " width " +
                       std::to_string(available));
          check_invariants(items, config, available);
        }
      }
    }
  }
}

TEST(LineBreakProperty, InvariantsWithUnboundedWidth) {
  std::mt19937 rng = seeded_rng(20260920);
  std::uniform_int_distribution<std::size_t> length(0, 60);
  for (int iteration = 0; iteration < 200; ++iteration) {
    const std::vector<Item> items = random_items(rng, length(rng));
    SCOPED_TRACE("iteration " + std::to_string(iteration));
    check_invariants(items, Config{}, kUnbounded);
  }
}

TEST(LineBreakProperty, MinContentWidthNeverOverflows) {
  // min_content_width は「この幅なら必ず収まる」下限であること。
  std::mt19937 rng = seeded_rng(20260921);
  std::uniform_int_distribution<std::size_t> length(1, 40);
  for (int iteration = 0; iteration < 200; ++iteration) {
    const std::vector<Item> items = random_items(rng, length(rng));
    const LineBreaker breaker;
    const float minimum = breaker.min_content_width(items);
    SCOPED_TRACE("iteration " + std::to_string(iteration) + " min " + std::to_string(minimum));
    EXPECT_GE(minimum, 0.0F);
    for (const Line& line : breaker.break_lines(items, minimum).lines) {
      EXPECT_FALSE(line.overflows);
      EXPECT_LE(line.width, minimum + kTolerance);
    }
  }
}

TEST(LineBreakProperty, ExtraProhibitedCharactersNeverStartOrEndLines) {
  // Config::extra_* で足した文字も禁則として守られる。
  std::mt19937 rng = seeded_rng(20260922);
  std::uniform_int_distribution<std::size_t> length(1, 40);
  std::uniform_real_distribution<float> width(8.0F, 160.0F);
  Config config;
  config.extra_line_start_prohibited = U"ん";
  config.extra_line_end_prohibited = U"か";
  const LineBreaker breaker(config);
  for (int iteration = 0; iteration < 200; ++iteration) {
    const std::vector<Item> items = random_items(rng, length(rng));
    const Breaks breaks = breaker.break_lines(items, width(rng));
    SCOPED_TRACE("iteration " + std::to_string(iteration));
    for (std::size_t i = 0; i < breaks.lines.size(); ++i) {
      const Line& line = breaks.lines[i];
      if (i > 0 && !breaks.lines[i - 1].forced && !line.overflows &&
          items[line.begin].kind == ItemKind::Text) {
        EXPECT_NE(items[line.begin].cp, U'ん') << describe(items, breaks, i);
      }
      if (i + 1 < breaks.lines.size() && !line.forced && line.content_end > line.begin &&
          items[line.content_end - 1].kind == ItemKind::Text) {
        EXPECT_NE(items[line.content_end - 1].cp, U'か') << describe(items, breaks, i);
      }
    }
  }
}

}  // namespace
}  // namespace shashoku::linebreak
