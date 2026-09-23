#include "html/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "core/diagnostics.hpp"
#include "core/result.hpp"
#include "html/dom.hpp"
#include "shashoku/error.hpp"
#include "shashoku/limits.hpp"

namespace shashoku::html {
namespace {

using ::testing::HasSubstr;

Node parsed(std::string_view source) {
  Diagnostics diagnostics{RenderLimits{}.max_diagnostics};
  Result<Node> result = parse(source, diagnostics);
  if (!result) {
    ADD_FAILURE() << "parse failed: " << to_string(result.error());
    return Node{};
  }
  return std::move(*result);
}

Error parse_failure(std::string_view source) {
  Diagnostics diagnostics{RenderLimits{}.max_diagnostics};
  Result<Node> result = parse(source, diagnostics);
  if (result) {
    ADD_FAILURE() << "parse unexpectedly succeeded: " << dump_json(*result);
    return Error{};
  }
  return std::move(result).error();
}

Node element(std::string tag, std::vector<Node> children = {}, std::vector<Attribute> attrs = {}) {
  Node node;
  node.type = Node::Type::Element;
  node.tag = std::move(tag);
  node.attrs = std::move(attrs);
  node.children = std::move(children);
  return node;
}

Node text(std::string value) {
  Node node;
  node.type = Node::Type::Text;
  node.text = std::move(value);
  return node;
}

// 子ノードを move で集める。std::initializer_list<Node> は要素をコピーするので使わない
// （Node の再帰的なコピーコンストラクタを作らないため）。
template <class... Nodes>
std::vector<Node> nodes(Nodes&&... items) {
  std::vector<Node> out;
  out.reserve(sizeof...(items));
  (out.push_back(std::forward<Nodes>(items)), ...);
  return out;
}

// 位置を見ない比較のために、木全体の SourceLocation を既定値に揃える（再帰しない）。
void clear_locations(Node& root) {
  std::vector<Node*> stack{&root};
  while (!stack.empty()) {
    Node* node = stack.back();
    stack.pop_back();
    node->location = SourceLocation{};
    for (Attribute& attr : node->attrs) {
      attr.location = SourceLocation{};
    }
    for (Node& child : node->children) {
      stack.push_back(&child);
    }
  }
}

// 位置を無視して木の形だけを比べる。差分を読めるようにするため JSON に落として比較する
// （期待値の木は位置が既定値のままなので、実際の木の位置も揃えてから比べる）。
void expect_shape(std::string_view source, std::vector<Node> children) {
  Node actual = parsed(source);
  clear_locations(actual);
  const Node expected = element("#root", std::move(children));
  EXPECT_EQ(dump_json(actual), dump_json(expected));
}

Attribute attribute(std::string name, std::string value) {
  return Attribute{.name = std::move(name), .value = std::move(value), .location = {}};
}

// ---- 正常系: 構造 ---------------------------------------------------------

TEST(HtmlParser, EmptyInputGivesChildlessRoot) {
  const Node root = parsed("");
  EXPECT_EQ(root.type, Node::Type::Element);
  EXPECT_EQ(root.tag, "#root");
  EXPECT_TRUE(root.attrs.empty());
  EXPECT_TRUE(root.children.empty());
  EXPECT_EQ(root.location, (SourceLocation{.offset = 0, .line = 1, .column = 1}));
}

TEST(HtmlParser, SingleElement) { expect_shape("<div></div>", nodes(element("div"))); }

TEST(HtmlParser, TextOnly) { expect_shape("hello", nodes(text("hello"))); }

TEST(HtmlParser, NestedElements) {
  expect_shape("<div><span>hi</span></div>",
               nodes(element("div", nodes(element("span", nodes(text("hi")))))));
}

TEST(HtmlParser, MultipleTopLevelNodes) {
  expect_shape("a<p>b</p><p>c</p>d", nodes(text("a"), element("p", nodes(text("b"))),
                                           element("p", nodes(text("c"))), text("d")));
}

TEST(HtmlParser, AllSupportedTags) {
  const Node root = parsed(
      "<div><p><h1></h1><h2></h2><h3></h3><h4></h4><h5></h5><h6></h6></p>"
      "<span><ruby><rp>(</rp><rt>a</rt><rp>)</rp></ruby></span>"
      "<br><img src=\"x\"><style>p{}</style></div>");
  ASSERT_EQ(root.children.size(), 1U);
  const Node& div = root.children.front();
  ASSERT_EQ(div.children.size(), 5U);
  EXPECT_EQ(div.children[0].tag, "p");
  EXPECT_EQ(div.children[1].tag, "span");
  EXPECT_EQ(div.children[2].tag, "br");
  EXPECT_EQ(div.children[3].tag, "img");
  EXPECT_EQ(div.children[4].tag, "style");
  EXPECT_EQ(div.children[0].children.size(), 6U);  // h1..h6
}

TEST(HtmlParser, RubyInternalStructureIsNotValidated) {
  // rt の位置の検証はレイアウトの仕事（ARCHITECTURE.md §3.6）。入れ子が正しければ通す。
  expect_shape("<ruby><rt>a</rt>漢</ruby>",
               nodes(element("ruby", nodes(element("rt", nodes(text("a"))), text("漢")))));
}

// ---- 正常系: タグと属性 ---------------------------------------------------

TEST(HtmlParser, TagAndAttributeNamesAreLowercased) {
  expect_shape("<DIV CLASS=\"A\">X</DiV>",
               nodes(element("div", nodes(text("X")), {attribute("class", "A")})));
}

TEST(HtmlParser, ThreeAttributeQuotingForms) {
  expect_shape("<div id=\"a b\" class='c' style=color:red></div>",
               nodes(element("div", {},
                             {attribute("id", "a b"), attribute("class", "c"),
                              attribute("style", "color:red")})));
}

TEST(HtmlParser, ValuelessAttributeIsEmptyString) {
  expect_shape("<img alt>", nodes(element("img", {}, {attribute("alt", "")})));
}

TEST(HtmlParser, WhitespaceAroundEqualsIsAllowed) {
  expect_shape("<div id = \"a\" class></div>",
               nodes(element("div", {}, {attribute("id", "a"), attribute("class", "")})));
}

TEST(HtmlParser, ImgSpecificAttributes) {
  expect_shape(
      "<img src='a.png' width=\"10\" height=20 alt=\"絵\" id=\"i\" class=\"c\">",
      nodes(element("img", {},
                    {attribute("src", "a.png"), attribute("width", "10"), attribute("height", "20"),
                     attribute("alt", "絵"), attribute("id", "i"), attribute("class", "c")})));
}

TEST(HtmlParser, AttributeValuesResolveCharacterReferences) {
  expect_shape("<div id=\"a&amp;b\" class='&lt;&#65;&gt;'></div>",
               nodes(element("div", {}, {attribute("id", "a&b"), attribute("class", "<A>")})));
}

TEST(HtmlParser, VoidElementsHaveNoEndTag) {
  expect_shape("<br><br/><img src=\"a\"/>",
               nodes(element("br"), element("br"), element("img", {}, {attribute("src", "a")})));
}

TEST(HtmlParser, FindAttrOnParsedElement) {
  const Node root = parsed("<div class=\"note\"></div>");
  ASSERT_EQ(root.children.size(), 1U);
  const Attribute* found = root.children.front().find_attr("class");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->value, "note");
  EXPECT_EQ(root.children.front().find_attr("id"), nullptr);
}

// ---- 正常系: テキスト -----------------------------------------------------

TEST(HtmlParser, WhitespaceIsPreservedIncludingWhitespaceOnlyNodes) {
  expect_shape("<div>  \n </div>", nodes(element("div", nodes(text("  \n ")))));
  expect_shape(" \t ", nodes(text(" \t ")));
}

TEST(HtmlParser, AdjacentTextIsMergedIntoOneNode) {
  expect_shape("a&amp;b&#99;", nodes(text("a&bc")));
}

TEST(HtmlParser, TextSeparatedByCommentIsMerged) {
  expect_shape("a<!-- note -->b", nodes(text("ab")));
}

TEST(HtmlParser, JapaneseText) {
  expect_shape("<p>日本語の文章です。</p>", nodes(element("p", nodes(text("日本語の文章です。")))));
}

TEST(HtmlParser, BareAmpersandBeforeWhitespaceOrEndOfInput) {
  expect_shape("A & B", nodes(text("A & B")));
  expect_shape("A &", nodes(text("A &")));
  expect_shape("A &\nB", nodes(text("A &\nB")));
}

// ---- 正常系: コメント / DOCTYPE / style -----------------------------------

TEST(HtmlParser, CommentsAndDoctypeAreSkipped) {
  expect_shape("<!DOCTYPE html><!-- c --><div><!--x--></div><!---->", nodes(element("div")));
}

TEST(HtmlParser, CommentContainingMarkupIsSkipped) {
  expect_shape("<div><!-- <span> & </span> --></div>", nodes(element("div")));
}

TEST(HtmlParser, StyleContentIsRawText) {
  expect_shape("<style>p::before{content:\"<&amp;>\"}</style>",
               nodes(element("style", nodes(text("p::before{content:\"<&amp;>\"}")))));
}

TEST(HtmlParser, EmptyStyleHasNoChildren) {
  expect_shape("<style></style>", nodes(element("style")));
}

TEST(HtmlParser, StyleEndTagIsCaseInsensitiveAndAllowsWhitespace) {
  expect_shape("<STYLE>a{}</STYLE >", nodes(element("style", nodes(text("a{}")))));
}

TEST(HtmlParser, StyleContentMayContainSimilarEndTag) {
  expect_shape("<style>a{}</styles>b</style>",
               nodes(element("style", nodes(text("a{}</styles>b")))));
}

// ---- 正常系: 文字参照 -----------------------------------------------------

TEST(HtmlParser, NamedCharacterReferences) {
  expect_shape("&amp;&lt;&gt;&quot;&apos;&nbsp;", nodes(text("&<>\"'\u00A0")));
}

TEST(HtmlParser, NumericCharacterReferences) {
  expect_shape("&#65;&#x42;&#X43;", nodes(text("ABC")));
}

TEST(HtmlParser, SupplementaryPlaneCharacterReference) {
  expect_shape("&#x1F600;&#128512;", nodes(text("\U0001F600\U0001F600")));
}

TEST(HtmlParser, JapaneseCharacterReference) {
  expect_shape("&#x3042;&#12356;", nodes(text("あい")));
}

TEST(HtmlParser, MaximumCodePointIsAccepted) {
  expect_shape("&#x10FFFF;", nodes(text("\U0010FFFF")));
}

// ---- 正常系: 深さ ---------------------------------------------------------

TEST(HtmlParser, NestingUpToTheLimitIsAccepted) {
  std::string source;
  for (std::size_t i = 0; i < kMaxNestingDepth; ++i) {
    source += "<div>";
  }
  source += "x";
  for (std::size_t i = 0; i < kMaxNestingDepth; ++i) {
    source += "</div>";
  }
  const Node root = parsed(source);
  std::size_t depth = 0;
  const Node* node = &root;
  while (!node->children.empty() && node->children.front().type == Node::Type::Element) {
    node = &node->children.front();
    ++depth;
  }
  EXPECT_EQ(depth, kMaxNestingDepth);
}

// ---- 位置 -----------------------------------------------------------------

TEST(HtmlParser, LocationsInMultilineJapaneseInput) {
  const Node root = parsed("<div>\n  <p>日本語の\n文章</p>\n</div>");
  ASSERT_EQ(root.children.size(), 1U);
  const Node& div = root.children.front();
  EXPECT_EQ(div.location, (SourceLocation{.offset = 0, .line = 1, .column = 1}));
  ASSERT_EQ(div.children.size(), 3U);

  EXPECT_EQ(div.children[0].type, Node::Type::Text);
  EXPECT_EQ(div.children[0].text, "\n  ");
  EXPECT_EQ(div.children[0].location.line, 1U);
  EXPECT_EQ(div.children[0].location.column, 6U);

  const Node& paragraph = div.children[1];
  EXPECT_EQ(paragraph.tag, "p");
  EXPECT_EQ(paragraph.location.line, 2U);
  EXPECT_EQ(paragraph.location.column, 3U);
  ASSERT_EQ(paragraph.children.size(), 1U);
  EXPECT_EQ(paragraph.children[0].text, "日本語の\n文章");
  // 桁はバイトではなくコードポイントで数える
  EXPECT_EQ(paragraph.children[0].location.line, 2U);
  EXPECT_EQ(paragraph.children[0].location.column, 6U);

  EXPECT_EQ(div.children[2].text, "\n");
  EXPECT_EQ(div.children[2].location.line, 3U);
  EXPECT_EQ(div.children[2].location.column, 7U);
}

TEST(HtmlParser, AttributeLocations) {
  const Node root = parsed("<div\n  id=\"a\"\n  class=\"b\"></div>");
  ASSERT_EQ(root.children.size(), 1U);
  const std::vector<Attribute>& attrs = root.children.front().attrs;
  ASSERT_EQ(attrs.size(), 2U);
  EXPECT_EQ(attrs[0].location.line, 2U);
  EXPECT_EQ(attrs[0].location.column, 3U);
  EXPECT_EQ(attrs[1].location.line, 3U);
  EXPECT_EQ(attrs[1].location.column, 3U);
}

TEST(HtmlParser, CrLfAndCrEachCountAsOneLine) {
  // 改行は LF / CRLF / CR のいずれも 1 行（core/utf8.hpp の locate() と同じ数え方）
  const Node root = parsed("<div>\r\n<p>x</p>\r<br>\n</div>");
  ASSERT_EQ(root.children.size(), 1U);
  const Node& div = root.children.front();
  ASSERT_EQ(div.children.size(), 5U);

  EXPECT_EQ(div.children[0].text, "\r\n");  // 1 行目の `<div>` の直後
  EXPECT_EQ(div.children[0].location.line, 1U);
  EXPECT_EQ(div.children[0].location.column, 6U);

  EXPECT_EQ(div.children[1].tag, "p");  // CRLF で 1 行だけ進む
  EXPECT_EQ(div.children[1].location.line, 2U);
  EXPECT_EQ(div.children[1].location.column, 1U);

  EXPECT_EQ(div.children[2].text, "\r");  // `</p>` の直後、まだ 2 行目
  EXPECT_EQ(div.children[2].location.line, 2U);
  EXPECT_EQ(div.children[2].location.column, 9U);

  EXPECT_EQ(div.children[3].tag, "br");  // CR 単独でも 1 行進む
  EXPECT_EQ(div.children[3].location.line, 3U);
  EXPECT_EQ(div.children[3].location.column, 1U);

  EXPECT_EQ(div.children[4].text, "\n");
  EXPECT_EQ(div.children[4].location.line, 3U);
  EXPECT_EQ(div.children[4].location.column, 5U);
}

// ---- エラー系 -------------------------------------------------------------

struct ErrorCase {
  std::string_view name;
  std::string_view source;
  ErrorKind kind;
  std::uint32_t line;
  std::uint32_t column;
  std::string_view message_contains;
};

TEST(HtmlParseError, Table) {
  const std::vector<ErrorCase> cases = {
      // 未対応タグ
      {"table", "<table></table>", ErrorKind::UnsupportedTag, 1, 1, "`<table>` is not supported"},
      {"script", "<div><script></script></div>", ErrorKind::UnsupportedTag, 1, 6, "`<script>`"},
      {"html", "<html><body></body></html>", ErrorKind::UnsupportedTag, 1, 1, "`<html>`"},
      {"body", "<div>\n<body></body></div>", ErrorKind::UnsupportedTag, 2, 1, "`<body>`"},
      {"anchor", "<p>a<a href=\"x\">b</a></p>", ErrorKind::UnsupportedTag, 1, 5, "`<a>`"},
      {"custom element", "<my-widget></my-widget>", ErrorKind::UnsupportedTag, 1, 1,
       "`<my-widget>`"},
      {"unsupported end tag", "</table>", ErrorKind::UnsupportedTag, 1, 1, "`</table>`"},
      {"supported tag list", "<table>", ErrorKind::UnsupportedTag, 1, 1,
       "supported tags: br, div, h1, h2, h3, h4, h5, h6, img, p, rp, rt, ruby, span, style"},

      // 未対応属性
      {"href on div", "<div href=\"x\"></div>", ErrorKind::UnsupportedAttribute, 1, 6,
       "`href` is not supported on `<div>`"},
      {"src on div", "<div src=\"x\"></div>", ErrorKind::UnsupportedAttribute, 1, 6,
       "supported attributes: class, id, style"},
      {"title on img", R"(<img alt="a" title="t">)", ErrorKind::UnsupportedAttribute, 1, 14,
       "supported attributes: alt, class, height, id, src, style, width"},
      {"width on div", "<div width=\"1\"></div>", ErrorKind::UnsupportedAttribute, 1, 6,
       "`width` is not supported on `<div>`"},

      // 属性の重複
      {"duplicate class", R"(<div class="a" class="b"></div>)", ErrorKind::HtmlParse, 1, 16,
       "duplicate attribute `class` on `<div>` (first given at 1:6)"},
      {"duplicate img src", "<img src=a src=b>", ErrorKind::HtmlParse, 1, 12,
       "duplicate attribute `src`"},

      // 入れ子と終了タグ
      {"unclosed div", "<div>", ErrorKind::HtmlParse, 1, 1, "unclosed element `<div>`"},
      {"unclosed inner", "<div><span>hi", ErrorKind::HtmlParse, 1, 6, "unclosed element `<span>`"},
      {"mismatched end tag", "<div><span></div>", ErrorKind::HtmlParse, 1, 12,
       "`</div>` does not match the open element `<span>` (opened at 1:6)"},
      {"stray end tag", "</div>", ErrorKind::HtmlParse, 1, 1,
       "unexpected end tag `</div>`: no element is open"},
      {"extra end tag", "<div></div></div>", ErrorKind::HtmlParse, 1, 12,
       "unexpected end tag `</div>`"},
      {"end tag for br", "<br></br>", ErrorKind::HtmlParse, 1, 5,
       "`</br>` is not allowed: `br` is a void element"},
      {"end tag for img", "<img src=\"a\"></img>", ErrorKind::HtmlParse, 1, 14, "`</img>`"},
      {"self closing div", "<div/>", ErrorKind::HtmlParse, 1, 1, "`<div/>` is not allowed"},
      {"self closing span", "<p><span/></p>", ErrorKind::HtmlParse, 1, 4,
       "only br, img are void elements"},
      {"empty end tag", "<div></></div>", ErrorKind::HtmlParse, 1, 6,
       "`</` must be followed by a tag name"},
      {"garbage in end tag", "<div></div x>", ErrorKind::HtmlParse, 1, 12,
       "unexpected `x` in the end tag `</div>`"},

      // タグの構文
      {"bare less than", "< div>", ErrorKind::HtmlParse, 1, 1, "`<` must start a tag"},
      {"less than at end", "a<", ErrorKind::HtmlParse, 1, 2, "the end of the input"},
      {"unterminated start tag", "<div", ErrorKind::HtmlParse, 1, 1, "unterminated start tag"},
      {"unterminated end tag", "<div></div", ErrorKind::HtmlParse, 1, 6, "unterminated end tag"},
      {"slash not before gt", "<img /x>", ErrorKind::HtmlParse, 1, 1,
       "`/` in the start tag `<img>` must be followed by `>`"},
      {"missing whitespace", R"(<div id="a"class="b"></div>)", ErrorKind::HtmlParse, 1, 12,
       "attributes must be separated by whitespace"},
      {"non-ascii attribute name", "<div \u263A></div>", ErrorKind::HtmlParse, 1, 6, "U+263A"},
      {"missing attribute value", "<div id=></div>", ErrorKind::HtmlParse, 1, 9,
       "missing value for attribute `id` on `<div>`"},
      {"slash in unquoted value", "<img src=a/b.png>", ErrorKind::HtmlParse, 1, 11,
       "`/` is not allowed in an unquoted attribute value"},
      {"quote in unquoted value", "<div id=a\"b\"></div>", ErrorKind::HtmlParse, 1, 10,
       "not allowed in an unquoted attribute value"},
      {"unterminated quoted value", "<div class=\"a></div>", ErrorKind::HtmlParse, 1, 12,
       "unterminated value for attribute `class`"},
      {"unterminated single quote", "<div class='a></div>", ErrorKind::HtmlParse, 1, 12,
       "expected a closing `'`"},

      // コメント / DOCTYPE / style
      {"unterminated comment", "<div><!-- x</div>", ErrorKind::HtmlParse, 1, 6,
       "unterminated comment"},
      {"bogus bang", "<!foo>", ErrorKind::HtmlParse, 1, 1, "`<!` must start a comment"},
      {"unterminated doctype", "<!DOCTYPE html", ErrorKind::HtmlParse, 1, 1,
       "unterminated DOCTYPE"},
      {"unterminated style", "<style>body{}", ErrorKind::HtmlParse, 1, 1,
       "unterminated `<style>` element"},

      // 文字参照
      {"unknown reference", "<p>&copy;</p>", ErrorKind::HtmlParse, 1, 4,
       "unknown character reference `&copy;`"},
      {"reference name list", "&COPY;", ErrorKind::HtmlParse, 1, 1,
       "supported: &amp; &apos; &gt; &lt; &nbsp; &quot;"},
      {"missing semicolon", "a &amp b", ErrorKind::HtmlParse, 1, 3,
       "character reference `&amp` is missing the closing `;`"},
      {"ampersand then semicolon", "&;", ErrorKind::HtmlParse, 1, 1,
       "`&` must be followed by a character reference name or `#`"},
      {"ampersand then quote", "<div id=\"a&\"></div>", ErrorKind::HtmlParse, 1, 11,
       "write `&amp;` for a literal `&`"},
      {"no decimal digits", "&#;", ErrorKind::HtmlParse, 1, 1,
       "`&#` must be followed by one or more decimal digits"},
      {"no hex digits", "&#xg;", ErrorKind::HtmlParse, 1, 1,
       "`&#x` must be followed by one or more hexadecimal digits"},
      {"numeric missing semicolon", "&#65", ErrorKind::HtmlParse, 1, 1,
       "character reference `&#65` is missing the closing `;`"},
      {"code point too large", "&#x110000;", ErrorKind::HtmlParse, 1, 1,
       "above the maximum code point U+10FFFF"},
      {"code point far too large", "&#99999999999999;", ErrorKind::HtmlParse, 1, 1,
       "above the maximum code point"},
      {"surrogate reference", "&#xD800;", ErrorKind::HtmlParse, 1, 1,
       "surrogate code point U+D800"},
      {"surrogate reference decimal", "&#57343;", ErrorKind::HtmlParse, 1, 1,
       "surrogate code point U+DFFF"},
      {"nul reference", "&#0;", ErrorKind::HtmlParse, 1, 1, "is U+0000, which is not allowed"},
      {"reference in attribute", "<div id=\"&bogus;\"></div>", ErrorKind::HtmlParse, 1, 10,
       "unknown character reference `&bogus;`"},

      // 位置が 2 行目以降 / 日本語を含む入力
      {"error after japanese", "<p>日本語</p>\n<p>あ<b></b></p>", ErrorKind::UnsupportedTag, 2, 5,
       "`<b>`"},
      {"error column counts code points", "<p>日本語&bad;</p>", ErrorKind::HtmlParse, 1, 7,
       "unknown character reference"},
  };

  for (const ErrorCase& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    const Error error = parse_failure(test_case.source);
    EXPECT_EQ(error.kind, test_case.kind);
    EXPECT_TRUE(error.location.has_value());
    const SourceLocation location = error.location.value_or(SourceLocation{});
    EXPECT_EQ(location.line, test_case.line);
    EXPECT_EQ(location.column, test_case.column);
    EXPECT_THAT(error.message, HasSubstr(std::string{test_case.message_contains}));
  }
}

TEST(HtmlParseError, InvalidUtf8) {
  const Error error = parse_failure("<div>\xFF</div>");
  EXPECT_EQ(error.kind, ErrorKind::InvalidUtf8);
  ASSERT_TRUE(error.location.has_value());
  const SourceLocation location = error.location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 1U);
  EXPECT_EQ(location.column, 6U);
  EXPECT_THAT(error.message, HasSubstr("invalid UTF-8"));
}

