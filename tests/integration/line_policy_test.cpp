#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// インライン要素の行分割ポリシー（issue #2 / ARCHITECTURE.md A23・A28）を、
// 本物のフォントを通した end-to-end で確かめる。
// 判定は絵ではなくボックスツリーのダンプ（行ごとのテキスト）で行う。
namespace shashoku::test {
namespace {

// --dump-stage box の JSON から、行ごとのテキストを取る。
// `"fragments"` は LineBox 1 つにつきちょうど 1 回出る（layout の dump_json）ので行の区切りに使い、
// その後ろの `"text": "…"`（TextFragment のデバッグ用テキスト）をつなぐ。
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

std::vector<std::string> lines_of(std::string_view html, int width) {
  const auto json = dump(html, japanese_fonts(), ImageSet{}, options_for(width), DumpStage::Box);
  EXPECT_TRUE(json.has_value()) << to_string(json.error());
  if (!json) {
    return {};
  }
  return line_texts(*json);
}

// issue #2 の再現表。幅 32px、font-size は既定の 16px（欧文 1 文字がおよそ 10px）。
TEST(LinePolicy, OverflowWrapOnASpanIsNotIgnored) {
  // 1 行目: ブロックに書いた場合（もともと効いていた）
  const std::vector<std::string> on_block =
      lines_of(R"(<div style="width:32px;overflow-wrap:anywhere">ABCDEFGH</div>)", 200);
  // 2 行目: span に書いた場合（#2 で直したところ）
  const std::vector<std::string> on_span = lines_of(
      R"(<div style="width:32px"><span style="overflow-wrap:anywhere">ABCDEFGH</span></div>)", 200);
  // 3 行目: 指定なし（割らずにはみ出す。A4）
  const std::vector<std::string> plain = lines_of(R"(<div style="width:32px">ABCDEFGH</div>)", 200);

  EXPECT_GT(on_block.size(), 1U) << "ブロックの overflow-wrap が効いていない";
  EXPECT_EQ(on_span, on_block) << "span の overflow-wrap が黙って無視されている（#2）";
  EXPECT_EQ(plain, (std::vector<std::string>{"ABCDEFGH"}));
  EXPECT_NE(on_span, plain) << "2 行目と 3 行目が区別できていない（#2 の症状そのもの）";
}

// span に書いた line-break が効く（loose では「・」の前でも割れる）。
TEST(LinePolicy, LineBreakOnASpanIsNotIgnored) {
  // 幅 20px = 全角 1 文字ぶん強。「あ・」は入らないので、どこで割るかがポリシーで変わる
  constexpr std::string_view kPlain = R"(<div style="width:20px">あ・い</div>)";
  constexpr std::string_view kOnSpan =
      R"(<div style="width:20px"><span style="line-break:loose">あ・い</span></div>)";
  constexpr std::string_view kOnBlock = R"(<div style="width:20px;line-break:loose">あ・い</div>)";

  const std::vector<std::string> plain = lines_of(kPlain, 200);
  const std::vector<std::string> on_span = lines_of(kOnSpan, 200);
  const std::vector<std::string> on_block = lines_of(kOnBlock, 200);

  EXPECT_EQ(plain, (std::vector<std::string>{"あ・", "い"}));  // strict: 行頭に「・」を出さない
  EXPECT_EQ(on_block, (std::vector<std::string>{"あ", "・", "い"}));
  EXPECT_EQ(on_span, on_block) << "span の line-break が黙って無視されている（#2）";
}

// ブロックに書いた場合と、全文を包む span に書いた場合で結果が同じ（性質テスト）。
TEST(LinePolicy, BlockLevelAndSpanLevelAgreeOnRealFonts) {
  struct Case {
    std::string_view declaration;
    std::string_view text;
    int width = 0;
  };
  constexpr std::array<Case, 6> kCases{{
      {"overflow-wrap:anywhere", "ABCDEFGHIJ", 40},
      {"overflow-wrap:break-word", "shashoku", 40},
      {"line-break:loose", "あ・い・う", 40},
      {"line-break:normal", "あっいっう", 40},
      {"line-break:strict", "あっいっう", 40},
      {"line-break:loose", "組版々の話", 48},
  }};
  for (const Case& test_case : kCases) {
    const std::string on_block =
        std::string(R"(<div style="width:)") + std::to_string(test_case.width) + "px;" +
        std::string(test_case.declaration) + R"(">)" + std::string(test_case.text) + "</div>";
    const std::string on_span = std::string(R"(<div style="width:)") +
                                std::to_string(test_case.width) + R"(px"><span style=")" +
                                std::string(test_case.declaration) + R"(">)" +
                                std::string(test_case.text) + "</span></div>";
    EXPECT_EQ(lines_of(on_span, 200), lines_of(on_block, 200))
        << test_case.declaration << " / " << test_case.text;
  }
}

}  // namespace
}  // namespace shashoku::test
