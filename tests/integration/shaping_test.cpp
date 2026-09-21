#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// シェーピング境界と装飾境界の分離（issue #8 / ARCHITECTURE.md A27）の性質テスト。
//
// **色だけを変える <span> を任意の位置に入れても、全グリフの glyph_id と位置が変わらない。**
// 偽の TextMeasurer（tests/layout/）はカーニングも合字も持たないのでこの性質を検査できない。
// 本物の Shaper（HarfBuzz + Noto）を通して、display-list のグリフを数値で比べる。
namespace shashoku::test {
namespace {

std::string strip_spaces(std::string_view json) {
  std::string out;
  out.reserve(json.size());
  for (const char c : json) {
    if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
      out.push_back(c);
    }
  }
  return out;
}

// display-list のダンプから、全 DrawGlyphs のグリフを描画順に "[glyph_id,x,y]" の列として取る。
// **どの DrawGlyphs に属するか（= 色による断片の分かれ方）は無視する**: 見たいのは
// 「グリフとその位置」であって、色ごとの描き分けはこの性質とは独立だから。
// 数値は文字列のまま比べる（比較のために丸めを持ち込まない）。
std::vector<std::string> glyph_tuples(std::string_view json) {
  const std::string compact = strip_spaces(json);
  constexpr std::string_view kKey = R"("glyphs":[)";
  std::vector<std::string> out;
  for (std::size_t at = compact.find(kKey); at != std::string::npos;
       at = compact.find(kKey, at + 1)) {
    std::size_t i = at + kKey.size();
    while (i < compact.size() && compact[i] == '[') {
      const std::size_t end = compact.find(']', i);
      if (end == std::string::npos) {
        return out;
      }
      out.push_back(compact.substr(i, end - i + 1));
      i = end + 1;
      if (i < compact.size() && compact[i] == ',') {
        ++i;
      }
    }
  }
  return out;
}

std::vector<std::string> glyphs_of(std::string_view html, const FontSet& fonts) {
  const RenderOptions options = options_for(3000);  // 折り返さない幅（行分割の違いを混ぜない）
  const auto json = dump(html, fonts, ImageSet{}, options, DumpStage::DisplayList);
  EXPECT_TRUE(json.has_value()) << to_string(json.error());
  if (!json) {
    return {};
  }
  std::vector<std::string> glyphs = glyph_tuples(*json);
  EXPECT_FALSE(glyphs.empty()) << *json;
  return glyphs;
}

// 色つきの draw_glyphs の数（色ごとの描き分けが残っていることの確認用）。
std::size_t command_count(std::string_view html, const FontSet& fonts) {
  const RenderOptions options = options_for(3000);
  const auto json = dump(html, fonts, ImageSet{}, options, DumpStage::DisplayList);
  EXPECT_TRUE(json.has_value()) << to_string(json.error());
  if (!json) {
    return 0;
  }
  const std::string compact = strip_spaces(*json);
  constexpr std::string_view kMarker = R"("op":"draw_glyphs")";
  std::size_t count = 0;
  for (std::size_t at = compact.find(kMarker); at != std::string::npos;
       at = compact.find(kMarker, at + 1)) {
    ++count;
  }
  return count;
}

// issue #8 の再現例。色だけを変えた B のグリフ位置が A と一致すること。
// 壊れていたときは「o」が 9〜11px（font-size の 1 割）右にずれていた。
constexpr std::string_view kPlain = R"(<div style="font-size:100px">AVTo</div>)";
constexpr std::string_view kColored =
    R"(<div style="font-size:100px">A<span style="color:red">V</span>T)"
    R"(<span style="color:red">o</span></div>)";

TEST(ShapingBoundaries, ColorSpansKeepKerningJapaneseFont) {
  const FontSet fonts = japanese_fonts();
  EXPECT_EQ(glyphs_of(kColored, fonts), glyphs_of(kPlain, fonts));
}

TEST(ShapingBoundaries, ColorSpansKeepKerningLatinFont) {
  FontSet fonts;
  fonts.add(text::assets::noto_sans());
  EXPECT_EQ(glyphs_of(kColored, fonts), glyphs_of(kPlain, fonts));
}

// 色ごとの描き分けは残る（位置だけを共有し、DrawGlyphs は色の境界で分かれる）。
TEST(ShapingBoundaries, ColorStillSplitsDrawCommands) {
  const FontSet fonts = japanese_fonts();
  EXPECT_EQ(command_count(kPlain, fonts), 1U);
  EXPECT_EQ(command_count(kColored, fonts), 4U);
}

// ---------------------------------------------------------------------------
// 種を固定したランダム挿入
// ---------------------------------------------------------------------------

// 再現性のための線形合同法（std::random の実装差を持ち込まない）。
class Lcg {
 public:
  explicit Lcg(std::uint64_t seed) : state_(seed) {}
  std::uint64_t next() {
    state_ = (state_ * 6364136223846793005ULL) + 1442695040888963407ULL;
    return state_ >> 33U;
  }

 private:
  std::uint64_t state_;
};

// UTF-8 の文字境界（先行バイト）の位置を全部返す。
std::vector<std::size_t> char_starts(std::string_view text) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U) {
      out.push_back(i);
    }
  }
  out.push_back(text.size());
  return out;
}

// [begin, end) の文字を色つき span で包んだ HTML を作る（本文は 1 文字も変えない）。
std::string wrap_spans(std::string_view text,
                       const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
  std::string body;
  std::size_t at = 0;
  for (const auto& [begin, end] : ranges) {
    body += text.substr(at, begin - at);
    body += R"(<span style="color:#c03000">)";
    body += text.substr(begin, end - begin);
    body += "</span>";
    at = end;
  }
  body += text.substr(at);
  return R"(<div style="font-size:48px">)" + body + "</div>";
}

// 欧文のカーニング対（AV / To / Wa / LT）と和文を混ぜた文。
constexpr std::string_view kSample = "AVATAR To Wander LTD. わたしの写植 WAVE Toyota Ya.";

TEST(ShapingBoundaries, RandomColorSpansNeverMoveGlyphs) {
  const FontSet fonts = latin_then_japanese();
  const std::vector<std::string> expected = glyphs_of(wrap_spans(kSample, {}), fonts);
  const std::vector<std::size_t> starts = char_starts(kSample);
  ASSERT_GT(starts.size(), 4U);

  Lcg random(20260920);
  for (int trial = 0; trial < 40; ++trial) {
    // 重ならない範囲を文字境界の上で選ぶ（span は入れ子にしない）
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t index = 0;
    while (index + 1 < starts.size()) {
      index += 1 + static_cast<std::size_t>(random.next() % 3U);  // 0〜2 文字あける
      if (index + 1 >= starts.size()) {
        break;
      }
      const std::size_t length = 1 + static_cast<std::size_t>(random.next() % 3U);
      const std::size_t last = std::min(index + length, starts.size() - 1);
      ranges.emplace_back(starts[index], starts[last]);
      index = last;
    }
    if (ranges.empty()) {
      continue;
    }
    const std::string html = wrap_spans(kSample, ranges);
    EXPECT_EQ(glyphs_of(html, fonts), expected) << "trial " << trial << "\n" << html;
  }
}

}  // namespace
}  // namespace shashoku::test
