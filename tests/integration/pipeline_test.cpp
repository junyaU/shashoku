#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/result.hpp"
#include "integration/integration_support.hpp"
#include "png/png.hpp"
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
// PNG の圧縮レベル（ARCHITECTURE.md A32）
//
// レベルは「入力の一部」。同じ入力 + 同じレベルなら同じバイト列が出る（DESIGN.md §3-5）。
// 変わるのは IDAT の縮み方だけで、絵（デコードした画素）は 1 ビットも変わらない。
// ---------------------------------------------------------------------------

TEST(CompressionLevel, DefaultIsSix) {
  EXPECT_EQ(RenderOptions{}.compression_level, 6);

  RenderOptions explicit_six = options_for(320);
  explicit_six.compression_level = 6;
  const auto with_default = render(kSample, japanese_fonts(), options_for(320));
  const auto with_six = render(kSample, japanese_fonts(), explicit_six);
  ASSERT_TRUE(with_default.has_value()) << to_string(with_default.error());
  ASSERT_TRUE(with_six.has_value()) << to_string(with_six.error());
  EXPECT_EQ(with_default->png, with_six->png);
}

TEST(CompressionLevel, ChangesTheSizeButNotThePixels) {
  RenderOptions fastest = options_for(320);
  fastest.compression_level = 0;
  RenderOptions smallest = options_for(320);
  smallest.compression_level = 9;

  const auto loose = render(kSample, japanese_fonts(), fastest);
  const auto tight = render(kSample, japanese_fonts(), smallest);
  ASSERT_TRUE(loose.has_value()) << to_string(loose.error());
  ASSERT_TRUE(tight.has_value()) << to_string(tight.error());
  EXPECT_GT(loose->png.size(), tight->png.size());
  EXPECT_EQ(loose->width, tight->width);
  EXPECT_EQ(loose->height, tight->height);

  const Result<Bitmap> loose_pixels = png::decode(loose->png);
  const Result<Bitmap> tight_pixels = png::decode(tight->png);
  ASSERT_TRUE(loose_pixels.has_value()) << to_string(loose_pixels.error());
  ASSERT_TRUE(tight_pixels.has_value()) << to_string(tight_pixels.error());
  EXPECT_EQ(loose_pixels->rgba, tight_pixels->rgba);
}

TEST(CompressionLevel, EveryLevelRendersTheSamePicture) {
  const auto reference = render(kSample, japanese_fonts(), options_for(320));
  ASSERT_TRUE(reference.has_value()) << to_string(reference.error());
  const Result<Bitmap> expected = png::decode(reference->png);
  ASSERT_TRUE(expected.has_value()) << to_string(expected.error());

  for (int level = 0; level <= 9; ++level) {
    RenderOptions options = options_for(320);
    options.compression_level = level;
    const auto result = render(kSample, japanese_fonts(), options);
    ASSERT_TRUE(result.has_value()) << "level " << level << ": " << to_string(result.error());
    const Result<Bitmap> pixels = png::decode(result->png);
    ASSERT_TRUE(pixels.has_value()) << "level " << level << ": " << to_string(pixels.error());
    EXPECT_EQ(pixels->rgba, expected->rgba) << "level " << level;

    // 同じレベルで 2 回描けばバイト列まで一致する。
    const auto again = render(kSample, japanese_fonts(), options);
    ASSERT_TRUE(again.has_value()) << "level " << level;
    EXPECT_EQ(result->png, again->png) << "level " << level;
  }
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

// 警告 1 件を「コードポイント + 行:桁」で書ける短縮形（期待値を読みやすくする）。
struct ExpectedWarning {
  char32_t codepoint = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

// 位置つきの警告（ARCHITECTURE.md A31 / issue #9）。位置は「その文字を含むテキストノードの
// 先頭」で、報告の粒度は (コードポイント, テキストノード) の組ごとに 1 件。
void expect_warnings(const std::vector<Warning>& warnings,
                     const std::vector<ExpectedWarning>& expected, std::string_view what) {
  ASSERT_EQ(warnings.size(), expected.size()) << what;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(warnings[i].kind, WarningKind::MissingGlyph) << what << " #" << i;
    EXPECT_EQ(warnings[i].codepoint, expected[i].codepoint) << what << " #" << i;
    const SourceLocation location =
        warnings[i].location.value_or(SourceLocation{.offset = 0, .line = 0, .column = 0});
    EXPECT_EQ(location.line, expected[i].line) << what << " #" << i;
    EXPECT_EQ(location.column, expected[i].column) << what << " #" << i;
    // detail には RenderError と同じ書式で位置が入る
    EXPECT_EQ(warnings[i].detail, std::format("no font has a glyph for U+{:04X} at {}:{}",
                                              static_cast<std::uint32_t>(expected[i].codepoint),
                                              expected[i].line, expected[i].column))
        << what << " #" << i;
  }
}

TEST(Warnings, MissingGlyphIsReportedAndRenderingContinues) {
  //                                          1         2         3
  //                                 123456789012345678901234567890
  const auto result = render(R"(<div style="font-size: 20px">ABC😀あ</div>)", latin_then_japanese(),
                             options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 30}}, "tofu");
  EXPECT_FALSE(result->png.empty());
}

