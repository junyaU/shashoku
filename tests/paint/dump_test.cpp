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
// 本物の XML パーサは要らない（dump_svg が出す範囲だけ見れば十分）。
class XmlChecker {
 public:
  explicit XmlChecker(std::string_view xml) : xml_(xml) {}

  ::testing::AssertionResult run() {
    while (cursor_ < xml_.size()) {
      const std::size_t next = xml_.find('<', cursor_);
      const std::size_t length =
          next == std::string_view::npos ? xml_.size() - cursor_ : next - cursor_;
      if (const auto text = check_entities(xml_.substr(cursor_, length), cursor_); !text) {
        return text;
      }
      if (next == std::string_view::npos) {
        break;
      }
      cursor_ = next;
      if (const auto markup = check_markup(); !markup) {
        return markup;
      }
    }
    if (!open_.empty()) {
      return ::testing::AssertionFailure() << "unclosed tag <" << open_.back() << '>';
    }
    if (elements_ == 0) {
      return ::testing::AssertionFailure() << "no elements";
    }
    return ::testing::AssertionSuccess();
  }

 private:
  static ::testing::AssertionResult fail(std::size_t at, std::string_view why) {
    return ::testing::AssertionFailure() << why << " (offset " << at << ')';
  }

  static bool known_entity(std::string_view name) {
    if (name.size() > 1 && name.front() == '#') {
      return true;  // 数値参照
    }
    return name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos";
  }

  static ::testing::AssertionResult check_entities(std::string_view text, std::size_t base) {
    for (std::size_t i = text.find('&'); i != std::string_view::npos; i = text.find('&', i + 1)) {
      const std::size_t end = text.find(';', i);
      if (end == std::string_view::npos || end == i + 1) {
        return fail(base + i, "bare '&'");
      }
      if (!known_entity(text.substr(i + 1, end - i - 1))) {
        return fail(base + i, "unknown entity");
      }
    }
    return ::testing::AssertionSuccess();
  }

  // コメントと処理命令は読み飛ばし、それ以外はタグとして検査する。
  ::testing::AssertionResult check_markup() {
    if (xml_.compare(cursor_, 4, "<!--") == 0) {
      return skip_to("-->", 4, "unterminated comment");
    }
    if (xml_.compare(cursor_, 2, "<?") == 0) {
      return skip_to("?>", 2, "unterminated processing instruction");
    }
    return check_tag();
  }

  ::testing::AssertionResult skip_to(std::string_view marker, std::size_t from,
                                     std::string_view why) {
    const std::size_t end = xml_.find(marker, cursor_ + from);
    if (end == std::string_view::npos) {
      return fail(cursor_, why);
    }
    cursor_ = end + marker.size();
    return ::testing::AssertionSuccess();
  }

  [[nodiscard]] std::string_view read_name(std::size_t& cursor) const {
    const std::size_t begin = cursor;
    const auto is_name_char = [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == ':' || c == '-' || c == '_';
    };
    while (cursor < xml_.size() && is_name_char(xml_[cursor])) {
      ++cursor;
    }
    return xml_.substr(begin, cursor - begin);
  }

  // 属性部分: name="value" の繰り返し（引用符は " のみ使う）。
  ::testing::AssertionResult check_attributes(std::size_t& cursor, bool& self_closing) const {
    while (cursor < xml_.size() && xml_[cursor] != '>') {
      if (xml_[cursor] == '/') {
        self_closing = true;
        ++cursor;
        continue;
      }
      if (std::isspace(static_cast<unsigned char>(xml_[cursor])) != 0) {
        ++cursor;
        continue;
      }
      const std::size_t equals = xml_.find('=', cursor);
      if (equals == std::string_view::npos || equals + 1 >= xml_.size() ||
          xml_[equals + 1] != '"') {
        return fail(cursor, "attribute is not name=\"value\"");
      }
      const std::size_t value_end = xml_.find('"', equals + 2);
      if (value_end == std::string_view::npos) {
        return fail(equals, "unterminated attribute value");
      }
      if (const auto value =
              check_entities(xml_.substr(equals + 2, value_end - equals - 2), equals + 2);
          !value) {
        return value;
      }
      cursor = value_end + 1;
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult check_tag() {
    const std::size_t begin = cursor_;
    const bool closing = xml_.compare(cursor_, 2, "</") == 0;
    std::size_t cursor = cursor_ + (closing ? 2 : 1);
    const std::string_view name = read_name(cursor);
    if (name.empty()) {
      return fail(begin, "empty tag name");
    }
    bool self_closing = false;
    if (const auto attributes = check_attributes(cursor, self_closing); !attributes) {
      return attributes;
    }
    if (cursor >= xml_.size()) {
      return fail(begin, "unterminated tag");
    }
    if (closing) {
      if (open_.empty() || open_.back() != name) {
        return fail(begin, "mismatched closing tag");
      }
      open_.pop_back();
    } else {
      ++elements_;
      if (!self_closing) {
        open_.push_back(name);
      }
    }
    cursor_ = cursor + 1;
    return ::testing::AssertionSuccess();
  }

  std::string_view xml_;
  std::vector<std::string_view> open_;
  std::size_t elements_ = 0;
  std::size_t cursor_ = 0;
};

::testing::AssertionResult well_formed_xml(std::string_view xml) { return XmlChecker(xml).run(); }

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

  const std::string extra_pop = dump_svg(
      {raster::PopClip{}, raster::FillRect{Rect{0, 0, 5, 5}, kRed}, raster::PopClip{}}, 10, 10);
  EXPECT_TRUE(well_formed_xml(extra_pop)) << extra_pop;
}

// 非有限値が混ざっても数値として妥当な SVG になる（ラスタライザは無視する命令だが、
// ダンプは落ちずに読めること）。
TEST(PaintDumpSvg, NonFiniteCoordinates) {
  const float inf = std::numeric_limits<float>::infinity();
  const std::string svg =
      dump_svg({raster::FillRect{Rect{inf, -inf, 10, 10}, kRed},
                raster::DrawGlyphs{0, 16, kBlack, false, {{1, Point{std::nanf(""), 0}}}}},
               10, 10);
  EXPECT_TRUE(well_formed_xml(svg)) << svg;
  EXPECT_EQ(svg.find("inf"), std::string::npos);
  EXPECT_EQ(svg.find("nan"), std::string::npos);
}

}  // namespace
}  // namespace shashoku::paint::test
