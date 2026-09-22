#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// ルビの掛け（JLREQ 3.3.8 / ARCHITECTURE.md §3.8 の規則 6。issue #28 の (b)）。
//
// 期待値は issue #28 の再現表（本物の Noto Sans JP、16px / <rt> 8px）。ルビが親文字より
// 長い組は、はみ出した量を前後の**仮名**に掛けてよい（漢字等には掛けない）。掛けたぶんだけ
// 組の送りが縮むので、続く文字が手前に寄り、改行位置も変わりうる。
// tests/layout/ruby_test.cpp が偽の TextMeasurer で規則を固定するのに対し、ここは
// **実フォントを通した box ダンプの数値そのもの**を固定する。
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

// ---- issue #28 の (b) の受け入れ条件 ----------------------------------------------------

// 前後の仮名に 4px ずつ掛かり、行の送りが 6 字ぶん = 96px に収まる。
TEST(IntegrationRubyOverhang, HangsOntoAdjacentKana) {
  const std::string json =
      box_dump(R"(<div style="font-size: 16px">あの<ruby>桜<rt>さくら</rt></ruby>のえだ</div>)");
  // 掛けが無ければ 桜 36・「のえだ」56。掛けで 4px ずつ前後に寄る
  EXPECT_EQ(glyph_positions_of(json, "桜"), (std::vector<std::string>{"32"}));
  EXPECT_EQ(field_of(json, "のえだ", "inline_start"), "48");
  // ルビは組の箱いっぱい（前後の「の」に 4px ずつはみ出す）
  EXPECT_EQ(glyph_positions_of(json, "さくら"), (std::vector<std::string>{"28", "36", "44"}));
}

// 漢字等（cl-19）には掛けない（Chrome は掛けるが JLREQ 違反。chrome_compare.md §4-2）。
TEST(IntegrationRubyOverhang, DoesNotHangOntoKanji) {
  const std::string json =
      box_dump(R"(<div style="font-size: 16px">名<ruby>桜<rt>さくら</rt></ruby>木</div>)");
  EXPECT_EQ(glyph_positions_of(json, "名"), (std::vector<std::string>{"0"}));
  EXPECT_EQ(glyph_positions_of(json, "桜"), (std::vector<std::string>{"20"}));
  EXPECT_EQ(field_of(json, "木", "inline_start"), "40");
}

// 行頭・行末の組は版面の外に出ない。
TEST(IntegrationRubyOverhang, PairAtTheLineEdgeStaysInsideTheColumn) {
  const std::string json =
      box_dump(R"(<div style="font-size: 16px"><ruby>桜<rt>さくら</rt></ruby>あいうえお</div>)");
  // 組は [0, 24]: ルビは 0 から始まり（前には掛けない）、後ろの「あ」にだけ掛かる
  EXPECT_EQ(glyph_positions_of(json, "さくら"), (std::vector<std::string>{"0", "8", "16"}));
  EXPECT_EQ(glyph_positions_of(json, "桜"), (std::vector<std::string>{"0"}));
  EXPECT_EQ(field_of(json, "あいうえお", "inline_start"), "16");
}

// 掛ける量はルビ文字サイズの全角（8px）を超えない。
TEST(IntegrationRubyOverhang, NeverHangsMoreThanOneRubyEm) {
  // ルビ 5 文字（40）− 親文字 1 文字（16）= 24。前後 12px ずつではなく 8px ずつで止まる
  const std::string json =
      box_dump(R"(<div style="font-size: 16px">のの<ruby>桜<rt>しゃしょく</rt></ruby>のの</div>)");
  EXPECT_EQ(glyph_positions_of(json, "しゃしょく"),
            (std::vector<std::string>{"24", "32", "40", "48", "56"}));
  // 組の送りは 40 − 8 − 8 = 24（[32, 56]）。掛けきれなかった余り 8 は組の内部の配分に回る
  // （1 クラスタなので中央 = 32 + 4）
  EXPECT_EQ(glyph_positions_of(json, "桜"), (std::vector<std::string>{"36"}));
  EXPECT_EQ(field_of(json, "のの", "inline_start"), "0");
}

// 掛けで行の幅が縮むので、改行位置が変わりうる。
TEST(IntegrationRubyOverhang, CanChangeWhereTheLineBreaks) {
  const RenderOptions options = options_for(64);
  // の（16）+ 組（24）+ のの（32）= 72 > 64。前後に 4px ずつ掛かると 64 でちょうど収まる
  const std::string hung = box_dump(
      R"(<div style="font-size: 16px">の<ruby>桜<rt>さくら</rt></ruby>のの</div>)", options);
  EXPECT_EQ(line_rects(hung).size(), 1U);
  EXPECT_EQ(field_of(hung, "のの", "inline_start"), "32");
  // 掛けられない相手（漢字）なら同じ幅に収まらず 2 行になる
  const std::string plain = box_dump(
      R"(<div style="font-size: 16px">木<ruby>桜<rt>さくら</rt></ruby>木木</div>)", options);
  EXPECT_EQ(line_rects(plain).size(), 2U);
}

// 縦書きでも同じ（進行方向の前後に掛ける）。
TEST(IntegrationRubyOverhang, VerticalUsesTheSameRule) {
  RenderOptions options = options_for(400);
  options.viewport_height = 400;
  const std::string json = box_dump(
      R"(<div style="writing-mode: vertical-rl; font-size: 16px">あの<ruby>桜<rt>さくら</rt></ruby>のえだ</div>)",
      options);
  EXPECT_EQ(glyph_positions_of(json, "桜"), (std::vector<std::string>{"32"}));
  EXPECT_EQ(glyph_positions_of(json, "さくら"), (std::vector<std::string>{"28", "36", "44"}));
  EXPECT_EQ(field_of(json, "のえだ", "inline_start"), "48");
}

}  // namespace
}  // namespace shashoku::test