// 同じテキストノードに同じ絵文字が何個あっても 1 件（粒度は「コードポイント × ノード」）。
// 並びは入力位置の昇順 → コードポイントの昇順。
TEST(Warnings, DeduplicatedPerNodeAndSortedByPositionThenCodepoint) {
  //                       1         2         3
  //              1234567890123456789012345678901234567
  const auto result = render(R"(<div>😃あ😀😀</div><p>😀</p>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings,
                  {{U'\U0001F600', 1, 6}, {U'\U0001F603', 1, 6}, {U'\U0001F600', 1, 19}}, "dedup");
}

// 位置が違うだけの `<span>` は shape() を切らない（A27 / A31）が、警告は別件になる。
TEST(Warnings, SameCodepointInDifferentNodesIsReportedSeparately) {
  //                       1         2         3         4
  //              1234567890123456789012345678901234567890123456
  const auto result =
      render(R"(<div><span>😀</span><span>😀</span></div>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 12}, {U'\U0001F600', 1, 26}}, "two spans");
}

// 文字参照（&#x1F600;）でも位置はテキストノードの先頭。桁をコードポイント単位で
// 遡らないので、参照の綴りの長さに引きずられない。
TEST(Warnings, CharacterReferenceReportsTheTextNodeStart) {
  //                       1         2
  //              1234567890123456789012345
  const auto result = render(R"(<div>ab&#x1F600;</div>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 6}}, "character reference");
}

// 空白の畳み込み（A14）と改行をまたいでも、元のテキストノードの先頭が出る。
TEST(Warnings, WhitespaceCollapsingKeepsTheNodeStart) {
  constexpr std::string_view kHtml = R"(<div>
  あ
  😀
</div>)";
  const auto result = render(kHtml, japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 6}}, "collapsed whitespace");
}

// <br> はテキストノードを割る。前後で別のノードなので位置も別。
TEST(Warnings, BrSplitsTheTextNodes) {
  //                       1         2
  //              12345678901234567890123456
  const auto result = render(R"(<div>😀<br>😃</div>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 6}, {U'\U0001F603', 1, 11}}, "br");
}

// ルビ: 親文字はそのテキストノード、<rt> は要素そのものの位置（ルビ文字は <rt> の子を
// 連結して空白を畳み込んだ 1 本なので、個々のテキストノードには遡らない。A31）。
TEST(Warnings, RubyBaseAndRtAreReportedAtTheirOwnNodes) {
  //                       1         2         3         4
  //              1234567890123456789012345678901234567890123456
  const auto result =
      render(R"(<div><ruby>😀<rt>😃</rt></ruby></div>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 12}, {U'\U0001F603', 1, 13}}, "ruby");
}

// フォールバックで run が分かれても（欧文 → 和文 → 豆腐）位置は変わらない。
TEST(Warnings, FontFallbackRunBoundariesDoNotMoveThePosition) {
  //                       1         2
  //              1234567890123456789012345
  const auto result = render(R"(<div>Aあ😀A</div>)", latin_then_japanese(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 6}}, "fallback");
}

// 無名ブロック（インラインとブロックが混ざった子）と flex アイテムの中でも同じ。
TEST(Warnings, AnonymousBlocksAndFlexItemsKeepThePosition) {
  //                       1         2         3         4         5
  //              123456789012345678901234567890123456789012345678901234567890
  const auto result = render(R"(<div>😀<div>あ</div></div><div style="display:flex">)"
                             R"(<div>😃</div></div>)",
                             japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 6}, {U'\U0001F603', 1, 56}},
                  "anonymous / flex");
}

// 縦書きでも同じ（豆腐は立てる。位置の出どころは書字方向に依らない）。
TEST(Warnings, VerticalWritingModeReportsTheSamePosition) {
  RenderOptions options = options_for(320);
  options.viewport_height = 240;
  //                       1         2         3         4
  //              1234567890123456789012345678901234567890123456
  const auto result =
      render(R"(<div style="writing-mode: vertical-rl">あ😀</div>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  expect_warnings(result->warnings, {{U'\U0001F600', 1, 40}}, "vertical");
}

TEST(Warnings, NoneForOrdinaryJapanese) {
  const auto result = render(kSample, japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(result->warnings.empty());
}

// 受け入れ条件（issue #9）: --dump-stage box に豆腐の記録と断片の元位置が出る。
TEST(Warnings, BoxDumpCarriesTofuRecordsAndFragmentLocations) {
  const auto dumped =
      dump(R"(<div>あ😀</div>)", japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  ASSERT_TRUE(dumped.has_value()) << to_string(dumped.error());
  EXPECT_NE(dumped->find(R"("missing_glyphs")"), std::string::npos);
  EXPECT_NE(dumped->find(R"("codepoint": "U+1F600")"), std::string::npos);
  EXPECT_NE(dumped->find(R"("location": "1:6")"), std::string::npos);

  // 豆腐が無ければキーごと出ない
  const auto clean =
      dump(R"(<div>あ</div>)", japanese_fonts(), ImageSet{}, options_for(320), DumpStage::Box);
  ASSERT_TRUE(clean.has_value()) << to_string(clean.error());
  EXPECT_EQ(clean->find(R"("missing_glyphs")"), std::string::npos);
  EXPECT_NE(clean->find(R"("location": "1:6")"), std::string::npos);
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
