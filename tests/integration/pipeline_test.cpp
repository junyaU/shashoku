#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

// パイプライン全体の性質: 決定性・出力サイズ・各段のダンプ・豆腐の警告。
namespace shashoku::test {
namespace {

constexpr std::string_view kSample =
    R"(<div style="padding: 8px; font-size: 16px; line-height: 1.5">こんにちは、世界のみんな。</div>)";

// DESIGN.md §3-5: 同じ入力 → バイト単位で同じ PNG。
TEST(Determinism, SameInputGivesSameBytes) {
  const FontSet fonts = japanese_fonts();
  const RenderOptions options = options_for(320);
  const auto first = render(kSample, fonts, options);
  const auto second = render(kSample, fonts, options);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(first->png, second->png);
  EXPECT_EQ(first->width, second->width);
  EXPECT_EQ(first->height, second->height);
}

// 別々に作った FontSet からでも同じ結果になる（グローバル状態を持たない）。
TEST(Determinism, FreshFontSetsGiveSameBytes) {
  const auto first = render(kSample, japanese_fonts(), options_for(320));
  const auto second = render(kSample, japanese_fonts(), options_for(320));
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(first->png, second->png);
}

// ---------------------------------------------------------------------------
// 画像の経路（A12）: ImageSet → png::decode → ImageLookup → ImageFragment
//                    → DrawImage → ラスタライザの画像テーブル
// ---------------------------------------------------------------------------

constexpr std::string_view kImageHtml =
    R"(<div style="padding: 6px"><img src="icon" style="width: 32px; height: 32px"> 図版</div>)";

TEST(Images, RendersAndIsDeterministic) {
  const auto first = render(kImageHtml, japanese_fonts(), icon_images(), options_for(200));
  const auto second = render(kImageHtml, japanese_fonts(), icon_images(), options_for(200));
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(first->png, second->png);
  EXPECT_TRUE(first->warnings.empty());
}

// 画像の固有寸法（64x64）が幅・高さの指定なしでそのまま使われる。
// display: block で測る（インラインの画像はベースラインに乗るので、行ボックスに
// フォントのディセントぶんが足される）。
TEST(Images, IntrinsicSize) {
  const auto result = render(R"(<img src="icon" style="display: block">)", japanese_fonts(),
                             icon_images(), options_for(200));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->height, 64);
}

// 名前が違えば別の画像として引ける（添字 = ImageId の対応が崩れていない）。
TEST(Images, SeveralImagesKeepTheirNames) {
  ImageSet images;
  images.add("first", test_icon_bytes());
  images.add("second", test_icon_bytes());
  const auto result =
      render(R"(<img src="second" style="display: block; width: 16px; height: 16px">)",
             japanese_fonts(), images, options_for(200));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->height, 16);
}

TEST(Images, BrokenPngIsRejected) {
  ImageSet images;
  std::vector<std::uint8_t> broken = test_icon_bytes();
  ASSERT_GT(broken.size(), 40U);
  broken[30] ^= 0xFFU;  // IHDR の中身を壊す（CRC が合わなくなる）
  images.add("icon", broken);

  const auto result = render(kImageHtml, japanese_fonts(), images, options_for(200));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::ImageDecode) << to_string(result.error());
  EXPECT_NE(result.error().message.find("icon"), std::string::npos) << result.error().message;
}

TEST(Images, DuplicateNameIsRejected) {
  ImageSet images;
  images.add("icon", test_icon_bytes());
  images.add("icon", test_icon_bytes());
  const auto result = render(kImageHtml, japanese_fonts(), images, options_for(200));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::InvalidOption);
}

// ---------------------------------------------------------------------------
// font-weight とフォント選択（ARCHITECTURE.md §3.5）
//
// 回帰テスト: かつて太さの照合が「font-family で名前が一致した family の中だけ」で
// 働いていたため、**font-family を書かない HTML では <h1> が太字にならなかった**。
// 絵を見るだけでは Regular と Bold を取り違えるので、ディスプレイリストの FontId を
// 数値で確かめる（FontId は FontSet の追加順: 0 = Regular, 1 = Bold）。
// ---------------------------------------------------------------------------

