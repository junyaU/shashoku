#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"
#include "support/failure.hpp"

// A46「一度に全部・直し方つき・結果にも」を公開 API から見たときの姿
// （ARCHITECTURE.md §3.10「診断の組み立て」）。
//
// 検査するのは**識別子（kind / warning / edge）と入力位置**で、message / detail の文面は
// 人向けなので見ない（hint は「有無」と「何を勧めるか」だけを見る）。
namespace shashoku::test {
namespace {

// ① html と ② style の対応外が混ざった入力。それぞれの段が別の問題を見つける。
constexpr std::string_view kMixedHtml =
    "<div style=\"float: left\">あ</div>\n"
    "<table><span style=\"max-width: 200px\">い</span></table>\n"
    "<div onclick=\"x\" style=\"position: absolute\">う</div>\n";

// 位置の取り出しはここに 1 か所だけ置く。①② の診断は必ず位置を持つ（持たなければ契約違反）が、
// テストの中で毎回 `location->` と書くと「確かめずに optional を開けている」ことになる。
SourceLocation location_of(const RenderError& error) {
  if (!error.location) {
    ADD_FAILURE() << "位置の無いエラー: " << to_string(error);
    return SourceLocation{};
  }
  return *error.location;
}

SourceLocation location_of(const Warning& warning) {
  if (!warning.location) {
    ADD_FAILURE() << "位置の無い警告: " << warning.detail;
    return SourceLocation{};
  }
  return *warning.location;
}

// 格上げされたエラーが保っている元の警告の種類（WarningAsError のときだけ入っている）。
WarningKind promoted_kind(const RenderError& error) {
  if (!error.warning) {
    ADD_FAILURE() << "格上げしたエラーに元の警告の種類がない: " << to_string(error);
    return WarningKind::MissingGlyph;
  }
  return *error.warning;
}

// text の中に needle が何回出るか（格上げした診断で位置が二重に出ていないことの検査）。
std::size_t count_occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t pos = text.find(needle); pos != std::string_view::npos;
       pos = text.find(needle, pos + needle.size())) {
    ++count;
  }
  return count;
}

std::vector<ErrorKind> kinds_of(const RenderFailure& failure) {
  std::vector<ErrorKind> kinds;
  kinds.reserve(failure.errors.size());
  for (const RenderError& error : failure.errors) {
    kinds.push_back(error.kind);
  }
  return kinds;
}

// ---------------------------------------------------------------------------
// 一度に全部（A46 の 1）
// ---------------------------------------------------------------------------

// 1 回の render() で ①② の問題が全部返り、並びは入力位置の昇順（RenderFailure の契約）。
// A46 より前は最初の 1 件で止まっていたので、5 件を直すのに CLI を 5 回走らせていた。
TEST(Diagnostics, CollectsHtmlAndStyleProblemsAtOnce) {
  const auto result = render(kMixedHtml, japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  const RenderFailure& failure = result.error();
  EXPECT_FALSE(failure.truncated);
  EXPECT_TRUE(failure.warnings.empty());

  EXPECT_EQ(kinds_of(failure),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedProperty, ErrorKind::UnsupportedTag,
                                    ErrorKind::UnsupportedProperty, ErrorKind::UnsupportedAttribute,
                                    ErrorKind::UnsupportedProperty}))
      << to_string(failure);

  // 入力位置の昇順（同じ入力からは同じ並び。DESIGN.md §3-5）
  for (std::size_t i = 1; i < failure.errors.size(); ++i) {
    EXPECT_LT(location_of(failure.errors[i - 1]).offset, location_of(failure.errors[i]).offset)
        << to_string(failure);
  }
  // 行も入力どおり（1 行目 → 2 行目 → 3 行目）
  EXPECT_EQ(location_of(failure.errors.front()).line, 1U);
  EXPECT_EQ(location_of(failure.errors.back()).line, 3U);
}

// 直し方は `RenderError::hint` に分けて入る（message には混ぜない。A46 の 6 / A48）。
// 機械側は kind と hint を別々に読めるし、人向けの 1 行は to_string() が作る。
TEST(Diagnostics, HintIsSeparateFromTheMessage) {
  const auto result =
      render(R"(<div style="float: left">あ</div>)", japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  const RenderError error = first_error(result.error());
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedProperty);
  EXPECT_FALSE(error.hint.empty()) << to_string(result.error());
  // 確かめた代替（flex で横並びにする）を勧める。message 側には入れない
  EXPECT_NE(error.hint.find("display: flex"), std::string::npos) << error.hint;
  EXPECT_EQ(error.message.find("display: flex"), std::string::npos) << error.message;
  // 人向けの 1 行は "  hint: …" の行として続く（error.hpp の契約）
  EXPECT_NE(to_string(result.error()).find("\n  hint: "), std::string::npos)
      << to_string(result.error());
}

