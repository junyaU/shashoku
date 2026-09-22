#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// 空のインラインボックスの font-size / line-height が行の高さに効く（issue #23 / A42）。
//
// 期待値は issue #23 の再現表（本物の Noto Sans JP + Chrome 153 の実測）。
// tests/layout/empty_inline_test.cpp が偽の TextMeasurer で規則を固定するのに対し、
// ここは**実フォントの数値そのもの**（115.84375 / 60 / 23.171875）を固定する。
// Chrome の値（116 / 60 / 24）との 0.8 px の差は `line-height: normal` の作り方の違い
// （Chrome は ascent / descent を整数 px に丸める。chrome_compare.md §4-5）。
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

// box ダンプから行ボックスの block_size（矩形の 4 番目）を文書順に取り出す。
// 行ボックスの矩形は「"rect" の直後のキーが "baseline"」で一意に見分けられる
// （ブロックは "lines" / "blocks" など、画像断片は "content_rect" が続く）。
// 数値は**文字列のまま**比べる（比較のために丸めを持ち込まない）。
std::vector<std::string> line_block_sizes(std::string_view json) {
  const std::string compact = strip_spaces(json);
  constexpr std::string_view kKey = R"("rect":[)";
  constexpr std::string_view kAfter = R"(,"baseline":)";
  std::vector<std::string> out;
  for (std::size_t at = compact.find(kKey); at != std::string::npos;
       at = compact.find(kKey, at + 1)) {
    const std::size_t begin = at + kKey.size();
    const std::size_t end = compact.find(']', begin);
    if (end == std::string::npos) {
      break;
    }
    if (compact.compare(end + 1, kAfter.size(), kAfter) != 0) {
      continue;  // 行ボックスではない矩形
    }
    const std::size_t last = compact.rfind(',', end);
    if (last == std::string::npos || last < begin) {
      continue;
    }
    out.push_back(compact.substr(last + 1, end - last - 1));
  }
  return out;
}

std::vector<std::string> line_heights_of(std::string_view html, const RenderOptions& options) {
  const auto json = dump(html, japanese_fonts(), ImageSet{}, options, DumpStage::Box);
  if (!json) {
    ADD_FAILURE() << "dump: " << to_string(json.error());
    return {};
  }
  return line_block_sizes(*json);
}

std::vector<std::string> line_heights_of(std::string_view html) {
  return line_heights_of(html, options_for(400));
}

// ---- issue #23 の再現表（本物のフォントでの数値） ---------------------------------------

TEST(IntegrationEmptyInline, EmptyInlineBoxContributesToLineHeight) {
  // 基準: font-size 16px の行（Chrome 24）
  EXPECT_EQ(line_heights_of("<div>A</div>"), std::vector<std::string>{"23.171875"});
  // 空 span の font-size（Chrome 116）
  EXPECT_EQ(line_heights_of(R"(<div>A<span style="font-size:80px"></span></div>)"),
            std::vector<std::string>{"115.84375"});
  // 空 span の line-height（Chrome 60）
  EXPECT_EQ(line_heights_of(R"(<div>A<span style="line-height:60px"></span></div>)"),
            std::vector<std::string>{"60"});
  // テキストの前にあっても同じ（Chrome 60）
  EXPECT_EQ(line_heights_of(R"(<div><span style="line-height:60px"></span>A</div>)"),
            std::vector<std::string>{"60"});
}

TEST(IntegrationEmptyInline, VerticalEmptyInlineBoxContributesToLineHeight) {
  RenderOptions options = options_for(400);
  options.viewport_height = 400;
  EXPECT_EQ(
      line_heights_of(
          R"(<div style="writing-mode: vertical-rl">あ<span style="font-size:80px"></span>い</div>)",
          options),
      std::vector<std::string>{"115.84375"});
}

// 下 3 つは CSS 2.1 §10.8.1 と Chrome に一致していて、**直したあとも変わってはいけない**。
TEST(IntegrationEmptyInline, CasesThatMustNotChange) {
  // 空 span だけの段落は行ボックスを作らない（高さ 0）
  EXPECT_TRUE(line_heights_of(R"(<div><span style="font-size:80px"></span></div>)").empty());
  // <br> の直後で、後ろにアイテムが無い空 span はどの行にも参加しない
  EXPECT_EQ(line_heights_of(R"(<div>A<br><span style="font-size:80px"></span></div>)"),
            std::vector<std::string>{"23.171875"});
  // 畳み込み後に空白 1 個が残る span は空ではない（もともと効いている）
  EXPECT_EQ(line_heights_of(R"(<div>A<span style="font-size:80px"> </span>B</div>)"),
            std::vector<std::string>{"115.84375"});
}

}  // namespace
}  // namespace shashoku::test