// dump(DisplayList) の draw_glyphs から (font, size) を拾う。
// キー順は paint の dump_json が固定している（op → font → size → color → …）。
std::vector<std::pair<std::string, std::string>> glyph_runs(std::string_view json) {
  constexpr std::string_view kMarker = R"("op": "draw_glyphs")";
  const auto value_after = [json](std::string_view key, std::size_t from) {
    const std::size_t at = json.find(key, from);
    if (at == std::string_view::npos) {
      return std::string{};
    }
    const std::size_t begin = at + key.size();
    const std::size_t end = json.find_first_of(",\n", begin);
    return std::string(json.substr(begin, end - begin));
  };

  std::vector<std::pair<std::string, std::string>> runs;
  for (std::size_t at = json.find(kMarker); at != std::string_view::npos;
       at = json.find(kMarker, at + 1)) {
    runs.emplace_back(value_after(R"("font": )", at), value_after(R"("size": )", at));
  }
  return runs;
}

TEST(FontWeight, HeadingUsesBoldWithoutFontFamily) {
  // font-family を書かない。h1 は UA スタイルシートで bold になる（§3.7）
  constexpr std::string_view kHtml =
      R"(<div><h1 style="font-size: 32px">見出し</h1><p style="font-size: 16px">本文</p></div>)";

  const auto json =
      dump(kHtml, japanese_fonts(), ImageSet{}, options_for(400), DumpStage::DisplayList);
  ASSERT_TRUE(json.has_value()) << to_string(json.error());

  const std::vector<std::pair<std::string, std::string>> runs = glyph_runs(*json);
  ASSERT_EQ(runs.size(), 2U) << *json;
  // 文書順に h1 → p
  EXPECT_EQ(runs[0].second, "32") << *json;
  EXPECT_EQ(runs[0].first, "1") << "h1 が Bold（FontId 1）で描かれていない\n" << *json;
  EXPECT_EQ(runs[1].second, "16") << *json;
  EXPECT_EQ(runs[1].first, "0") << "本文が Regular（FontId 0）で描かれていない\n" << *json;
}

// font-family は family の優先順を変えるだけで、太さの照合はいつも全 family に働く。
TEST(FontWeight, ExplicitWeightPicksBold) {
  constexpr std::string_view kHtml =
      R"(<div style="font-size: 20px"><span style="font-weight: 700">太</span><span>細</span></div>)";

  const auto json =
      dump(kHtml, japanese_fonts(), ImageSet{}, options_for(400), DumpStage::DisplayList);
  ASSERT_TRUE(json.has_value()) << to_string(json.error());

  const std::vector<std::pair<std::string, std::string>> runs = glyph_runs(*json);
  ASSERT_EQ(runs.size(), 2U) << *json;
  EXPECT_EQ(runs[0].first, "1") << *json;  // font-weight: 700 → Bold
  EXPECT_EQ(runs[1].first, "0") << *json;  // 既定の 400 → Regular
}

// Bold を渡していなければ Regular で描く（落ちない・黙って崩さない）。
TEST(FontWeight, FallsBackWhenNoBoldIsAvailable) {
  FontSet regular_only;
  regular_only.add(text::assets::noto_sans_jp_regular());

  const auto json = dump(R"(<h1 style="font-size: 32px">見出し</h1>)", regular_only, ImageSet{},
                         options_for(400), DumpStage::DisplayList);
  ASSERT_TRUE(json.has_value()) << to_string(json.error());

  const std::vector<std::pair<std::string, std::string>> runs = glyph_runs(*json);
  ASSERT_EQ(runs.size(), 1U) << *json;
  EXPECT_EQ(runs[0].first, "0") << *json;
}

// ---------------------------------------------------------------------------
// 出力サイズ（ARCHITECTURE.md §3.10）
// ---------------------------------------------------------------------------

TEST(OutputSize, HeightFollowsContent) {
  const RenderOptions options = options_for(320);
  const auto one_line =
      render(R"(<div style="line-height: 20px">あ</div>)", japanese_fonts(), options);
  const auto two_lines =
      render(R"(<div style="line-height: 20px">あ<br>い</div>)", japanese_fonts(), options);
  ASSERT_TRUE(one_line.has_value()) << to_string(one_line.error());
  ASSERT_TRUE(two_lines.has_value()) << to_string(two_lines.error());
  EXPECT_EQ(one_line->width, 320);
  EXPECT_EQ(one_line->height, 20);
  EXPECT_EQ(two_lines->height, 40);
}

