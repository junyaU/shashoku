#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "paint/display_list_builder.hpp"
#include "raster/display_list.hpp"

// --dump-stage=display-list と --dump-stage=svg の出力（DESIGN.md §3-3）。
namespace shashoku::paint::test {
namespace {

constexpr Color kRed{255, 0, 0, 255};
constexpr Color kHalfBlue{0, 0, 255, 128};

raster::DisplayList sample_list() {
  return {raster::FillRect{Rect{0, 0, 100, 40}, kRed},
          raster::StrokeRoundedRect{Rect{0, 0, 100, 40}, 4, 1.5F, kBlack},
          raster::FillRoundedRect{Rect{8, 8, 20, 20}, 10, kHalfBlue},
          raster::DrawGlyphs{2, 16, kBlack, true, {{10, Point{4, 20.5F}}, {11, Point{20, 20.5F}}}},
          raster::PushClip{Rect{8, 8, 20, 20}, 10},
          raster::DrawImage{1, Rect{8, 8, 20, 20}},
          raster::PopClip{}};
}

// 出力形式そのものを固定する（キー順・インデント・色の書式）。
TEST(PaintDumpJson, FixedFormat) {
  EXPECT_EQ(dump_json(sample_list()), R"JSON({
  "commands": [
    {
      "op": "fill_rect",
      "rect": [
        0,
        0,
        100,
        40
      ],
      "color": "#ff0000ff"
    },
    {
      "op": "stroke_rounded_rect",
      "rect": [
        0,
        0,
        100,
        40
      ],
      "radius": 4,
      "width": 1.5,
      "color": "#000000ff"
    },
    {
      "op": "fill_rounded_rect",
      "rect": [
        8,
        8,
        20,
        20
      ],
      "radius": 10,
      "color": "#0000ff80"
    },
    {
      "op": "draw_glyphs",
      "font": 2,
      "size": 16,
      "color": "#000000ff",
      "sideways": true,
      "glyphs": [
        [
          10,
          4,
          20.5
        ],
        [
          11,
          20,
          20.5
        ]
      ]
    },
    {
      "op": "push_clip",
      "rect": [
        8,
        8,
        20,
        20
      ],
      "radius": 10
    },
    {
      "op": "draw_image",
      "image": 1,
      "dest": [
        8,
        8,
        20,
        20
      ]
    },
    {
      "op": "pop_clip"
    }
  ]
})JSON");
}

TEST(PaintDumpJson, EmptyList) { EXPECT_EQ(dump_json({}), "{\n  \"commands\": []\n}"); }

// 既定値のキーは省く。
TEST(PaintDumpJson, OmitsDefaultSideways) {
  const std::string json =
      dump_json({raster::DrawGlyphs{0, 16, kBlack, false, {{10, Point{0, 0}}}}});
  EXPECT_EQ(json.find("sideways"), std::string::npos);
}

// ---------------------------------------------------------------------------
// SVG。中身の見た目はデバッグ用なので固定しないが、XML として整形式であることは要る。
// ---------------------------------------------------------------------------

// 最小限の整形式チェック: タグの対応、属性の引用符、実体参照。
::testing::AssertionResult well_formed_xml(std::string_view xml) {
  const auto fail = [](std::size_t at, std::string_view why) {
    return ::testing::AssertionFailure() << why << " (offset " << at << ")";
  };
  const auto entities_ok = [&](std::string_view text, std::size_t base) {
    for (std::size_t i = text.find('&'); i != std::string_view::npos; i = text.find('&', i + 1)) {
      const std::size_t end = text.find(';', i);
      if (end == std::string_view::npos || end == i + 1) {
        return fail(base + i, "bare '&'");
      }
      const std::string_view name = text.substr(i + 1, end - i - 1);
      const bool numeric = name.size() > 1 && name.front() == '#';
      if (!numeric && name != "amp" && name != "lt" && name != "gt" && name != "quot" &&
          name != "apos") {
        return fail(base + i, "unknown entity");
      }
    }
    return ::testing::AssertionSuccess();
  };

  std::vector<std::string_view> open;
  std::size_t elements = 0;
  std::size_t i = 0;
  while (i < xml.size()) {
    const std::size_t next = xml.find('<', i);
    if (next == std::string_view::npos) {
      if (const auto r = entities_ok(xml.substr(i), i); !r) {
        return r;
      }
      break;
    }
    if (const auto r = entities_ok(xml.substr(i, next - i), i); !r) {
      return r;
    }
    i = next;
    if (xml.compare(i, 4, "<!--") == 0) {
      const std::size_t end = xml.find("-->", i + 4);
      if (end == std::string_view::npos) {
        return fail(i, "unterminated comment");
      }
      i = end + 3;
      continue;
    }
    if (xml.compare(i, 2, "<?") == 0) {
      const std::size_t end = xml.find("?>", i + 2);
      if (end == std::string_view::npos) {
        return fail(i, "unterminated processing instruction");
      }
      i = end + 2;
      continue;
    }
    const bool closing = xml.compare(i, 2, "</") == 0;
    std::size_t cursor = i + (closing ? 2 : 1);
    const std::size_t name_begin = cursor;
    while (cursor < xml.size() && (std::isalnum(static_cast<unsigned char>(xml[cursor])) != 0 ||
                                   xml[cursor] == ':' || xml[cursor] == '-' || xml[cursor] == '_')) {
      ++cursor;
    }
    if (cursor == name_begin) {
      return fail(i, "empty tag name");
    }
    const std::string_view name = xml.substr(name_begin, cursor - name_begin);
    bool self_closing = false;
    // 属性部分: name="value" の繰り返し（引用符は " のみ使う）。
    while (cursor < xml.size() && xml[cursor] != '>') {
      if (xml[cursor] == '/') {
        self_closing = true;
        ++cursor;
        continue;
      }
      if (std::isspace(static_cast<unsigned char>(xml[cursor])) != 0) {
        ++cursor;
        continue;
      }
      const std::size_t equals = xml.find('=', cursor);
      if (equals == std::string_view::npos || equals + 1 >= xml.size() || xml[equals + 1] != '"') {
        return fail(cursor, "attribute is not name=\"value\"");
      }
      const std::size_t value_end = xml.find('"', equals + 2);
      if (value_end == std::string_view::npos) {
        return fail(equals, "unterminated attribute value");
      }
      if (const auto r = entities_ok(xml.substr(equals + 2, value_end - equals - 2), equals + 2);
          !r) {
        return r;
      }
      cursor = value_end + 1;
    }
    if (cursor >= xml.size()) {
      return fail(i, "unterminated tag");
    }
    if (closing) {
      if (open.empty() || open.back() != name) {
        return fail(i, "mismatched closing tag");
      }
      open.pop_back();
    } else {
      ++elements;
      if (!self_closing) {
        open.push_back(name);
      }
    }
    i = cursor + 1;
  }
  if (!open.empty()) {
    return ::testing::AssertionFailure() << "unclosed tag <" << open.back() << ">";
  }
  if (elements == 0) {
    return ::testing::AssertionFailure() << "no elements";
  }
  return ::testing::AssertionSuccess();
}

TEST(PaintDumpSvg, WellFormed) {
  const std::string svg = dump_svg(sample_list(), 100, 40);
  EXPECT_TRUE(well_formed_xml(svg)) << svg;
  EXPECT_TRUE(svg.starts_with("<?xml"));
  EXPECT_NE(svg.find("<svg"), std::string::npos);
  EXPECT_NE(svg.find(R"(width="100")"), std::string::npos);
  EXPECT_NE(svg.find("clip-path"), std::string::npos);
}

TEST(PaintDumpSvg, EmptyListIsStillWellFormed) {
  const std::string svg = dump_svg({}, 10, 10);
  EXPECT_TRUE(well_formed_xml(svg)) << svg;
}

// 壊れた命令列でも整形式を保つ（デバッグ用の出力が読めなくなると困る）。
TEST(PaintDumpSvg, UnbalancedClipsAreStillWellFormed) {
  const std::string unclosed =
      dump_svg({raster::PushClip{Rect{0, 0, 5, 5}, 1}, raster::PushClip{Rect{0, 0, 5, 5}, 0},
                raster::FillRect{Rect{0, 0, 5, 5}, kRed}},
               10, 10);
  EXPECT_TRUE(well_formed_xml(unclosed)) << unclosed;

  const std::string extra_pop =
      dump_svg({raster::PopClip{}, raster::FillRect{Rect{0, 0, 5, 5}, kRed}, raster::PopClip{}}, 10,
               10);
  EXPECT_TRUE(well_formed_xml(extra_pop)) << extra_pop;
}

// 非有限値が混ざっても数値として妥当な SVG になる（ラスタライザは無視する命令だが、
// ダンプは落ちずに読めること）。
TEST(PaintDumpSvg, NonFiniteCoordinates) {
  const float inf = std::numeric_limits<float>::infinity();
  const std::string svg = dump_svg(
      {raster::FillRect{Rect{inf, -inf, 10, 10}, kRed},
       raster::DrawGlyphs{0, 16, kBlack, false, {{1, Point{std::nanf(""), 0}}}}},
      10, 10);
  EXPECT_TRUE(well_formed_xml(svg)) << svg;
  EXPECT_EQ(svg.find("inf"), std::string::npos);
  EXPECT_EQ(svg.find("nan"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::paint::test
