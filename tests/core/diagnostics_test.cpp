#include "core/diagnostics.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "shashoku/error.hpp"
#include "shashoku/source_location.hpp"
#include "shashoku/warning.hpp"

// 集める診断（ARCHITECTURE.md A46）。上限・整列・into_failure の 3 点が仕様。
namespace shashoku {
namespace {

SourceLocation at(std::uint32_t offset, std::uint32_t line = 1, std::uint32_t column = 1) {
  return SourceLocation{.offset = offset, .line = line, .column = column};
}

RenderError error_at(ErrorKind kind, std::string message,
                     std::optional<SourceLocation> location = std::nullopt) {
  return RenderError{.kind = kind,
                     .message = std::move(message),
                     .location = location,
                     .hint = {},
                     .warning = std::nullopt};
}

Warning warning_at(WarningKind kind, std::string detail, char32_t codepoint = 0,
                   std::optional<SourceLocation> location = std::nullopt) {
  return Warning{.kind = kind,
                 .detail = std::move(detail),
                 .codepoint = codepoint,
                 .location = location,
                 .overflow_px = 0.0F};
}

std::vector<std::string> messages(const std::vector<RenderError>& errors) {
  std::vector<std::string> out;
  out.reserve(errors.size());
  for (const RenderError& error : errors) {
    out.push_back(error.message);
  }
  return out;
}

std::vector<std::string> details(const std::vector<Warning>& warnings) {
  std::vector<std::string> out;
  out.reserve(warnings.size());
  for (const Warning& warning : warnings) {
    out.push_back(warning.detail);
  }
  return out;
}

// ---------------------------------------------------------------------------
// 上限（errors + warnings の合計）
// ---------------------------------------------------------------------------

TEST(Diagnostics, RecordsUpToTheLimitAndThenTruncates) {
  Diagnostics diagnostics{3};
  EXPECT_EQ(diagnostics.max_entries(), 3U);
  EXPECT_FALSE(diagnostics.truncated());
  EXPECT_FALSE(diagnostics.has_errors());

  EXPECT_TRUE(diagnostics.add_error(error_at(ErrorKind::CssParse, "a")));
  EXPECT_TRUE(diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "b")));
  EXPECT_TRUE(diagnostics.add_error(error_at(ErrorKind::CssParse, "c")));
  EXPECT_TRUE(diagnostics.has_errors());
  EXPECT_FALSE(diagnostics.truncated());

  // 4 件目は捨てられ、truncated が立つ（記録はやめるが解析は続けられる）。
  EXPECT_FALSE(diagnostics.add_error(error_at(ErrorKind::CssParse, "d")));
  EXPECT_FALSE(diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "e")));
  EXPECT_TRUE(diagnostics.truncated());
  EXPECT_EQ(diagnostics.errors().size(), 2U);
  EXPECT_EQ(diagnostics.warnings().size(), 1U);
}

// 上限 0 でも**最初の 1 件は記録する**（実効の下限は 1。limits.hpp / diagnostics.hpp）。
// 1 件も記録しないと、対応外の入力なのに `has_errors()` が false になり、api が「成功」として
// PNG を返してしまう（fail loudly の穴。DESIGN.md §3-6）。
TEST(Diagnostics, ZeroLimitStillRecordsTheFirstEntry) {
  Diagnostics diagnostics{0};
  EXPECT_TRUE(diagnostics.add_error(error_at(ErrorKind::CssParse, "a")));
  EXPECT_TRUE(diagnostics.has_errors());
  EXPECT_FALSE(diagnostics.truncated());

  // 2 件目からは捨てて truncated（上限 1 を渡したときと同じ振る舞い）。
  EXPECT_FALSE(diagnostics.add_error(error_at(ErrorKind::CssParse, "b")));
  EXPECT_FALSE(diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "c")));
  EXPECT_TRUE(diagnostics.truncated());
  EXPECT_EQ(diagnostics.errors().size(), 1U);
  EXPECT_TRUE(diagnostics.warnings().empty());
}

// 予算は errors と warnings で共通なので、最初の 1 件が警告のこともある。
TEST(Diagnostics, ZeroLimitFirstEntryCanBeAWarning) {
  Diagnostics diagnostics{0};
  EXPECT_TRUE(diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "tofu", U'A')));
  EXPECT_FALSE(diagnostics.truncated());
  EXPECT_FALSE(diagnostics.add_error(error_at(ErrorKind::CssParse, "a")));
  EXPECT_TRUE(diagnostics.truncated());
  EXPECT_EQ(diagnostics.warnings().size(), 1U);
  EXPECT_FALSE(diagnostics.has_errors());
}

// ---------------------------------------------------------------------------
// 整列（決定性。DESIGN.md §3-5）
// ---------------------------------------------------------------------------

// エラーは (location.offset, kind, message) の昇順。位置なしは末尾。
TEST(Diagnostics, ErrorsSortByOffsetThenKindThenMessage) {
  Diagnostics diagnostics{100};
  diagnostics.add_error(error_at(ErrorKind::NoFonts, "no location"));
  diagnostics.add_error(error_at(ErrorKind::UnsupportedValue, "at-20", at(20)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "at-10-b", at(10)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "at-10-a", at(10)));
  // 同じ位置なら kind（列挙の並び）で: CssParse < UnsupportedProperty
  diagnostics.add_error(error_at(ErrorKind::UnsupportedProperty, "at-10-property", at(10)));
  diagnostics.sort();

  EXPECT_EQ(
      messages(diagnostics.errors()),
      (std::vector<std::string>{"at-10-a", "at-10-b", "at-10-property", "at-20", "no location"}));
}

