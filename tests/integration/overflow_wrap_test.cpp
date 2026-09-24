#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// overflow-wrap（CSS Text Level 3 §5.4）と flex アイテムの自動最小サイズ
// （CSS Flexbox Level 1 §4.5）の組み合わせ（issue #18 / ARCHITECTURE.md A35）。
//
// 単独では通っていた機能が、**組み合わせたときだけ**壊れていた:
// `anywhere` は緊急分割にしか効かず min-content に効かなかったので、flex アイテムは
// 「1 行ぶんの幅」より縮められず、親からはみ出したまま 1 行になっていた。
//
// CSS Text 3 §5.4:
//   anywhere  … min-content intrinsic size の計算でもこの分割位置を考える
//   break-word… 同じだが min-content の計算では考えない（= 1 行ぶんの幅のまま）
// したがって `break-word` / `normal` の flex は**現状維持が正しい**。
//
// 判定は絵ではなくボックスツリーのダンプ（子の inline サイズと行ごとのテキスト）で行う。
namespace shashoku::test {
namespace {

// --dump-stage box の JSON から、最も内側の <div> の inline サイズを取る。
// rect は [inline_start, block_start, inline_size, block_size]（layout の dump_json）なので、
// 横書き・縦書きのどちらでも 3 番目が「行の進む向きの幅」になる。
// テストの HTML は div を入れ子にするだけなので、最後に現れる div が子になる。
float innermost_inline_size(std::string_view json) {
  constexpr std::string_view kTagKey = R"("tag": "div")";
  constexpr std::string_view kRectKey = R"("rect": [)";
  const std::size_t tag_at = json.rfind(kTagKey);
  if (tag_at == std::string_view::npos) {
    ADD_FAILURE() << "div が見つかりません";
    return -1;
  }
  std::size_t at = json.find(kRectKey, tag_at);
  if (at == std::string_view::npos) {
    ADD_FAILURE() << "rect が見つかりません";
    return -1;
  }
  at += kRectKey.size();
  // 3 番目の数（inline_size）まで進む。
  float value = -1;
  for (int index = 0; index < 3; ++index) {
    const std::size_t begin = json.find_first_of("-0123456789", at);
    const std::size_t end = json.find_first_not_of("-0123456789.eE+", begin);
    if (begin == std::string_view::npos || end == std::string_view::npos) {
      ADD_FAILURE() << "rect を読めません";
      return -1;
    }
    value = std::stof(std::string(json.substr(begin, end - begin)));
    at = end;
  }
  return value;
}

// 行ごとのテキスト。`"fragments"` は LineBox 1 つにつきちょうど 1 回出るので行の区切りに使い、
// その後ろの `"text": "…"` をつなぐ（line_policy_test.cpp と同じ読み方）。
std::vector<std::string> line_texts(std::string_view json) {
  constexpr std::string_view kLineMarker = R"("fragments")";
  constexpr std::string_view kTextKey = R"("text": ")";

