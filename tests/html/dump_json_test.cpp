#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "html/dom.hpp"
#include "html/parser.hpp"

namespace shashoku::html {
namespace {

std::string dumped(std::string_view source) {
  Result<Node> result = parse(source);
  if (!result) {
    ADD_FAILURE() << "parse failed: " << to_string(result.error());
    return {};
  }
  return dump_json(*result);
}

// 出力形式そのものを固定する（--dump-stage=dom の契約。キー順・インデントを含む）。
TEST(HtmlDumpJson, ElementWithAttributeAndText) {
  EXPECT_EQ(dumped("<div id=\"a\">hi</div>"), R"JSON({
  "type": "element",
  "tag": "#root",
  "attrs": [],
  "location": {
    "line": 1,
    "column": 1
  },
  "children": [
    {
      "type": "element",
      "tag": "div",
      "attrs": [
        {
          "name": "id",
          "value": "a"
        }
      ],
      "location": {
        "line": 1,
        "column": 1
      },
      "children": [
        {
          "type": "text",
          "text": "hi",
          "location": {
            "line": 1,
            "column": 13
          }
        }
      ]
    }
  ]
})JSON");
}

// テキストのエスケープ、空要素（children が空）、複数行の位置。
TEST(HtmlDumpJson, TextEscapingAndVoidElement) {
  EXPECT_EQ(dumped("&quot;\n<br>"), R"JSON({
  "type": "element",
  "tag": "#root",
  "attrs": [],
  "location": {
    "line": 1,
    "column": 1
  },
  "children": [
    {
      "type": "text",
      "text": "\"\n",
      "location": {
        "line": 1,
        "column": 1
      }
    },
    {
      "type": "element",
      "tag": "br",
      "attrs": [],
      "location": {
        "line": 2,
        "column": 1
      },
      "children": []
    }
  ]
})JSON");
}

TEST(HtmlDumpJson, EmptyRoot) {
  EXPECT_EQ(dumped(""), R"JSON({
  "type": "element",
  "tag": "#root",
  "attrs": [],
  "location": {
    "line": 1,
    "column": 1
  },
  "children": []
})JSON");
}

TEST(HtmlDumpJson, NonAsciiIsWrittenAsUtf8) {
  const std::string json = dumped("<p>日本語</p>");
  EXPECT_NE(json.find("\"text\": \"日本語\""), std::string::npos) << json;
}

TEST(HtmlDumpJson, DeepTreeDoesNotRecurse) {
  // dump_json は明示スタックで走査するので、上限いっぱいの深さでも落ちない。
  std::string source;
  for (std::size_t i = 0; i < kMaxNestingDepth; ++i) {
    source += "<div>";
  }
  for (std::size_t i = 0; i < kMaxNestingDepth; ++i) {
    source += "</div>";
  }
  const std::string json = dumped(source);
  EXPECT_FALSE(json.empty());
}

}  // namespace
}  // namespace shashoku::html
