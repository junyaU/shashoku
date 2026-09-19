#include <string>

#include <gtest/gtest.h>

#include "layout/test_support.hpp"

// --dump-stage=box の出力形式そのものを固定する（キー順・インデント・省略の規則）。
namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

TEST(LayoutDumpJson, FixedFormat) {
  FakeMeasurer measurer;
  // 期待値を切りのいい数にするため、メトリクスを ascent 0.75em / descent 0.25em にする
  measurer.ascent_ratio = 0.75F;
  measurer.descent_ratio = 0.25F;
  const auto root = build({block({text("あい")}, [](ComputedStyle& style) {
    style.background_color = Color{255, 0, 0, 255};
    style.padding = {2, 2, 2, 2};
  })});
  const auto tree = run_layout(root, 100, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(dump_json(*tree), R"JSON({
  "writing_mode": "horizontal-tb",
  "viewport_width": 100,
  "viewport_height": null,
  "root": {
    "tag": "#root",
    "rect": [
      0,
      0,
      100,
      20
    ],
    "blocks": [
      {
        "tag": "div",
        "rect": [
          0,
          0,
          100,
          20
        ],
        "background_color": "#ff0000ff",
        "padding": [
          2,
          2,
          2,
          2
        ],
        "lines": [
          {
            "rect": [
              2,
              2,
              96,
              16
            ],
            "baseline": 14,
            "fragments": [
              {
                "type": "text",
                "text": "あい",
                "font": 0,
                "font_size": 16,
                "color": "#000000ff",
                "inline_start": 2,
                "inline_size": 32,
                "baseline": 14,
                "glyphs": [
                  [
                    12354,
                    2,
                    0,
                    0
                  ],
                  [
                    12356,
                    18,
                    0,
                    0
                  ]
                ]
              }
            ]
          }
        ]
      }
    ]
  }
})JSON");
}

// 既定値のキーは出さない（透明な背景・幅 0 の枠線・padding 0）。
TEST(LayoutDumpJson, OmitsDefaults) {
  FakeMeasurer measurer;
  const auto tree = run_layout(build({}), 100, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(dump_json(*tree), R"JSON({
  "writing_mode": "horizontal-tb",
  "viewport_width": 100,
  "viewport_height": null,
  "root": {
    "tag": "#root",
    "rect": [
      0,
      0,
      100,
      0
    ],
    "lines": []
  }
})JSON");
}

// 枠線と角丸、インライン背景も出る（paint が読む情報が全部載っていること）。
TEST(LayoutDumpJson, BorderAndInlineBackground) {
  FakeMeasurer measurer;
  measurer.ascent_ratio = 0.75F;
  measurer.descent_ratio = 0.25F;
  const auto root = build({block(
      {inline_box({text("あ")},
                  [](ComputedStyle& style) { style.background_color = Color{0, 0, 255, 128}; })},
      [](ComputedStyle& style) {
        style.border_width = 1;
        style.border_color = Color{0, 255, 0, 255};
        style.border_radius = 4;
      })});
  const auto tree = run_layout(root, 40, measurer);
  ASSERT_TRUE(tree.has_value());
  EXPECT_EQ(dump_json(*tree), R"JSON({
  "writing_mode": "horizontal-tb",
  "viewport_width": 40,
  "viewport_height": null,
  "root": {
    "tag": "#root",
    "rect": [
      0,
      0,
      40,
      18
    ],
    "blocks": [
      {
        "tag": "div",
        "rect": [
          0,
          0,
          40,
          18
        ],
        "border_width": 1,
        "border_color": "#00ff00ff",
        "border_radius": 4,
        "lines": [
          {
            "rect": [
              1,
              1,
              38,
              16
            ],
            "baseline": 13,
            "fragments": [
              {
                "type": "background",
                "rect": [
                  1,
                  1,
                  16,
                  16
                ],
                "color": "#0000ff80"
              },
              {
                "type": "text",
                "text": "あ",
                "font": 0,
                "font_size": 16,
                "color": "#000000ff",
                "inline_start": 1,
                "inline_size": 16,
                "baseline": 13,
                "glyphs": [
                  [
                    12354,
                    1,
                    0,
                    0
                  ]
                ]
              }
            ]
          }
        ]
      }
    ]
  }
})JSON");
}

}  // namespace
}  // namespace shashoku::layout::test