  std::vector<std::string> lines;
  for (std::size_t at = json.find(kLineMarker); at != std::string_view::npos;
       at = json.find(kLineMarker, at + 1)) {
    const std::size_t stop = json.find(kLineMarker, at + 1);
    std::string current;
    for (std::size_t text_at = json.find(kTextKey, at);
         text_at != std::string_view::npos && text_at < stop;
         text_at = json.find(kTextKey, text_at + 1)) {
      const std::size_t begin = text_at + kTextKey.size();
      const std::size_t end = json.find('"', begin);
      if (end == std::string_view::npos) {
        break;
      }
      current += json.substr(begin, end - begin);
    }
    lines.push_back(current);
  }
  return lines;
}

// 子の inline サイズと行の内容。
struct Flowed {
  float inline_size = -1;
  std::vector<std::string> lines;
};

Flowed flow(std::string_view html, bool vertical) {
  RenderOptions options = options_for(200);
  if (vertical) {
    options.viewport_height = 200;  // 縦書きでは高さが先に決まらないので必須
  }
  const auto json = dump(html, japanese_fonts(), ImageSet{}, options, DumpStage::Box);
  EXPECT_TRUE(json.has_value()) << to_string(json.error());
  if (!json) {
    return {};
  }
  return Flowed{.inline_size = innermost_inline_size(*json), .lines = line_texts(*json)};
}

// ---- 組み合わせの表 ---------------------------------------------------------------

enum class Container : std::uint8_t { FlexRow, FlexColumn, Block };
enum class Where : std::uint8_t { Element, Span };       // overflow-wrap をどこに書くか
enum class Sizing : std::uint8_t { None, Width, Flex };  // 子の主軸サイズの指定

// NotoSansJP-Regular / font-size 16px の実測値（issue #18 の表）。
constexpr float kOneLine = 82.21875F;  // "ABCDEFGH" 1 行ぶん
constexpr float kBox = 32.0F;          // 親の inline サイズ

struct Case {
  Container container;
  Where where;
  Sizing sizing;
  std::string_view wrap;
  float inline_size;                      // 子の inline サイズ
  std::array<std::string_view, 3> lines;  // 行の内容（空文字は「その行は無い」）
  // 著者が書くプロパティ名。`word-wrap` は `overflow-wrap` の legacy name alias
  // （CSS Text 3 §5.4。issue #25）で、名前の表で写し替わるだけなので結果は同じ。
  std::string_view property = "overflow-wrap";
};

// 横書きと縦書きで同じ期待値を使う（書字方向で結果が変わってはいけない）。
constexpr std::array<Case, 31> kCases{{
    // --- anywhere: min-content にも効く（CSS Text 3 §5.4）。flex でも親に収まる ---
    {Container::FlexRow, Where::Element, Sizing::None, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Span, Sizing::None, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Element, Sizing::Width, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Span, Sizing::Width, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Element, Sizing::Flex, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Span, Sizing::Flex, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexColumn, Where::Element, Sizing::None, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexColumn, Where::Span, Sizing::None, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::Block, Where::Element, Sizing::None, "anywhere", kBox, {"ABC", "DEF", "GH"}},
    {Container::Block, Where::Span, Sizing::None, "anywhere", kBox, {"ABC", "DEF", "GH"}},

    // --- break-word: min-content には効かない。**width が auto の** flex の子は
    //     1 行ぶんの幅のままはみ出す（Chrome も同じ）。それ以外は anywhere と同じ ---
    // `flex:1` も width は auto なので、指定なしと同じく 1 行ぶんの幅になる（A52。§4.5 の
    // specified size suggestion は width で、flex-basis: 0 はここに入らない）
    {Container::FlexRow, Where::Element, Sizing::None, "break-word", kOneLine, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Span, Sizing::None, "break-word", kOneLine, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Element, Sizing::Width, "break-word", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Span, Sizing::Width, "break-word", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexRow, Where::Element, Sizing::Flex, "break-word", kOneLine, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Span, Sizing::Flex, "break-word", kOneLine, {"ABCDEFGH"}},
    {Container::FlexColumn, Where::Element, Sizing::None, "break-word", kBox, {"ABC", "DEF", "GH"}},
    {Container::FlexColumn, Where::Span, Sizing::None, "break-word", kBox, {"ABC", "DEF", "GH"}},
    {Container::Block, Where::Element, Sizing::None, "break-word", kBox, {"ABC", "DEF", "GH"}},
    {Container::Block, Where::Span, Sizing::None, "break-word", kBox, {"ABC", "DEF", "GH"}},

    // --- normal: 緊急分割そのものが無いので、どこでも 1 行（A4 禁則 > 幅） ---
    {Container::FlexRow, Where::Element, Sizing::None, "normal", kOneLine, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Span, Sizing::None, "normal", kOneLine, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Element, Sizing::Width, "normal", kBox, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Span, Sizing::Width, "normal", kBox, {"ABCDEFGH"}},
    // `flex:1` の子は自動最小サイズ = min-content（= 1 行ぶん）なのでそこまでしか縮まない（A52）
    {Container::FlexRow, Where::Element, Sizing::Flex, "normal", kOneLine, {"ABCDEFGH"}},
    {Container::FlexRow, Where::Span, Sizing::Flex, "normal", kOneLine, {"ABCDEFGH"}},
    {Container::FlexColumn, Where::Element, Sizing::None, "normal", kBox, {"ABCDEFGH"}},
    {Container::FlexColumn, Where::Span, Sizing::None, "normal", kBox, {"ABCDEFGH"}},
    {Container::Block, Where::Element, Sizing::None, "normal", kBox, {"ABCDEFGH"}},
    {Container::Block, Where::Span, Sizing::None, "normal", kBox, {"ABCDEFGH"}},

    // --- legacy name alias: `word-wrap` で書いても同じ。主軸サイズの決まらない flex の子
    //     （min-content が効くかどうかがそのまま出る場所）で確かめる（issue #25）---
    {Container::FlexRow,
     Where::Element,
     Sizing::None,
     "anywhere",
     kBox,
     {"ABC", "DEF", "GH"},
     "word-wrap"},
}};

std::string describe(const Case& test_case, bool vertical) {
  std::string out = vertical ? "縦書き " : "横書き ";
  switch (test_case.container) {
    case Container::FlexRow:
      out += "flex(row)";
      break;
    case Container::FlexColumn:
      out += "flex(column)";
      break;
    case Container::Block:
      out += "block";
      break;
  }
  out += test_case.where == Where::Span ? " span" : " 要素";
  switch (test_case.sizing) {
    case Sizing::None:
      out += " 指定なし";
      break;
    case Sizing::Width:
      out += " 主軸サイズあり";
      break;
    case Sizing::Flex:
      out += " flex:1";
      break;
  }
  out += ' ';
  out += test_case.property;
  out += ':';
  out += test_case.wrap;
  return out;
}

// 親は inline サイズ 32px（横書きなら width、縦書きなら height）。
std::string html_of(const Case& test_case, bool vertical) {
  const std::string inline_size = vertical ? "height:32px" : "width:32px";
  std::string outer;
  switch (test_case.container) {
    case Container::FlexRow:
      outer = "display:flex;" + inline_size;
      break;
    case Container::FlexColumn:
      outer = "display:flex;flex-direction:column;" + inline_size;
      break;
    case Container::Block:
      outer = inline_size;
      break;
  }

  std::string child;
  switch (test_case.sizing) {
    case Sizing::None:
      break;
    case Sizing::Width:
      child = inline_size + ";";
      break;
    case Sizing::Flex:
      child = "flex:1;";
      break;
  }
  const std::string declaration =
      std::string(test_case.property) + ":" + std::string(test_case.wrap);
  std::string content = "ABCDEFGH";
  if (test_case.where == Where::Span) {
    content = R"(AB<span style=")" + declaration + R"(">CDEFGH</span>)";
  } else {
    child += declaration;
  }

  const std::string body = R"(<div style=")" + outer + R"(;font-size:16px"><div style=")" + child +
                           R"(">)" + content + "</div></div>";
  return vertical ? R"(<div style="writing-mode:vertical-rl">)" + body + "</div>" : body;
}