// hint の無い対応外（対応表に確かめた代替が無いもの）は空のまま。
TEST(Diagnostics, HintIsEmptyWhenThereIsNoVerifiedAlternative) {
  const auto result = render("<table>あ</table>", japanese_fonts(), options_for(320));
  ASSERT_FALSE(result.has_value());
  const RenderError error = first_error(result.error());
  EXPECT_EQ(error.kind, ErrorKind::UnsupportedTag);
  EXPECT_TRUE(error.hint.empty()) << error.hint;
}

// ---------------------------------------------------------------------------
// 上限（RenderLimits::max_diagnostics）
// ---------------------------------------------------------------------------

// 上限に達したら記録をやめて truncated を立てる（解析は続ける）。件数は上限ちょうど。
TEST(Diagnostics, StopsRecordingAtMaxDiagnostics) {
  RenderOptions options = options_for(320);
  options.limits.max_diagnostics = 2;
  const auto result = render(kMixedHtml, japanese_fonts(), options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().errors.size(), 2U) << to_string(result.error());
  EXPECT_TRUE(result.error().truncated);
  // 残るのは**先に記録された** 2 件。段の順に予算を使うので、上限が小さいと ① html の
  // 分で埋まり、② style の分は落ちる（記録をやめるだけで、並べ替えの規則は変わらない）。
  EXPECT_EQ(kinds_of(result.error()),
            (std::vector<ErrorKind>{ErrorKind::UnsupportedTag, ErrorKind::UnsupportedAttribute}))
      << to_string(result.error());
  EXPECT_EQ(location_of(result.error().errors[0]).line, 2U) << to_string(result.error());
  EXPECT_EQ(location_of(result.error().errors[1]).line, 3U) << to_string(result.error());
}

