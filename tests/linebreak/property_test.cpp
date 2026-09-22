#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

// 位置 i（items[i-1] と items[i] の間）で overflow-wrap の緊急分割が起こりうるか。
// 両側のアイテムがともに Normal 以外のときだけ（ARCHITECTURE.md A23）。
bool anywhere_between(std::span<const Item> items, const Config& config, std::size_t i) {
  if (i == 0 || i >= items.size()) {
    return false;
  }
  return items[i - 1].wrap.value_or(config.wrap) != Wrap::Normal &&
         items[i].wrap.value_or(config.wrap) != Wrap::Normal;
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
    // 分割位置は break_opportunities() が true の位置（緊急分割の発動を除く）
    const bool anywhere_here = anywhere_between(items, config, line.begin);
    if (i > 0 && !anywhere_here) {
      EXPECT_TRUE(opportunities[line.begin]);
    }
    if (line.overflows || line.content_end == line.begin) {
      continue;
    }
    // 製品の存在理由: 行頭に句読点・終わり括弧が絶対に出ない。
    // ただし直前の行が強制改行で終わっていれば、その位置の改行は LB4 で必ず起きる
    // （非適合化できない規則なので禁則より強い）。
    if (i > 0 && !anywhere_here && !breaks.lines[i - 1].forced &&
        items[line.begin].kind == ItemKind::Text) {
      EXPECT_FALSE(contains(kAlwaysLineStartProhibited, items[line.begin].cp))
          << "行頭禁則の文字が行頭に出た: " << describe(items, breaks, i);
    }
    // 行末に始め括弧が残らない（最終行と強制改行で終わる行は「次の行」がないので除く）
    if (i + 1 < breaks.lines.size() && !line.forced && !anywhere_between(items, config, line.end) &&
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
        for (const Wrap wrap : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
          Config config;
          config.strictness = strictness;
          config.overflow = overflow;
          config.wrap = wrap;
          SCOPED_TRACE("iteration " + std::to_string(iteration) + " strictness " +
                       std::to_string(static_cast<int>(strictness)) + " overflow " +
                       std::to_string(static_cast<int>(overflow)) + " wrap " +
                       std::to_string(static_cast<int>(wrap)) + " width " +
                       std::to_string(available));
          check_invariants(items, config, available);
        }
      }
    }
  }
}

// アイテムごとのポリシー（ARCHITECTURE.md A23）を「span 相当の連続した範囲」に振る。
// 範囲の切り方も値もランダムなので、入れ子・隣接・Config との食い違いが一通り出る。
void sprinkle_policies(std::mt19937& rng, std::vector<Item>& items) {
  std::uniform_int_distribution<std::size_t> run(1, 6);
  std::uniform_int_distribution<int> choice(0, 4);
  std::size_t i = 0;
  while (i < items.size()) {
    const std::size_t end = std::min(items.size(), i + run(rng));
    std::optional<Strictness> strictness;
    switch (choice(rng)) {
      case 1:
        strictness = Strictness::Strict;
        break;
      case 2:
        strictness = Strictness::Normal;
        break;
      case 3:
        strictness = Strictness::Loose;
        break;
      default:
        break;  // nullopt: Config に従う
    }
    std::optional<Wrap> wrap;
    switch (choice(rng)) {
      case 1:
        wrap = Wrap::Anywhere;
        break;
      case 2:
        wrap = Wrap::Normal;
        break;
      case 3:
        wrap = Wrap::BreakWord;
        break;
      default:
        break;  // nullopt: Config に従う
    }
    for (std::size_t j = i; j < end; ++j) {
      items[j].strictness = strictness;
      items[j].wrap = wrap;
    }
    i = end;
  }
}

// ルビの掛け（JLREQ 3.3.8。Item::overhang_*）をランダムに振る。契約の範囲（非負・送り以内）を
// 守る値だけでなく、**契約を破る値**（負・送りより大きい）も混ぜる: 行分割器はそれを丸めて
// 不変条件を保つ（line_breaker.hpp の Item の説明）。
void sprinkle_overhang(std::mt19937& rng, std::vector<Item>& items) {
  std::uniform_int_distribution<int> pick(0, 5);
  std::uniform_real_distribution<float> amount(-8.0F, 40.0F);
  for (Item& item : items) {
    if (pick(rng) == 0) {
      item.overhang_before = amount(rng);
    }
    if (pick(rng) == 0) {
      item.overhang_after = amount(rng);
    }
  }
}