TEST(HtmlParseError, TruncatedUtf8Sequence) {
  const Error error = parse_failure("<p>\xE3\x81</p>");
  EXPECT_EQ(error.kind, ErrorKind::InvalidUtf8);
  ASSERT_TRUE(error.location.has_value());
  const SourceLocation location = error.location.value_or(SourceLocation{});
  EXPECT_EQ(location.column, 4U);
}

TEST(HtmlParseError, NulCharacter) {
  const std::string source("<div>\0</div>", 12);
  const Error error = parse_failure(source);
  EXPECT_EQ(error.kind, ErrorKind::HtmlParse);
  ASSERT_TRUE(error.location.has_value());
  const SourceLocation location = error.location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 1U);
  EXPECT_EQ(location.column, 6U);
  EXPECT_THAT(error.message, HasSubstr("NUL character"));
}

TEST(HtmlParseError, NulCharacterInsideStyle) {
  const std::string source("<style>a\0{}</style>", 19);
  const Error error = parse_failure(source);
  EXPECT_EQ(error.kind, ErrorKind::HtmlParse);
  EXPECT_THAT(error.message, HasSubstr("NUL character"));
}

TEST(HtmlParseError, NestingTooDeep) {
  std::string source;
  for (std::size_t i = 0; i <= kMaxNestingDepth; ++i) {
    source += "<div>";
  }
  const Error error = parse_failure(source);
  // 構文の誤りではなく「上限を超えた」なので LimitExceeded（A25）。
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  ASSERT_TRUE(error.location.has_value());
  const SourceLocation location = error.location.value_or(SourceLocation{});
  EXPECT_EQ(location.line, 1U);
  // 257 個目の `<div>` の位置（1 個 5 桁）
  EXPECT_EQ(location.column, static_cast<std::uint32_t>((kMaxNestingDepth * 5) + 1));
  EXPECT_THAT(error.message, HasSubstr("nested too deeply"));
  EXPECT_THAT(error.message, HasSubstr("the maximum is 256"));
}