// 警告にも同じ上限が掛かり、打ち切ったことは RenderResult にも出る。
TEST(Diagnostics, WarningsShareTheSameBudget) {
  RenderOptions options = options_for(320);
  options.limits.max_diagnostics = 1;
  const auto result = render(R"(<div>😀</div><p>😃</p>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->warnings.size(), 1U);
  EXPECT_TRUE(result->diagnostics_truncated);
}

// 上限 0 でも**最初の 1 件は必ず記録する**（実効の下限は 1。limits.hpp）。
// 0 件だと対応外の入力が診断なしで「成功」してしまう（fail loudly の穴）。
TEST(Diagnostics, ZeroMaxDiagnosticsStillFails) {
  RenderOptions options = options_for(320);
  options.limits.max_diagnostics = 0;
  const auto result = render(kMixedHtml, japanese_fonts(), options);
  ASSERT_FALSE(result.has_value()) << "対応外の入力なのに成功した（診断が 0 件で握り潰された）";
  ASSERT_EQ(result.error().errors.size(), 1U) << to_string(result.error());
  EXPECT_EQ(result.error().errors[0].kind, ErrorKind::UnsupportedTag) << to_string(result.error());
  EXPECT_TRUE(result.error().truncated);
}

// 上限に達していなければ立たない（既定の 100 件では普通の入力で立たない）。
TEST(Diagnostics, NotTruncatedByDefault) {
  const auto result = render(R"(<div>😀</div>)", japanese_fonts(), options_for(320));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(result->warnings.size(), 1U);
  EXPECT_FALSE(result->diagnostics_truncated);
}

// ---------------------------------------------------------------------------
// 結果にも（A46 の 3）: 紙面からのはみ出し
// ---------------------------------------------------------------------------

// 固定した高さより背の高い中身は ContentOverflow の警告になり、描画は続行する。
// A46 より前は警告なし・exit 0 で切れた PNG が出ていた。
TEST(Diagnostics, ContentOverflowIsWarnedWithPixelsAndEdge) {
  RenderOptions options = options_for(200);
  options.viewport_height = 100;
  const auto result =
      render(R"(<div style="width: 100px; height: 400px">あ</div>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 1U);
  const Warning& warning = result->warnings[0];
  EXPECT_EQ(warning.kind, WarningKind::ContentOverflow);
  EXPECT_EQ(to_string(warning.kind), "content-overflow");
  EXPECT_EQ(warning.codepoint, 0U);
  // 単位は CSS px（scale を掛ける前。A50）: 400 - 100
  EXPECT_FLOAT_EQ(warning.overflow_px, 300.0F);
  EXPECT_EQ(warning.overflow_edge, OverflowEdge::Bottom);
  EXPECT_EQ(to_string(warning.overflow_edge), "bottom");
  EXPECT_EQ(location_of(warning).line, 1U);
  EXPECT_EQ(location_of(warning).column, 1U);
  EXPECT_FALSE(result->png.empty());  // 警告であって失敗ではない
  EXPECT_EQ(result->height, 100);
}

// 横にはみ出せば辺は right。直し方が辺で変わるので、機械が分けて読めること（A50）。
TEST(Diagnostics, ContentOverflowReportsTheHorizontalEdge) {
  RenderOptions options = options_for(200);
  options.viewport_height = 400;
  const auto result =
      render(R"(<div style="width: 500px; height: 50px">あ</div>)", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 1U);
  EXPECT_EQ(result->warnings[0].kind, WarningKind::ContentOverflow);
  EXPECT_FLOAT_EQ(result->warnings[0].overflow_px, 300.0F);  // 500 - 200
  EXPECT_EQ(result->warnings[0].overflow_edge, OverflowEdge::Right);
}

// 高さを省く（内容追従）と縦にははみ出しようがないので、同じ入力でも警告は出ない。
TEST(Diagnostics, NoOverflowWarningWhenTheHeightFollowsTheContent) {
  const auto result = render(R"(<div style="width: 100px; height: 400px">あ</div>)",
                             japanese_fonts(), options_for(200));
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(result->warnings.empty()) << result->warnings.size();
  EXPECT_EQ(result->height, 400);
}

// 豆腐とはみ出しが混ざっても、並びは (位置, 種類, コードポイント, detail) の昇順で決まる。
TEST(Diagnostics, TofuAndOverflowAreSortedByPosition) {
  RenderOptions options = options_for(200);
  options.viewport_height = 60;
  const auto result =
      render("<div>😀</div>\n<div style=\"height: 300px\">あ</div>", japanese_fonts(), options);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  ASSERT_EQ(result->warnings.size(), 2U);
  EXPECT_EQ(result->warnings[0].kind, WarningKind::MissingGlyph);
  EXPECT_EQ(result->warnings[1].kind, WarningKind::ContentOverflow);
  EXPECT_LT(location_of(result->warnings[0]).offset, location_of(result->warnings[1]).offset);
}

// ---------------------------------------------------------------------------
// 格上げ（A46 の 4）: warnings_as_errors
// ---------------------------------------------------------------------------

RenderOptions strict_options(int width) {
  RenderOptions options = options_for(width);
  options.warnings_as_errors = true;
  return options;
}

// 豆腐を格上げすると PNG は返らない。識別子・位置・詳細は失わない。
TEST(Diagnostics, StrictPromotesMissingGlyph) {
  const auto lenient = render(R"(<div>ABC😀</div>)", latin_then_japanese(), options_for(320));
  ASSERT_TRUE(lenient.has_value()) << to_string(lenient.error());
  ASSERT_EQ(lenient->warnings.size(), 1U);

  const auto result = render(R"(<div>ABC😀</div>)", latin_then_japanese(), strict_options(320));
  ASSERT_FALSE(result.has_value()) << "strict なのに成功した";
  const RenderFailure& failure = result.error();
  ASSERT_EQ(failure.errors.size(), 1U) << to_string(failure);
  EXPECT_EQ(failure.errors[0].kind, ErrorKind::WarningAsError);
  EXPECT_EQ(to_string(failure.errors[0].kind), "warning-as-error");
  // 元の警告の種類・位置をそのまま持つ。message は警告の detail から末尾の位置を除いたもの
  // （位置は location にあり、to_string が付け直す。error.hpp の契約）。
  EXPECT_EQ(promoted_kind(failure.errors[0]), WarningKind::MissingGlyph);
  EXPECT_EQ(failure.errors[0].location, lenient->warnings[0].location);
  EXPECT_TRUE(lenient->warnings[0].detail.starts_with(failure.errors[0].message))
      << lenient->warnings[0].detail << " / " << failure.errors[0].message;
  // 格上げしたものは errors 側にだけ残す
  EXPECT_TRUE(failure.warnings.empty());
  EXPECT_FALSE(failure.truncated);
}

// はみ出しも同じ経路で格上げされる（サーバーで「切れた画像は配らない」判断に使う）。
TEST(Diagnostics, StrictPromotesContentOverflow) {
  RenderOptions options = strict_options(200);
  options.viewport_height = 100;
  const auto result =
      render(R"(<div style="width: 100px; height: 400px">あ</div>)", japanese_fonts(), options);
  ASSERT_FALSE(result.has_value()) << "strict なのに成功した";
  ASSERT_EQ(result.error().errors.size(), 1U) << to_string(result.error());
  EXPECT_EQ(result.error().errors[0].kind, ErrorKind::WarningAsError);
  EXPECT_EQ(promoted_kind(result.error().errors[0]), WarningKind::ContentOverflow);
  EXPECT_TRUE(result.error().errors[0].hint.empty());
}

// 格上げした message に位置を書かない（error.hpp の契約）。位置は `location` にあり、
// `to_string(RenderError)` が " at L:C" を 1 回だけ付ける。
// 直す前は "error[warning-as-error] at 1:4: no font has a glyph for U+1F525 at 1:4" と
// 位置が二重に出ていた（--diagnostics json の message も同じ）。
TEST(Diagnostics, PromotedMessageDoesNotRepeatTheLocation) {
  const auto tofu = render(R"(<div>ABC😀</div>)", latin_then_japanese(), strict_options(320));
  ASSERT_FALSE(tofu.has_value()) << "strict なのに成功した";
  ASSERT_EQ(tofu.error().errors.size(), 1U) << to_string(tofu.error());
  EXPECT_EQ(count_occurrences(tofu.error().errors[0].message, " at "), 0U)
      << tofu.error().errors[0].message;
  EXPECT_EQ(count_occurrences(to_string(tofu.error().errors[0]), " at "), 1U)
      << to_string(tofu.error().errors[0]);

  RenderOptions options = strict_options(200);
  options.viewport_height = 100;
  const auto overflow =
      render(R"(<div style="width: 100px; height: 400px">あ</div>)", japanese_fonts(), options);
  ASSERT_FALSE(overflow.has_value()) << "strict なのに成功した";
  ASSERT_EQ(overflow.error().errors.size(), 1U) << to_string(overflow.error());
  EXPECT_EQ(count_occurrences(overflow.error().errors[0].message, " at "), 0U)
      << overflow.error().errors[0].message;
  EXPECT_EQ(count_occurrences(to_string(overflow.error().errors[0]), " at "), 1U)
      << to_string(overflow.error().errors[0]);
}

// 警告が無ければ strict でも今までどおり成功する（既定と同じ PNG が出る）。
TEST(Diagnostics, StrictSucceedsWithoutWarnings) {
  const auto lenient = render("<div>あ</div>", japanese_fonts(), options_for(320));
  ASSERT_TRUE(lenient.has_value()) << to_string(lenient.error());
  const auto strict = render("<div>あ</div>", japanese_fonts(), strict_options(320));
  ASSERT_TRUE(strict.has_value()) << to_string(strict.error());
  EXPECT_EQ(strict->png, lenient->png);  // オプションは絵を変えない
}

// 対応外のエラーは strict の有無で変わらない（格上げは ③ 以降の警告だけの話）。
TEST(Diagnostics, StrictDoesNotChangeStageErrors) {
  const auto result = render(kMixedHtml, japanese_fonts(), strict_options(320));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().errors.size(), 5U) << to_string(result.error());
  for (const RenderError& error : result.error().errors) {
    EXPECT_NE(error.kind, ErrorKind::WarningAsError);
  }
}

// dump() も Box 以降の段では同じ組み立てになる（§3.10）。
TEST(Diagnostics, StrictAppliesToDumpAfterLayout) {
  RenderOptions options = strict_options(200);
  options.viewport_height = 100;
  constexpr std::string_view kHtml = R"(<div style="width: 100px; height: 400px">あ</div>)";

  const auto box = dump(kHtml, japanese_fonts(), ImageSet{}, options, DumpStage::Box);
  ASSERT_FALSE(box.has_value()) << "strict なのに dump が成功した";
  ASSERT_EQ(box.error().errors.size(), 1U) << to_string(box.error());
  EXPECT_EQ(box.error().errors[0].kind, ErrorKind::WarningAsError);

  // ② までの段は layout に入らないので、はみ出しは分からない = 成功のまま
  const auto styled = dump(kHtml, japanese_fonts(), ImageSet{}, options, DumpStage::Style);
  EXPECT_TRUE(styled.has_value()) << to_string(styled.error());

  // strict でなければ Box も今までどおり出る
  RenderOptions lenient = options;
  lenient.warnings_as_errors = false;
  const auto ok = dump(kHtml, japanese_fonts(), ImageSet{}, lenient, DumpStage::Box);
  EXPECT_TRUE(ok.has_value()) << to_string(ok.error());
}

}  // namespace
}  // namespace shashoku::test
