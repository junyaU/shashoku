#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "shashoku/shashoku.hpp"

// 公開 API そのものの振る舞い（ARCHITECTURE.md §3.10）。
// 組版の中身は tests/integration/ が見る。ここは「箱」の検査。
namespace shashoku::header_check {
int use_public_api();  // public_header_check.cpp（include/ だけでコンパイルされる TU）
}  // namespace shashoku::header_check

namespace shashoku::test {
namespace {

// ---------------------------------------------------------------------------
// 公開ヘッダの隔離
// ---------------------------------------------------------------------------

// include パスを include/ だけに絞った TU がリンクできている
// （＝公開ヘッダが src/ の内部ヘッダも FreeType / HarfBuzz も引いていない）。
TEST(PublicHeaders, IsolatedTranslationUnitLinks) { EXPECT_EQ(header_check::use_public_api(), 0); }

// コンパイルでの検査に加えて、テキストとしても見る（将来ヘッダを足したときの網）。
TEST(PublicHeaders, IncludeOnlyPublicAndStandardHeaders) {
  const std::filesystem::path dir{SHASHOKU_PUBLIC_INCLUDE_DIR};
  ASSERT_TRUE(std::filesystem::is_directory(dir)) << dir;

  std::size_t checked = 0;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".hpp") {
      continue;
    }
    ++checked;
    std::ifstream input(entry.path());
    ASSERT_TRUE(input.is_open()) << entry.path();
    std::string source_line;
    for (int number = 1; std::getline(input, source_line); ++number) {
      // 行頭（空白を除く）の #include だけを見る。コメント中の例文は対象外。
      const std::size_t start = source_line.find_first_not_of(" \t");
      if (start == std::string::npos || source_line.compare(start, 8, "#include") != 0) {
        continue;
      }
      const std::string_view rest = std::string_view(source_line).substr(start + 8);
      const std::size_t open = rest.find_first_of("\"<");
      ASSERT_NE(open, std::string_view::npos) << entry.path() << ':' << number;
      const bool quoted = rest[open] == '"';
      const std::size_t close = rest.find(quoted ? '"' : '>', open + 1);
      ASSERT_NE(close, std::string_view::npos) << entry.path() << ':' << number;
      const std::string_view target = rest.substr(open + 1, close - open - 1);

      if (quoted) {
        // 引用符つきは公開ヘッダ同士だけ（"core/..." のような内部ヘッダは不可）
        EXPECT_TRUE(target.starts_with("shashoku/"))
            << entry.path().filename() << ':' << number << " → \"" << target << '"';
      } else {
        // 山括弧は標準ライブラリだけ（<freetype/...> や <hb.h> を弾く）
        EXPECT_EQ(target.find('/'), std::string_view::npos)
            << entry.path().filename() << ':' << number << " → <" << target << '>';
        EXPECT_EQ(target.find("ft2build"), std::string_view::npos) << entry.path().filename();
        EXPECT_EQ(target.find("hb"), std::string_view::npos) << entry.path().filename();
      }
    }
  }
  EXPECT_GE(checked, 7U) << "公開ヘッダが減っている？";
}

// ---------------------------------------------------------------------------
// FontSet / ImageSet
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

TEST(FontSetTest, KeepsInsertionOrderAndCopiesBytes) {
  std::vector<std::uint8_t> source = bytes_of("first");
  FontSet fonts;
  EXPECT_TRUE(fonts.empty());
  fonts.add(source);
  fonts.add(bytes_of("second"));

  // 追加したあとに元のバイト列を壊しても、FontSet の中身は変わらない（コピーして持つ）
  source.assign(source.size(), 0);

  ASSERT_EQ(fonts.size(), 2U);
  EXPECT_FALSE(fonts.empty());
  const std::span<const std::uint8_t> first = fonts.at(0);
  EXPECT_EQ(std::string(first.begin(), first.end()), "first");
  const std::span<const std::uint8_t> second = fonts.at(1);
  EXPECT_EQ(std::string(second.begin(), second.end()), "second");
  EXPECT_TRUE(fonts.at(2).empty());  // 範囲外でも落ちない
}

TEST(ImageSetTest, KeepsNamesAndBytes) {
  ImageSet images;
  EXPECT_TRUE(images.empty());
  images.add("icon", bytes_of("png-bytes"));
  images.add("logo", bytes_of("more"));

  ASSERT_EQ(images.size(), 2U);
  EXPECT_EQ(images.name(0), "icon");
  EXPECT_EQ(images.name(1), "logo");
  const std::span<const std::uint8_t> first = images.bytes(0);
  EXPECT_EQ(std::string(first.begin(), first.end()), "png-bytes");
  EXPECT_TRUE(images.name(2).empty());
  EXPECT_TRUE(images.bytes(2).empty());
}

// ---------------------------------------------------------------------------
// 文字列化
// ---------------------------------------------------------------------------

TEST(ApiStrings, DumpStage) {
  EXPECT_EQ(to_string(DumpStage::Dom), "dom");
  EXPECT_EQ(to_string(DumpStage::Style), "style");
  EXPECT_EQ(to_string(DumpStage::Box), "box");
  EXPECT_EQ(to_string(DumpStage::DisplayList), "display-list");
  EXPECT_EQ(to_string(DumpStage::Svg), "svg");
}

TEST(ApiStrings, WarningKind) { EXPECT_EQ(to_string(WarningKind::MissingGlyph), "missing-glyph"); }

TEST(ApiStrings, Version) {
  EXPECT_FALSE(version().empty());
  EXPECT_NE(version().find('.'), std::string_view::npos);
}

// ---------------------------------------------------------------------------
// フォントなしでも判定できるエラー（フォントの読み込みより前に返るもの）
// ---------------------------------------------------------------------------

TEST(RenderErrors, EmptyFontSet) {
  const auto result = render("<p>あ</p>", FontSet{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::NoFonts);
}

// オプションの検証は HTML のパースより前（壊れた HTML でも InvalidOption が返る）。
TEST(RenderErrors, OptionsAreValidatedFirst) {
  RenderOptions options;
  options.viewport_width = 0;
  const auto result = render("<div>", FontSet{}, options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(result.error().message.find("viewport width"), std::string::npos);
}

TEST(RenderErrors, BrokenHtmlBeforeFonts) {
  // fail loudly: 閉じ忘れは HtmlParse。FontSet が空でも HTML のエラーが先に出る
  const auto result = render("<div><p>あ</div>", FontSet{});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::HtmlParse);
  EXPECT_TRUE(result.error().location.has_value());
}

// Dom / Style のダンプはフォントを見ない（空の FontSet でも成功する）。
TEST(DumpStages, DomAndStyleWorkWithoutFonts) {
  const auto dom = dump("<p>あ</p>", FontSet{}, ImageSet{}, RenderOptions{}, DumpStage::Dom);
  ASSERT_TRUE(dom.has_value()) << to_string(dom.error());
  EXPECT_TRUE(dom->starts_with("{"));

  const auto styled = dump("<p>あ</p>", FontSet{}, ImageSet{}, RenderOptions{}, DumpStage::Style);
  ASSERT_TRUE(styled.has_value()) << to_string(styled.error());
  EXPECT_TRUE(styled->starts_with("{"));
}

TEST(DumpStages, BoxNeedsFonts) {
  const auto box = dump("<p>あ</p>", FontSet{}, ImageSet{}, RenderOptions{}, DumpStage::Box);
  ASSERT_FALSE(box.has_value());
  EXPECT_EQ(box.error().kind, ErrorKind::NoFonts);
}

}  // namespace
}  // namespace shashoku::test