// 深さの上限は呼び出し側が決める（api は RenderLimits::nesting_depth を渡す）。
// ちょうどは通り、1 段でも超えたら LimitExceeded。
TEST(HtmlParseError, NestingDepthIsAParameter) {
  const auto nested = [](std::size_t depth) {
    std::string source;
    for (std::size_t i = 0; i < depth; ++i) {
      source += "<div>";
    }
    for (std::size_t i = 0; i < depth; ++i) {
      source += "</div>";
    }
    return source;
  };

  Diagnostics diagnostics{RenderLimits{}.max_diagnostics};
  const Result<Node> exact = parse(nested(4), diagnostics, 4);
  ASSERT_TRUE(exact.has_value()) << to_string(exact.error());

  Result<Node> over = parse(nested(5), diagnostics, 4);
  ASSERT_FALSE(over.has_value());
  const Error error = std::move(over).error();
  EXPECT_EQ(error.kind, ErrorKind::LimitExceeded);
  EXPECT_THAT(error.message, HasSubstr("the maximum is 4"));
  ASSERT_TRUE(error.location.has_value());
  // 5 個目の `<div>`（1 個 5 桁）
  EXPECT_EQ(error.location.value_or(SourceLocation{}).column, 21U);
}

TEST(HtmlParseError, FormattedErrorMentionsKindAndLocation) {
  const Error error = parse_failure("<div>\n  <table>\n</div>");
  EXPECT_EQ(to_string(error).substr(0, 32), "error[unsupported-tag] at 2:3: `");
}

}  // namespace
}  // namespace shashoku::html
