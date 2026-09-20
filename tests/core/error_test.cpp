#include "shashoku/error.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "core/result.hpp"

namespace shashoku {
namespace {

// ErrorKind の全値 → ケバブケース。新しい値を足したらここにも足すこと。
TEST(ErrorKindToString, CoversEveryKind) {
  constexpr std::array<std::pair<ErrorKind, std::string_view>, 14> kCases{{
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
      {ErrorKind::Internal, "internal"},
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
                          .location = SourceLocation{.offset = 42, .line = 3, .column = 14}};
  EXPECT_EQ(to_string(error), "error[unsupported-property] at 3:14: `float` is not supported");
}

// location がなければ " at L:C" ごと省く
TEST(ErrorToString, WithoutLocation) {
  const RenderError error{
      .kind = ErrorKind::NoFonts, .message = "FontSet is empty", .location = std::nullopt};
  EXPECT_EQ(to_string(error), "error[no-fonts]: FontSet is empty");
}

TEST(ErrorToString, EmptyMessageStillHasSeparator) {
  const RenderError error{.kind = ErrorKind::Internal, .message = "", .location = std::nullopt};
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

}  // namespace
}  // namespace shashoku
