#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// ルビ組の JLREQ 3.3.6 の配分（issue #28 / ARCHITECTURE.md §3.8）。
//
// 期待値は issue #28 の再現表（本物の Noto Sans JP、16px / <rt> 8px）。
// tests/layout/ruby_test.cpp が偽の TextMeasurer で規則を固定するのに対し、ここは
// **実フォントを通した box ダンプの数値そのもの**を固定する。あわせて「配分は組の内部だけを
// 動かす」ことを、行の矩形と組の直後の文字の位置（= 行分割器に渡した送り）で押さえる。
namespace shashoku::test {
namespace {

std::string compact_json(std::string_view json) {
  std::string out;
  out.reserve(json.size());
  for (const char c : json) {
    if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
      out.push_back(c);
    }
  }
  return out;
}

std::string box_dump(std::string_view html, const RenderOptions& options) {
  const auto json = dump(html, japanese_fonts(), ImageSet{}, options, DumpStage::Box);
  if (!json) {
    ADD_FAILURE() << "dump: " << to_string(json.error());
    return {};
  }
  return compact_json(*json);
}

std::string box_dump(std::string_view html) { return box_dump(html, options_for(400)); }

// text が一致する最初の TextFragment の位置（ダンプの文字列のまま比べる。丸めを持ち込まない）。
std::size_t fragment_at(const std::string& json, std::string_view text) {
  const std::string key = R"("text":")" + std::string(text) + R"(")";
  const std::size_t at = json.find(key);
  if (at == std::string::npos) {
    ADD_FAILURE() << "断片が見つかりません: " << text;
  }
  return at;
}

// その断片のグリフのペン位置（`[glyph_id, ペン位置, x_offset, y_offset]` の 2 番目）。
std::vector<std::string> glyph_positions_of(const std::string& json, std::string_view text) {
  std::vector<std::string> out;
  const std::size_t fragment = fragment_at(json, text);
  if (fragment == std::string::npos) {
    return out;
  }
  constexpr std::string_view kGlyphs = R"("glyphs":[)";
  const std::size_t begin = json.find(kGlyphs, fragment);
  const std::size_t end = json.find("]]", begin);
  if (begin == std::string::npos || end == std::string::npos) {
    ADD_FAILURE() << "glyphs が見つかりません: " << text;
    return out;
  }
  for (std::size_t at = json.find('[', begin + kGlyphs.size()); at != std::string::npos && at < end;
       at = json.find('[', at + 1)) {
    const std::size_t first = json.find(',', at);
    const std::size_t second = json.find(',', first + 1);
    out.push_back(json.substr(first + 1, second - first - 1));
  }
  return out;
}

// その断片の数値フィールド（inline_start など）。
std::string field_of(const std::string& json, std::string_view text, std::string_view field) {
  const std::size_t fragment = fragment_at(json, text);
  if (fragment == std::string::npos) {
    return {};
  }
  const std::string key = "\"" + std::string(field) + "\":";
  const std::size_t begin = json.find(key, fragment);
  if (begin == std::string::npos) {
    ADD_FAILURE() << "フィールドが見つかりません: " << field;
    return {};
  }
  const std::size_t value = begin + key.size();
  const std::size_t end = json.find_first_of(",}", value);
  return json.substr(value, end - value);
}

// 行ボックスの矩形を文書順に（"rect" の直後のキーが "baseline" なら行ボックス）。
std::vector<std::string> line_rects(const std::string& json) {
  constexpr std::string_view kKey = R"("rect":[)";
  constexpr std::string_view kAfter = R"(,"baseline":)";
  std::vector<std::string> out;
  for (std::size_t at = json.find(kKey); at != std::string::npos; at = json.find(kKey, at + 1)) {
    const std::size_t begin = at + kKey.size();
    const std::size_t end = json.find(']', begin);
    if (end == std::string::npos) {
      break;
    }
    if (json.compare(end + 1, kAfter.size(), kAfter) != 0) {
      continue;  // 行ボックスではない矩形
    }
    out.push_back(json.substr(begin, end - begin));
  }
  return out;
}

// ---- issue #28 の再現表 ---------------------------------------------------------------

// ルビの方が長い組: 親文字を端 2 / 字間 4 で配る（修正前は 4・20 のベタ中央置き）。
TEST(IntegrationRubyDistribution, LongerRubyDistributesTheBase) {
  const std::string json =
      box_dump(R"(<div style="font-size: 16px"><ruby>写植<rt>しゃしょく</rt></ruby>です。</div>)");
  EXPECT_EQ(glyph_positions_of(json, "写植"), (std::vector<std::string>{"2", "22"}));
  // ルビ側はベタのまま
  EXPECT_EQ(glyph_positions_of(json, "しゃしょく"),
            (std::vector<std::string>{"0", "8", "16", "24", "32"}));
  // 組の送りは max(32, 40) = 40 のまま（続く「です。」の位置 = 行分割器に渡した送り）
  EXPECT_EQ(field_of(json, "です。", "inline_start"), "40");
  // 行の矩形も変わらない（配分は組の内部だけを動かす）
  EXPECT_EQ(line_rects(json), (std::vector<std::string>{"0,0,400,34.75"}));
}

// 親文字の方が長い組: ルビを端 4 / 字間 8 で配る（修正前は 12・20・28）。
TEST(IntegrationRubyDistribution, ShorterRubyIsDistributed) {
  const std::string json =
      box_dump(R"(<div style="font-size: 16px"><ruby>図書館<rt>としょ</rt></ruby>へゆく</div>)");
  EXPECT_EQ(glyph_positions_of(json, "図書館"), (std::vector<std::string>{"0", "16", "32"}));
  EXPECT_EQ(glyph_positions_of(json, "としょ"), (std::vector<std::string>{"4", "20", "36"}));
  EXPECT_EQ(field_of(json, "へゆく", "inline_start"), "48");
  EXPECT_EQ(line_rects(json), (std::vector<std::string>{"0,0,400,34.75"}));
}

// 端の空きはルビ文字サイズの全角（8px）が上限。止めたぶんは字間に回る。
TEST(IntegrationRubyDistribution, EdgeSpaceIsCappedAtOneRubyEm) {
  const std::string json =
      box_dump(R"(<div style="font-size: 16px"><ruby>図書館員<rt>とし</rt></ruby>へゆく</div>)");
  // 端は 48/4 = 12 ではなく 8 で止まり、字間が 48 − 8 × 2 = 32 になる
  EXPECT_EQ(glyph_positions_of(json, "とし"), (std::vector<std::string>{"8", "48"}));
  EXPECT_EQ(field_of(json, "へゆく", "inline_start"), "64");
}

// 縦書きでも同じ配分（inline 座標が横書きと一致する）。
TEST(IntegrationRubyDistribution, VerticalUsesTheSameDistribution) {
  RenderOptions options = options_for(400);
  options.viewport_height = 400;
  const std::string json = box_dump(
      R"(<div style="writing-mode: vertical-rl; font-size: 16px"><ruby>写植<rt>しゃしょく</rt></ruby>です。</div>)",
      options);
  EXPECT_EQ(glyph_positions_of(json, "写植"), (std::vector<std::string>{"2", "22"}));
  EXPECT_EQ(glyph_positions_of(json, "しゃしょく"),
            (std::vector<std::string>{"0", "8", "16", "24", "32"}));
  EXPECT_EQ(field_of(json, "です。", "inline_start"), "40");
}

// 配分する余りが無い / 配る先が無い組は数値が変わらない（修正前と同じ）。
TEST(IntegrationRubyDistribution, CasesThatMustNotChange) {
  // 送りが等しい組（親文字 2 × 16 = ルビ 4 × 8）は両方ベタ
  const std::string equal =
      box_dump(R"(<div style="font-size: 16px"><ruby>東京<rt>とうきょ</rt></ruby>へ</div>)");
  EXPECT_EQ(glyph_positions_of(equal, "東京"), (std::vector<std::string>{"0", "16"}));
  EXPECT_EQ(glyph_positions_of(equal, "とうきょ"),
            (std::vector<std::string>{"0", "8", "16", "24"}));
  EXPECT_EQ(field_of(equal, "へ", "inline_start"), "32");

  // 親文字 1 文字は中央のまま（配る字間が無い）
  const std::string single =
      box_dump(R"(<div style="font-size: 16px"><ruby>桜<rt>さくら</rt></ruby>へ</div>)");
  EXPECT_EQ(glyph_positions_of(single, "桜"), (std::vector<std::string>{"4"}));
  EXPECT_EQ(field_of(single, "へ", "inline_start"), "24");

  // ルビ 1 文字も中央のまま（端 12 は上限 8 を超えるが、中央置きには上限を掛けない）
  const std::string one_ruby =
      box_dump(R"(<div style="font-size: 16px"><ruby>東京<rt>と</rt></ruby>へ</div>)");
  EXPECT_EQ(glyph_positions_of(one_ruby, "と"), (std::vector<std::string>{"12"}));
  EXPECT_EQ(field_of(one_ruby, "へ", "inline_start"), "32");
}

}  // namespace
}  // namespace shashoku::test