TEST(OutputSize, ExplicitHeightWins) {
  RenderOptions options = options_for(320);
  options.viewport_height = 100;
  const auto result =
      render(R"(<div style="line-height: 20px">あ</div>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->width, 320);
  EXPECT_EQ(result->height, 100);
}

// デバイスピクセル = ceil(CSS px * scale)。
TEST(OutputSize, ScaleMultipliesDevicePixels) {
  RenderOptions options = options_for(101);
  options.viewport_height = 51;
  options.scale = 2.0F;
  const auto result =
      render(R"(<div style="line-height: 20px">あ</div>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->width, 202);
  EXPECT_EQ(result->height, 102);
}

// 内容が空で高さの指定もなければ「描くものがない」（InvalidOption）。
TEST(OutputSize, NothingToRender) {
  const auto result = render("", japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::InvalidOption);
  EXPECT_NE(result.error().message.find("nothing to render"), std::string::npos);
}

// 縦書きでは内容が伸びる向きが横なので、高さを推定できない（ARCHITECTURE.md §3.8 / A1）。
TEST(OutputSize, VerticalNeedsExplicitHeight) {
  constexpr std::string_view kVertical = R"(<div style="writing-mode: vertical-rl">縦書き</div>)";
  const auto without = render(kVertical, japanese_fonts(), options_for(320));
  ASSERT_FALSE(without.has_value());
  EXPECT_EQ(without.error().kind, ErrorKind::InvalidOption) << to_string(without.error());

  RenderOptions options = options_for(320);
  options.viewport_height = 240;
  const auto with = render(kVertical, japanese_fonts(), options);
  ASSERT_TRUE(with.has_value()) << to_string(with.error());
  EXPECT_EQ(with->width, 320);
  EXPECT_EQ(with->height, 240);
}

TEST(OutputSize, EmptyDocumentWithExplicitHeightIsFine) {
  RenderOptions options = options_for(320);
  options.viewport_height = 40;
  const auto result = render("", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->height, 40);
}

// ---------------------------------------------------------------------------
// 豆腐（DESIGN.md §3-6 の唯一の例外: エラーではなく警告で続行）
// ---------------------------------------------------------------------------

TEST(Warnings, MissingGlyphIsReportedAndRenderingContinues) {
  const auto result = render(R"(<div style="font-size: 20px">ABC😀あ</div>)", latin_then_japanese(),
                             options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 1U);
  EXPECT_EQ(result->warnings[0].kind, WarningKind::MissingGlyph);
  EXPECT_EQ(result->warnings[0].codepoint, U'\U0001F600');
  EXPECT_EQ(result->warnings[0].detail, "no font has a glyph for U+1F600");
  EXPECT_FALSE(result->png.empty());
}

// 警告の並びはコードポイント昇順に固定する（出現順ではない）。
TEST(Warnings, SortedByCodepoint) {
  const auto result =
      render(R"(<div style="font-size: 20px">😀あ😃</div>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 2U);
  EXPECT_EQ(result->warnings[0].codepoint, U'\U0001F600');
  EXPECT_EQ(result->warnings[1].codepoint, U'\U0001F603');
}

TEST(Warnings, NoneForOrdinaryJapanese) {
  const auto result = render(kSample, japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(result->warnings.empty());
}

// ---------------------------------------------------------------------------
// --dump-stage（DESIGN.md §3-3）
// ---------------------------------------------------------------------------

TEST(DumpStages, EveryStageProducesOutput) {
  const FontSet fonts = japanese_fonts();
  const RenderOptions options = options_for(320);
  for (const DumpStage stage :
       {DumpStage::Dom, DumpStage::Style, DumpStage::Box, DumpStage::DisplayList}) {
    const auto dumped = dump(kSample, fonts, ImageSet{}, options, stage);
    ASSERT_TRUE(dumped.has_value()) << to_string(stage) << ": " << to_string(dumped.error());
    EXPECT_TRUE(dumped->starts_with("{")) << to_string(stage) << ": " << *dumped;
    EXPECT_GT(dumped->size(), 16U) << to_string(stage);
  }

  const auto svg = dump(kSample, fonts, ImageSet{}, options, DumpStage::Svg);
  ASSERT_TRUE(svg.has_value()) << to_string(svg.error());
  EXPECT_NE(svg->find("<svg"), std::string::npos);
  EXPECT_NE(svg->find("</svg>"), std::string::npos);
}

// ダンプも決定的（同じ入力なら同じ文字列）。
TEST(DumpStages, Deterministic) {
  const auto first = dump(kSample, japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  const auto second = dump(kSample, japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, *second);
}

// ダンプのボックスツリーは、レイアウトが本当に走っていることを示す座標を持つ。
TEST(DumpStages, BoxContainsGlyphPositions) {
  const auto box = dump(kSample, japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  ASSERT_TRUE(box.has_value()) << to_string(box.error());
  EXPECT_NE(box->find("\"glyphs\""), std::string::npos);
  EXPECT_NE(box->find("\"baseline\""), std::string::npos);
  EXPECT_NE(box->find("こんにちは、世界のみんな。"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::test
