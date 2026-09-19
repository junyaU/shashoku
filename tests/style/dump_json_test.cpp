#include <string>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "style/computed_style.hpp"
#include "style_test_dom.hpp"

// --dump-stage=style の出力。キー順と表現を文字列完全一致で固定する。

namespace shashoku::style {
namespace {

TEST(StyleDump, GoldenOutput) {
  const html::Node tree = test_root(
      test_style_element("p { color: #336699; line-height: 1.5 }"),
      test_parent("p", {test_attr("style", "width: 50%; margin: 0 auto; border: 2px solid")},
                  test_text("写植")),
      test_element("img", {test_attr("src", "logo.png"), test_attr("width", "24")}));
  const Result<StyledNode> styled = resolve(tree);
  ASSERT_TRUE(styled.has_value()) << (styled ? "" : styled.error().message);

  EXPECT_EQ(dump_json(*styled), R"({
  "type": "element",
  "tag": "#root",
  "style": {
    "display": "block",
    "width": "auto",
    "height": "auto",
    "margin": [
      {
        "px": 0
      },
      {
        "px": 0
      },
      {
        "px": 0
      },
      {
        "px": 0
      }
    ],
    "padding": [
      0,
      0,
      0,
      0
    ],
    "border-width": 0,
    "border-color": "#000000ff",
    "border-radius": 0,
    "background-color": "#00000000",
    "flex-direction": "row",
    "justify-content": "flex-start",
    "align-items": "stretch",
    "row-gap": 0,
    "column-gap": 0,
    "flex-grow": 0,
    "flex-shrink": 1,
    "flex-basis": "auto",
    "color": "#000000ff",
    "font-size": 16,
    "font-family": [],
    "font-weight": 400,
    "line-height": "normal",
    "letter-spacing": 0,
    "text-align": "start",
    "line-break": "auto",
    "overflow-wrap": "normal",
    "writing-mode": "horizontal-tb"
  },
  "children": [
    {
      "type": "element",
      "tag": "p",
      "style": {
        "display": "block",
        "width": {
          "percent": 50
        },
        "height": "auto",
        "margin": [
          {
            "px": 0
          },
          "auto",
          {
            "px": 0
          },
          "auto"
        ],
        "padding": [
          0,
          0,
          0,
          0
        ],
        "border-width": 2,
        "border-color": "#336699ff",
        "border-radius": 0,
        "background-color": "#00000000",
        "flex-direction": "row",
        "justify-content": "flex-start",
        "align-items": "stretch",
        "row-gap": 0,
        "column-gap": 0,
        "flex-grow": 0,
        "flex-shrink": 1,
        "flex-basis": "auto",
        "color": "#336699ff",
        "font-size": 16,
        "font-family": [],
        "font-weight": 400,
        "line-height": {
          "number": 1.5
        },
        "letter-spacing": 0,
        "text-align": "start",
        "line-break": "auto",
        "overflow-wrap": "normal",
        "writing-mode": "horizontal-tb"
      },
      "children": [
        {
          "type": "text",
          "text": "写植",
          "style": {
            "display": "inline",
            "width": "auto",
            "height": "auto",
            "margin": [
              {
                "px": 0
              },
              {
                "px": 0
              },
              {
                "px": 0
              },
              {
                "px": 0
              }
            ],
            "padding": [
              0,
              0,
              0,
              0
            ],
            "border-width": 0,
            "border-color": "#336699ff",
            "border-radius": 0,
            "background-color": "#00000000",
            "flex-direction": "row",
            "justify-content": "flex-start",
            "align-items": "stretch",
            "row-gap": 0,
            "column-gap": 0,
            "flex-grow": 0,
            "flex-shrink": 1,
            "flex-basis": "auto",
            "color": "#336699ff",
            "font-size": 16,
            "font-family": [],
            "font-weight": 400,
            "line-height": {
              "number": 1.5
            },
            "letter-spacing": 0,
            "text-align": "start",
            "line-break": "auto",
            "overflow-wrap": "normal",
            "writing-mode": "horizontal-tb"
          }
        }
      ]
    },
    {
      "type": "element",
      "tag": "img",
      "src": "logo.png",
      "attr-width": 24,
      "attr-height": null,
      "style": {
        "display": "inline",
        "width": "auto",
        "height": "auto",
        "margin": [
          {
            "px": 0
          },
          {
            "px": 0
          },
          {
            "px": 0
          },
          {
            "px": 0
          }
        ],
        "padding": [
          0,
          0,
          0,
          0
        ],
        "border-width": 0,
        "border-color": "#000000ff",
        "border-radius": 0,
        "background-color": "#00000000",
        "flex-direction": "row",
        "justify-content": "flex-start",
        "align-items": "stretch",
        "row-gap": 0,
        "column-gap": 0,
        "flex-grow": 0,
        "flex-shrink": 1,
        "flex-basis": "auto",
        "color": "#000000ff",
        "font-size": 16,
        "font-family": [],
        "font-weight": 400,
        "line-height": "normal",
        "letter-spacing": 0,
        "text-align": "start",
        "line-break": "auto",
        "overflow-wrap": "normal",
        "writing-mode": "horizontal-tb"
      },
      "children": []
    }
  ]
})");
}

}  // namespace
}  // namespace shashoku::style