TEST(LineBreakProperty, InvariantsWithPerItemPolicies) {
  std::mt19937 rng = seeded_rng(20260925);
  std::uniform_int_distribution<std::size_t> length(0, 40);
  std::uniform_real_distribution<float> width(1.0F, 220.0F);

  for (int iteration = 0; iteration < 200; ++iteration) {
    std::vector<Item> items = random_items(rng, length(rng));
    sprinkle_policies(rng, items);
    const float available = width(rng);
    for (const Strictness strictness :
         {Strictness::Strict, Strictness::Normal, Strictness::Loose}) {
      for (const OverflowPolicy overflow :
           {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
        for (const Wrap wrap : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
          Config config;
          config.strictness = strictness;
          config.overflow = overflow;
          config.wrap = wrap;
          SCOPED_TRACE("iteration " + std::to_string(iteration) + " strictness " +
                       std::to_string(static_cast<int>(strictness)) + " overflow " +
                       std::to_string(static_cast<int>(overflow)) + " wrap " +
                       std::to_string(static_cast<int>(wrap)) + " width " +
                       std::to_string(available));
          check_invariants(items, config, available);
        }
      }
    }
  }
}

// ルビの掛けを混ぜても不変条件が保たれる（#28(b)）。掛けは行の幅を**縮める**ので、
// 「行はこの幅に収まる」「行頭に句読点が出ない」などが崩れやすいところ。
TEST(LineBreakProperty, InvariantsWithOverhang) {
  std::mt19937 rng = seeded_rng(20260922);
  std::uniform_int_distribution<std::size_t> length(0, 40);
  std::uniform_real_distribution<float> width(1.0F, 220.0F);

  for (int iteration = 0; iteration < 200; ++iteration) {
    std::vector<Item> items = random_items(rng, length(rng));
    sprinkle_overhang(rng, items);
    if (iteration % 2 == 1) {
      sprinkle_policies(rng, items);
    }
    const float available = width(rng);
    for (const OverflowPolicy overflow :
         {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
      for (const Wrap wrap : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
        Config config;
        config.overflow = overflow;
        config.wrap = wrap;
        SCOPED_TRACE("iteration " + std::to_string(iteration) + " overflow " +
                     std::to_string(static_cast<int>(overflow)) + " wrap " +
                     std::to_string(static_cast<int>(wrap)) + " width " +
                     std::to_string(available));
        check_invariants(items, config, available);
      }
    }
    check_invariants(items, Config{}, kUnbounded);
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
  // overflow-wrap: anywhere は min-content の区間を細かく切る（A35）ので、
  // 「切った位置で本当に割れる」ことがここで担保される（緊急分割の最後の逃げ場）。
  // アイテムごとに anywhere / break-word / normal を混ぜた入力でも成り立つこと。
  std::mt19937 rng = seeded_rng(20260921);
  std::uniform_int_distribution<std::size_t> length(1, 40);
  for (int iteration = 0; iteration < 200; ++iteration) {
    std::vector<Item> items = random_items(rng, length(rng));
    if (iteration % 2 == 1) {
      sprinkle_policies(rng, items);  // span 相当の範囲にランダムなポリシーを振る
    }
    if (iteration % 3 == 0) {
      sprinkle_overhang(rng, items);  // ルビの掛け（#28(b)）を混ぜても成り立つこと
    }
    for (const Wrap wrap : {Wrap::Normal, Wrap::BreakWord, Wrap::Anywhere}) {
      Config config;
      config.wrap = wrap;
      const LineBreaker breaker(config);
      const float minimum = breaker.min_content_width(items);
      SCOPED_TRACE("iteration " + std::to_string(iteration) + " wrap " +
                   std::to_string(static_cast<int>(wrap)) + " min " + std::to_string(minimum));
      EXPECT_GE(minimum, 0.0F);
      for (const Line& line : breaker.break_lines(items, minimum).lines) {
        EXPECT_FALSE(line.overflows);
        EXPECT_LE(line.width, minimum + kTolerance);
      }
    }
  }
}

// 分離禁止ペア（…… / ——）を「普通の文字で挟んだ孤立したペア」として埋め込んだランダム文。
struct PairedText {
  std::vector<Item> items;
  std::vector<std::uint8_t> pair_second;  // 分離禁止ペアの 2 文字目のアイテム
};

PairedText random_paired_text(std::mt19937& rng, std::size_t tokens) {
  // ペアの前が普通に割れる文字（「 以外）だと緊急分割まで来ないこともあるので、
  // 始め括弧・句点も混ぜて位置選びの各段を踏ませる。
  static constexpr std::array<char32_t, 8> kPlain{U'あ', U'い', U'漢', U'字',
                                                  U'。', U'「', U'」', U'ん'};
  std::uniform_int_distribution<std::size_t> plain(0, kPlain.size() - 1);
  std::uniform_int_distribution<int> shape(0, 3);
  std::u32string text;
  std::vector<std::size_t> seconds;
  for (std::size_t i = 0; i < tokens; ++i) {
    if (shape(rng) == 0) {
      const char32_t mark = shape(rng) < 2 ? U'…' : U'—';
      text.push_back(mark);
      seconds.push_back(text.size());
      text.push_back(mark);
    }
    text.push_back(kPlain[plain(rng)]);  // ペアは必ず普通の文字で挟む
  }
  std::string utf8;
  for (const char32_t cp : text) {
    utf8 += test::to_utf8(cp);
  }
  PairedText result;
  result.items = test::items_of(utf8);
  result.pair_second.assign(result.items.size(), 0);
  for (const std::size_t index : seconds) {
    result.pair_second[index] = 1;
  }
  return result;
}

TEST(LineBreakProperty, BreakAnywhereKeepsInseparablePairsThatFit) {
  // 緊急分割でも、ペアが 1 行に収まる幅なら途中では割らない。
  // 出典: JIS X 4051 / JLREQ 3.1.1 分離禁則。ペアが収まらない幅で割れるのは想定内。
  std::mt19937 rng = seeded_rng(20260923);
  std::uniform_int_distribution<std::size_t> tokens(1, 20);
  std::uniform_real_distribution<float> width(4.0F, 120.0F);
  for (int iteration = 0; iteration < 300; ++iteration) {
    const PairedText text = random_paired_text(rng, tokens(rng));
    const float available = width(rng);
    // loose では IN が ID に解決されて分離禁則が外れるので strict / normal を見る。
    for (const Strictness strictness : {Strictness::Strict, Strictness::Normal}) {
      for (const OverflowPolicy overflow :
           {OverflowPolicy::Oidashi, OverflowPolicy::Oikomi, OverflowPolicy::Burasage}) {
        Config config;
        config.strictness = strictness;
        config.overflow = overflow;
        config.wrap = Wrap::Anywhere;
        const Breaks breaks = LineBreaker(config).break_lines(text.items, available);
        SCOPED_TRACE("iteration " + std::to_string(iteration) + " width " +
                     std::to_string(available) + " strictness " +
                     std::to_string(static_cast<int>(strictness)));
        for (std::size_t i = 1; i < breaks.lines.size(); ++i) {
          const std::size_t begin = breaks.lines[i].begin;
          if (text.pair_second[begin] == 0) {
            continue;
          }
          const float pair = text.items[begin - 1].advance + text.items[begin].advance;
          EXPECT_GT(pair, available + kTolerance)
              << "1 行に収まるはずの分離禁止ペアを割った: " << describe(text.items, breaks, i);
        }
      }
    }
  }
}

// 分離禁則に掛かる要素（数字・…・—・結合文字）を含まない文字だけの列。
// この列では splits_inseparable() が決して真にならないので、緊急分割の位置選びは
// 「禁則に掛かるか」だけで決まり、テスト側から厳密に予測できる。
std::vector<Item> random_separable_text(std::mt19937& rng, std::size_t count) {
  static constexpr std::array<char32_t, 18> kSeparablePool{
      U'あ', U'い', U'漢', U'字', U'A',  U'b',  U'Z', U'x', U' ',
      U'。', U'、', U'」', U'）', U'「', U'（', U',', U'!', U'/',
  };
  std::uniform_int_distribution<std::size_t> pick(0, kSeparablePool.size() - 1);
  std::string utf8;
  for (std::size_t i = 0; i < count; ++i) {
    utf8 += test::to_utf8(kSeparablePool[pick(rng)]);
  }
  return test::items_of(utf8);
}

TEST(LineBreakProperty, BreakAnywhereBreaksLineStartRuleOnlyAsLastResort) {
  // 緊急分割で行頭禁則を破った行があるなら、直前の行の中に
  // 「行頭禁則にも行末禁則にも掛からず、幅にも収まるクラスタ境界」は 1 つも無かったはず。
  std::mt19937 rng = seeded_rng(20260924);
  std::uniform_int_distribution<std::size_t> length(1, 30);
  std::uniform_real_distribution<float> width(4.0F, 90.0F);
  for (int iteration = 0; iteration < 300; ++iteration) {
    const std::vector<Item> items = random_separable_text(rng, length(rng));
    const float available = width(rng);
    for (const Strictness strictness : {Strictness::Strict, Strictness::Normal}) {
      Config config;
      config.strictness = strictness;
      config.wrap = Wrap::Anywhere;
      const LineBreaker breaker(config);
      const Breaks breaks = breaker.break_lines(items, available);
      const std::vector<bool> opportunities = breaker.break_opportunities(items);
      SCOPED_TRACE("iteration " + std::to_string(iteration) + " width " +
                   std::to_string(available));
      for (std::size_t i = 1; i < breaks.lines.size(); ++i) {
        const std::size_t begin = breaks.lines[i].begin;
        if (opportunities[begin] || breaks.lines[i - 1].overflows) {
          continue;  // 通常の分割位置 / 1 クラスタも収まらなかった行
        }
        if (!contains(kAlwaysLineStartProhibited, items[begin].cp)) {
          continue;
        }
        // 直前の行を短くすれば禁則を守れた、という位置があってはならない。
        for (std::size_t p = breaks.lines[i - 1].begin + 1; p < begin; ++p) {
          const bool prohibited = contains(kAlwaysLineStartProhibited, items[p].cp) ||
                                  contains(kAlwaysLineEndProhibited, items[p - 1].cp);
          EXPECT_TRUE(prohibited) << "禁則を守れる位置があったのに破った: "
                                  << describe(items, breaks, i);
        }
      }
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
