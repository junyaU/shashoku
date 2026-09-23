#include "shashoku/error.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "shashoku/warning.hpp"

namespace shashoku {
namespace {

// ErrorKind の全値 → ケバブケース。新しい値を足したらここにも足すこと。
TEST(ErrorKindToString, CoversEveryKind) {
  constexpr std::array<std::pair<ErrorKind, std::string_view>, 17> kCases{{
      {ErrorKind::InvalidUtf8, "invalid-utf8"},
      {ErrorKind::HtmlParse, "html-parse"},
      {ErrorKind::UnsupportedTag, "unsupported-tag"},
      {ErrorKind::UnsupportedAttribute, "unsupported-attribute"},
      {ErrorKind::CssParse, "css-parse"},
      {ErrorKind::UnsupportedProperty, "unsupported-property"},
      {ErrorKind::UnsupportedValue, "unsupported-value"},
      {ErrorKind::UnsupportedLayout, "unsupported-layout"},
      {ErrorKind::FontLoad, "font-load"},
      {ErrorKind::NoFonts, "no-fonts"},
      {ErrorKind::ImageDecode, "image-decode"},
      {ErrorKind::ImageNotFound, "image-not-found"},
      {ErrorKind::InvalidOption, "invalid-option"},
      {ErrorKind::LimitExceeded, "limit-exceeded"},
      {ErrorKind::OutOfMemory, "out-of-memory"},
      {ErrorKind::Internal, "internal"},
      {ErrorKind::WarningAsError, "warning-as-error"},
  }};

  for (const auto& [kind, expected] : kCases) {
    EXPECT_EQ(to_string(kind), expected);
  }
}

TEST(ErrorKindToString, IsKebabCase) {
  for (const std::string_view name :
       {to_string(ErrorKind::UnsupportedProperty), to_string(ErrorKind::ImageNotFound),
        to_string(ErrorKind::InvalidUtf8)}) {
    EXPECT_EQ(name.find('_'), std::string_view::npos);
    EXPECT_EQ(name.find(' '), std::string_view::npos);
    for (const char c : name) {
      EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') << name;
    }
  }
}

TEST(ErrorToString, WithLocation) {
  const RenderError error{.kind = ErrorKind::UnsupportedProperty,
                          .message = "`float` is not supported",
                          .location = SourceLocation{.offset = 42, .line = 3, .column = 14},
                          .hint = {},
                          .warning = std::nullopt};
  EXPECT_EQ(to_string(error), "error[unsupported-property] at 3:14: `float` is not supported");
}

// location がなければ " at L:C" ごと省く
TEST(ErrorToString, WithoutLocation) {
  const RenderError error{.kind = ErrorKind::NoFonts,
                          .message = "FontSet is empty",
                          .location = std::nullopt,
                          .hint = {},
                          .warning = std::nullopt};
  EXPECT_EQ(to_string(error), "error[no-fonts]: FontSet is empty");
}

TEST(ErrorToString, EmptyMessageStillHasSeparator) {
  const RenderError error{.kind = ErrorKind::Internal,
                          .message = "",
                          .location = std::nullopt,
                          .hint = {},
                          .warning = std::nullopt};
  EXPECT_EQ(to_string(error), "error[internal]: ");
}

TEST(Fail, BuildsUnexpectedError) {
  const Result<int> result = fail(ErrorKind::UnsupportedTag, "<marquee> is not supported",
                                  SourceLocation{.offset = 7, .line = 1, .column = 8});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, ErrorKind::UnsupportedTag);
  EXPECT_EQ(result.error().message, "<marquee> is not supported");
  EXPECT_EQ(result.error().location,
            std::make_optional(SourceLocation{.offset = 7, .line = 1, .column = 8}));
  EXPECT_EQ(to_string(result.error()), "error[unsupported-tag] at 1:8: <marquee> is not supported");
}

TEST(Fail, LocationIsOptional) {
  const Result<int> result = fail(ErrorKind::Internal, "boom");
  ASSERT_FALSE(result.has_value());
  EXPECT_FALSE(result.error().location.has_value());
}

// ---------------------------------------------------------------------------
// RenderFailure（A46）: 1 行 1 件、hint は続く行、警告と打ち切りも出す
// ---------------------------------------------------------------------------

RenderError make_error(ErrorKind kind, std::string message,
                       std::optional<SourceLocation> location = std::nullopt,
                       std::string hint = {}) {
  return RenderError{.kind = kind,
                     .message = std::move(message),
                     .location = location,
                     .hint = std::move(hint),
                     .warning = std::nullopt};
}

Warning make_warning(WarningKind kind, std::string detail, char32_t codepoint = 0,
                     std::optional<SourceLocation> location = std::nullopt,
                     float overflow_px = 0.0F) {
  return Warning{.kind = kind,
                 .detail = std::move(detail),
                 .codepoint = codepoint,
                 .location = location,
                 .overflow_px = overflow_px};
}

// 1 件だけ・hint なしなら、to_string(RenderError) と同じ 1 行（CLI の出力が変わらない）。
TEST(FailureToString, SingleErrorMatchesTheSingleLineForm) {
  const RenderError error = make_error(ErrorKind::UnsupportedProperty, "`float` is not supported",
                                       SourceLocation{.offset = 5, .line = 1, .column = 6});
  const RenderFailure failure{.errors = {error}, .warnings = {}, .truncated = false};
  EXPECT_EQ(to_string(failure), to_string(error));
  EXPECT_FALSE(to_string(failure).ends_with("\n"));
}

TEST(FailureToString, HintGoesOnItsOwnLine) {
  const RenderFailure failure{
      .errors = {make_error(ErrorKind::UnsupportedProperty, "`box-sizing` is not supported",
                            SourceLocation{.offset = 0, .line = 2, .column = 3},
                            "content-box only")},
      .warnings = {},
      .truncated = false};
  EXPECT_EQ(to_string(failure),
            "error[unsupported-property] at 2:3: `box-sizing` is not supported\n"
            "  hint: content-box only");
}

TEST(FailureToString, ErrorsThenWarningsThenTruncation) {
  const RenderFailure failure{
      .errors = {make_error(ErrorKind::CssParse, "bad declaration",
                            SourceLocation{.offset = 1, .line = 1, .column = 2}),
                 make_error(ErrorKind::NoFonts, "FontSet is empty")},
      .warnings = {make_warning(WarningKind::MissingGlyph, "no font has a glyph for U+1F600 at 3:4",
                                U'\U0001F600'),
                   make_warning(WarningKind::ContentOverflow,
                                "content overflows the canvas by 42.5px (bottom) at 5:1", 0,
                                std::nullopt, 42.5F)},
      .truncated = true};
  EXPECT_EQ(to_string(failure),
            "error[css-parse] at 1:2: bad declaration\n"
            "error[no-fonts]: FontSet is empty\n"
            "warning[missing-glyph]: no font has a glyph for U+1F600 at 3:4\n"
            "warning[content-overflow]: content overflows the canvas by 42.5px (bottom) at 5:1\n"
            "(diagnostics truncated at 4)");
}

TEST(WarningKindToString, CoversEveryKind) {
  EXPECT_EQ(to_string(WarningKind::MissingGlyph), "missing-glyph");
  EXPECT_EQ(to_string(WarningKind::ContentOverflow), "content-overflow");
}

}  // namespace
}  // namespace shashoku