TEST(OverflowWrap, FlexAndBlockAgreeWithCssText3) {
  for (const Case& test_case : kCases) {
    for (const bool vertical : {false, true}) {
      SCOPED_TRACE(describe(test_case, vertical));
      std::vector<std::string> expected;
      for (const std::string_view line : test_case.lines) {
        if (!line.empty()) {
          expected.emplace_back(line);
        }
      }
      const Flowed flowed = flow(html_of(test_case, vertical), vertical);
      EXPECT_FLOAT_EQ(flowed.inline_size, test_case.inline_size);
      EXPECT_EQ(flowed.lines, expected);
    }
  }
}

// min-content そのものを観測する: 親を min-content より狭くすると、flex アイテムは
// そこで止まる（Flexbox §4.5 の自動最小サイズ）。
TEST(OverflowWrap, AutomaticMinimumSizeIsTheMinContentWidth) {
  // 要素全体に anywhere → min-content は「いちばん広い 1 クラスタ」= "H" の送り。
  for (const bool vertical : {false, true}) {
    SCOPED_TRACE(vertical ? "縦書き" : "横書き");
    const std::string narrow =
        vertical
            ? R"(<div style="writing-mode:vertical-rl"><div style="display:flex;height:8px;font-size:16px"><div style="overflow-wrap:anywhere">ABCDEFGH</div></div></div>)"
            : R"(<div style="display:flex;width:8px;font-size:16px"><div style="overflow-wrap:anywhere">ABCDEFGH</div></div>)";
    EXPECT_FLOAT_EQ(flow(narrow, vertical).inline_size, 11.640625F) << "H の送り";

    // span だけ anywhere → 割れない "ABC" が min-content（A23: 要素の境界では割らない）。
    const std::string span =
        vertical
            ? R"(<div style="writing-mode:vertical-rl"><div style="display:flex;height:8px;font-size:16px"><div>AB<span style="overflow-wrap:anywhere">CDEFGH</span></div></div></div>)"
            : R"(<div style="display:flex;width:8px;font-size:16px"><div>AB<span style="overflow-wrap:anywhere">CDEFGH</span></div></div>)";
    EXPECT_FLOAT_EQ(flow(span, vertical).inline_size, 30.453125F) << "ABC の幅";
  }
}

// issue #18 の現実的な入力（OG カードに長い URL）。flex の子が親からはみ出さないこと。
TEST(OverflowWrap, LongUrlInAFlexCardStaysInsideTheBox) {
  constexpr std::string_view kHtml =
      R"(<div style="display:flex;width:320px;font-size:16px"><div style="overflow-wrap:anywhere">)"
      R"(記事: https://example.com/articles/aVeryLongUnbreakableSlugThatNeverEnds2026</div></div>)";
  const Flowed flowed = flow(kHtml, false);
  EXPECT_FLOAT_EQ(flowed.inline_size, 320.0F);
  EXPECT_EQ(flowed.lines.size(), 3U);
  // 同じ内容をブロックに置いたときと同じ行になる（flex を通しても結果が変わらない）。
  constexpr std::string_view kBlock =
      R"(<div style="width:320px;font-size:16px"><div style="overflow-wrap:anywhere">)"
      R"(記事: https://example.com/articles/aVeryLongUnbreakableSlugThatNeverEnds2026</div></div>)";
  EXPECT_EQ(flowed.lines, flow(kBlock, false).lines);
}

}  // namespace
}  // namespace shashoku::test