// 同じキーのものは足した順のまま（安定整列）。
TEST(Diagnostics, SortIsStableForEqualKeys) {
  Diagnostics diagnostics{100};
  diagnostics.add_error(error_at(ErrorKind::CssParse, "same", at(4)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "same", at(4)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "earlier", at(1)));
  diagnostics.sort();
  EXPECT_EQ(messages(diagnostics.errors()), (std::vector<std::string>{"earlier", "same", "same"}));
}

// 警告は (offset, kind, codepoint, detail) の昇順。位置なしは末尾。
TEST(Diagnostics, WarningsSortByOffsetThenKindThenCodepoint) {
  Diagnostics diagnostics{100};
  diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "no location", U'あ'));
  diagnostics.add_warning(warning_at(WarningKind::ContentOverflow, "overflow-at-5", 0, at(5)));
  diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "glyph-at-5-high", U'一', at(5)));
  diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "glyph-at-5-low", U'A', at(5)));
  diagnostics.sort();

  // 同じ位置では kind（MissingGlyph < ContentOverflow）→ コードポイントの昇順
  EXPECT_EQ(details(diagnostics.warnings()),
            (std::vector<std::string>{"glyph-at-5-low", "glyph-at-5-high", "overflow-at-5",
                                      "no location"}));
}

// ---------------------------------------------------------------------------
// into_failure
// ---------------------------------------------------------------------------

TEST(Diagnostics, IntoFailureSortsAndMovesTheDiagnostics) {
  Diagnostics diagnostics{100};
  diagnostics.add_error(error_at(ErrorKind::CssParse, "second", at(20)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "first", at(10)));
  diagnostics.add_warning(warning_at(WarningKind::MissingGlyph, "tofu", U'A', at(30)));

  // sort() を呼んでいなくても、into_failure() が同じ規則で整列する。
  const RenderFailure failure = std::move(diagnostics).into_failure();
  EXPECT_EQ(messages(failure.errors), (std::vector<std::string>{"first", "second"}));
  EXPECT_EQ(details(failure.warnings), (std::vector<std::string>{"tofu"}));
  EXPECT_FALSE(failure.truncated);
}

// extra（解析を止めた致命エラーなど）は、集めた列と合わせてから位置の昇順に並べ直す。
// 致命エラーは普通いちばん後ろの位置にあるので、先頭には来ない。
TEST(Diagnostics, IntoFailureSortsExtraTogetherWithTheCollectedOnes) {
  Diagnostics diagnostics{100};
  diagnostics.add_error(error_at(ErrorKind::CssParse, "collected-20", at(20)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "collected-10", at(10)));
  diagnostics.sort();

  const RenderFailure failure =
      std::move(diagnostics).into_failure({error_at(ErrorKind::HtmlParse, "fatal-15", at(15))});
  EXPECT_EQ(messages(failure.errors),
            (std::vector<std::string>{"collected-10", "fatal-15", "collected-20"}));
}

// 位置を持たない extra（NoFonts など）は末尾に来る。
TEST(Diagnostics, IntoFailurePutsAnExtraWithoutLocationLast) {
  Diagnostics diagnostics{100};
  diagnostics.add_error(error_at(ErrorKind::CssParse, "collected-10", at(10)));
  diagnostics.sort();

  const RenderFailure failure =
      std::move(diagnostics).into_failure({error_at(ErrorKind::NoFonts, "FontSet is empty")});
  EXPECT_EQ(messages(failure.errors),
            (std::vector<std::string>{"collected-10", "FontSet is empty"}));
}

// 何も集めていないときは「その 1 件だけ」の失敗になる（いまの api の経路）。
TEST(Diagnostics, IntoFailureWithOnlyExtraGivesASingleError) {
  Diagnostics diagnostics{100};
  diagnostics.sort();
  const RenderFailure failure =
      std::move(diagnostics).into_failure({error_at(ErrorKind::NoFonts, "FontSet is empty")});
  ASSERT_EQ(failure.errors.size(), 1U);
  EXPECT_EQ(failure.errors.front().kind, ErrorKind::NoFonts);
  EXPECT_TRUE(failure.warnings.empty());
  EXPECT_FALSE(failure.truncated);
}

TEST(Diagnostics, IntoFailureCarriesTruncated) {
  Diagnostics diagnostics{1};
  diagnostics.add_error(error_at(ErrorKind::CssParse, "kept", at(1)));
  diagnostics.add_error(error_at(ErrorKind::CssParse, "dropped", at(2)));
  diagnostics.sort();

  const RenderFailure failure = std::move(diagnostics).into_failure();
  EXPECT_EQ(messages(failure.errors), (std::vector<std::string>{"kept"}));
  EXPECT_TRUE(failure.truncated);
}

}  // namespace
}  // namespace shashoku
