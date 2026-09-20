// 性質テスト（issue #10-2a）: 横書きと縦書きで、論理座標のボックスツリーが一致する。
//
// A1「レイアウトは論理座標（inline / block）で行い、物理座標への変換は paint で 1 回だけ」が
// 本当なら、同じ内容を horizontal-tb（幅 W）と vertical-rl（高さ W）で組んだ結果は、
// 書字方向に固有の差を除いてボックスツリーが一致するはず。ゴールデンは横書きと縦書きで
// 別々の画像なので、この「両者が同じ論理座標を通っている」ことは捕まえられない。
//
// ---- 許す差（コードから特定したもの。ここに挙げたもの以外は 1 ビットも違ってはいけない）----
//
// (1) BoxTree::writing_mode / viewport_width / viewport_height
//     ルートの包含ブロックは「横書きなら viewport の幅、縦書きなら高さ」（layout.cpp
//     layout_root）。同じ inline サイズで組むには片方は幅に、片方は高さに W を渡す。
//     縦書きの viewport_width はレイアウトには使われない（paint が物理座標に直すときだけ
//     使う）ので、このテストはわざと別の値を渡して、漏れていないことも一緒に確かめる。
//
// (2) ベースライン（LineBox::baseline / TextFragment::baseline）
//     横書きのベースラインは「行の上端から ascent + 半行間」、縦書きの「ベースライン」は
//     行の中心軸（inline_layout.cpp extend_line_height）。別物なので比べない。
//     行の高さ自体は、偽の測定器（ascent 0.88em + descent 0.12em = 1em）では一致する。
//
// (3) TextFragment::sideways と、それによる断片の切れ目
//     縦書きは欧文・数字を横倒しにする（UAX #50。偽の測定器も ASCII を sideways にする）。
//     断片は「フォントか sideways が変わるところ」で切れるので、和欧混植では断片の
//     分かれ方まで変わる。混植のケースは CompareOptions::glyph_granularity で
//     「行ごとのグリフ列」に均してから比べる。
//
// (4) 行の中の block 方向の配置が、書字方向で仕様として違うもの
//     * <img>: 横書きは margin-box の下端をベースラインに乗せ、縦書きは中心軸に中央揃え
//       （inline_layout.cpp measure_line / place_image）。行の高さまで変わるので、画像を
//       含むケースは inline 方向だけを比べる（CompareOptions::block_axis = false）。
//     * インライン背景: 横書きは ascent / descent、縦書きは font-size の半分ずつで作る
//       （inline_collect.cpp collect_element）。同じ font-size だけの行では一致するが、
//       行の中に大小が混ざると block 方向がずれる（CompareOptions::background_block）。
//     * ルビ: 親文字の font-size がブロックの font-size と違うと、ルビが張り出す側と
//       反対側の空き（横書きは descent、縦書きは行の半分）が違うので行の高さが変わる。
//       ふつうの使い方（親文字 = ブロックの font-size）では一致する。
//
// (5) <img> の固有寸法は物理（幅 × 高さ）なので、論理方向への読み替えが書字方向で入れ替わる
//     （image.cpp の map_.inline_of）。縦長の画像は横書きでは inline に幅、縦書きでは
//     inline に高さが来る。この差を避けるため、画像のテストは正方形を使う。
//
// (6) 縦書きでは幅の `%` が使えない（engine.cpp）ので、`%` は使わない。

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/test_support.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"

namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;
using style::Dimension;

// ---- 論理方向でスタイルを書く小道具 ---------------------------------------------
// 同じ内容を 2 つの書字方向で組むには CSS の物理プロパティを書き分ける必要がある
// （A1 の読み替え表 = logical.hpp の LogicalMap の逆向き）。

struct LogicalSpace {
  float inline_start = 0;
  float inline_end = 0;
  float block_start = 0;
  float block_end = 0;
};

// LogicalMap::edges の逆向き（logical.hpp）。
template <class T, class Make>
Edges<T> to_physical(const LogicalSpace& space, bool vertical, Make make) {
  if (vertical) {
    // vertical-rl: inline は上 → 下、block は右 → 左
    return Edges<T>{.top = make(space.inline_start),
                    .right = make(space.block_start),
                    .bottom = make(space.inline_end),
                    .left = make(space.block_end)};
  }
  return Edges<T>{.top = make(space.block_start),
                  .right = make(space.inline_end),
                  .bottom = make(space.block_end),
                  .left = make(space.inline_start)};
}

void set_inline_size(ComputedStyle& style, float px, bool vertical) {
  (vertical ? style.height : style.width) = Dimension::px(px);
}

void set_block_size(ComputedStyle& style, float px, bool vertical) {
  (vertical ? style.width : style.height) = Dimension::px(px);
}

void set_margin(ComputedStyle& style, const LogicalSpace& space, bool vertical) {
  style.margin = to_physical<Dimension>(space, vertical, [](float v) { return Dimension::px(v); });
}

void set_padding(ComputedStyle& style, const LogicalSpace& space, bool vertical) {
  style.padding = to_physical<float>(space, vertical, [](float v) { return v; });
}

// ---- 2 つのボックスツリーの比較 ---------------------------------------------------

struct CompareOptions {
  // block 方向（block_start / block_size）も比べる。<img> を含むケースだけ false。
  bool block_axis = true;
  // インライン背景の block 方向も比べる。行の中に font-size の大小が混ざるケースだけ false。
  bool background_block = true;
  // 断片の切れ目を無視し、行ごとのグリフ列として比べる（和欧混植 = sideways の差）。
  bool glyph_granularity = false;
  // 許す誤差（CSS px）。既定は 0 = ビット単位で一致すること。
  // ルビだけは書字方向で**足し算の式そのものが違う**（横書き: ascent + rt_ascent +
  // rt_descent + descent、縦書き: 親の半分 + rt + 親の半分）ので、同じ実数になる式でも
  // float の丸めで 1 ulp ずれる。そこだけ緩める。
  float tolerance = 0;
};

class Comparer {
 public:
  explicit Comparer(const CompareOptions& options) : options_(options) {}

  void compare(const BoxTree& horizontal, const BoxTree& vertical) {
    compare_block(horizontal.root, vertical.root, "root");
    if (horizontal.missing_glyphs != vertical.missing_glyphs) {
      add("missing_glyphs", "（横書きと縦書きで豆腐の記録が違う）");
    }
  }

  [[nodiscard]] const std::vector<std::string>& diffs() const { return diffs_; }
  [[nodiscard]] std::string text() const {
    std::string out;
    for (const std::string& diff : diffs_) {
      out += "\n  " + diff;
    }
    return out;
  }

 private:
  void add(std::string_view path, std::string_view what) {
    diffs_.emplace_back(std::format("{}: {}", path, what));
  }

  void equal(std::string_view path, float horizontal, float vertical) {
    const float difference = horizontal - vertical;
    // NaN はどの比較も偽になるので、この書き方だと「差あり」に倒れる（それでよい）
    const bool within = difference <= options_.tolerance && -difference <= options_.tolerance;
    if (!within) {
      add(path, std::format("horizontal-tb {} / vertical-rl {}", horizontal, vertical));
    }
  }

  template <class T>
  void equal_value(std::string_view path, const T& horizontal, const T& vertical) {
    if (!(horizontal == vertical)) {
      add(path, "値が違う");
    }
  }

  void compare_rect(const std::string& path, const LogicalRect& h, const LogicalRect& v,
                    bool block) {
    equal(path + ".inline_start", h.inline_start, v.inline_start);
    equal(path + ".inline_size", h.inline_size, v.inline_size);
    if (block) {
      equal(path + ".block_start", h.block_start, v.block_start);
      equal(path + ".block_size", h.block_size, v.block_size);
    }
  }

  void compare_block(const BlockBox& h, const BlockBox& v, const std::string& path);
  void compare_line(const LineBox& h, const LineBox& v, const std::string& path);
  void compare_fragments(const LineBox& h, const LineBox& v, const std::string& path);
  void compare_glyph_run(const LineBox& h, const LineBox& v, const std::string& path);
  void compare_text(const TextFragment& h, const TextFragment& v, const std::string& path);
  void compare_image(const ImageFragment& h, const ImageFragment& v, const std::string& path);

  CompareOptions options_;
  std::vector<std::string> diffs_;
};

void Comparer::compare_block(const BlockBox& h, const BlockBox& v, const std::string& path) {
  if (h.tag != v.tag) {
    add(path + ".tag", std::format(R"(horizontal-tb "{}" / vertical-rl "{}")", h.tag, v.tag));
    return;
  }
  compare_rect(path + ".rect", h.rect, v.rect, options_.block_axis);
  equal_value(path + ".decoration", h.decoration, v.decoration);
  equal(path + ".padding.inline_start", h.padding.inline_start, v.padding.inline_start);
  equal(path + ".padding.inline_end", h.padding.inline_end, v.padding.inline_end);
  if (options_.block_axis) {
    equal(path + ".padding.block_start", h.padding.block_start, v.padding.block_start);
    equal(path + ".padding.block_end", h.padding.block_end, v.padding.block_end);
  }
  if ((h.lines() != nullptr) != (v.lines() != nullptr)) {
    add(path, "片方だけが行ボックスを持っている");
    return;
  }
  if (const std::vector<LineBox>* lines = h.lines()) {
    const std::vector<LineBox>& other = *v.lines();
    if (lines->size() != other.size()) {
      add(path + ".lines", std::format("行数が違う: horizontal-tb {} / vertical-rl {}",
                                       lines->size(), other.size()));
      return;
    }
    for (std::size_t i = 0; i < lines->size(); ++i) {
      compare_line((*lines)[i], other[i], std::format("{}.lines[{}]", path, i));
    }
    return;
  }
  const std::vector<BlockBox>& blocks = *h.blocks();
  const std::vector<BlockBox>& other = *v.blocks();
  if (blocks.size() != other.size()) {
    add(path + ".blocks", std::format("子の数が違う: horizontal-tb {} / vertical-rl {}",
                                      blocks.size(), other.size()));
    return;
  }
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    compare_block(blocks[i], other[i], std::format("{}.blocks[{}]", path, i));
  }
}

void Comparer::compare_line(const LineBox& h, const LineBox& v, const std::string& path) {
  compare_rect(path + ".rect", h.rect, v.rect, options_.block_axis);
  // baseline は比べない（許す差 (2)）
  if (options_.glyph_granularity) {
    compare_glyph_run(h, v, path);
    return;
  }
  compare_fragments(h, v, path);
}

void Comparer::compare_fragments(const LineBox& h, const LineBox& v, const std::string& path) {
  if (h.fragments.size() != v.fragments.size()) {
    add(path + ".fragments", std::format("断片の数が違う: horizontal-tb {} / vertical-rl {}",
                                         h.fragments.size(), v.fragments.size()));
    return;
  }
  for (std::size_t i = 0; i < h.fragments.size(); ++i) {
    const std::string at = std::format("{}.fragments[{}]", path, i);
    if (h.fragments[i].index() != v.fragments[i].index()) {
      add(at, "断片の種類が違う");
      continue;
    }
    if (const auto* text = std::get_if<TextFragment>(&h.fragments[i])) {
      compare_text(*text, std::get<TextFragment>(v.fragments[i]), at);
      continue;
    }
    if (const auto* image = std::get_if<ImageFragment>(&h.fragments[i])) {
      compare_image(*image, std::get<ImageFragment>(v.fragments[i]), at);
      continue;
    }
    const auto& background = std::get<InlineBackground>(h.fragments[i]);
    const auto& other = std::get<InlineBackground>(v.fragments[i]);
    compare_rect(at + ".rect", background.rect, other.rect,
                 options_.block_axis && options_.background_block);
    equal_value(at + ".color", background.color, other.color);
  }
}

void Comparer::compare_text(const TextFragment& h, const TextFragment& v, const std::string& path) {
  if (h.text != v.text) {
    add(path + ".text", std::format(R"(horizontal-tb "{}" / vertical-rl "{}")", h.text, v.text));
  }
  equal(path + ".font", static_cast<float>(h.font), static_cast<float>(v.font));
  equal(path + ".font_size", h.font_size, v.font_size);
  equal_value(path + ".color", h.color, v.color);
  equal(path + ".inline_start", h.inline_start, v.inline_start);
  equal(path + ".inline_size", h.inline_size, v.inline_size);
  equal_value(path + ".location", h.location.offset, v.location.offset);
  // sideways と baseline は比べない（許す差 (2) (3)）
  if (h.glyphs.size() != v.glyphs.size()) {
    add(path + ".glyphs", std::format("グリフ数が違う: horizontal-tb {} / vertical-rl {}",
                                      h.glyphs.size(), v.glyphs.size()));
    return;
  }
  for (std::size_t i = 0; i < h.glyphs.size(); ++i) {
    const std::string at = std::format("{}.glyphs[{}]", path, i);
    equal_value(at + ".glyph_id", h.glyphs[i].glyph_id, v.glyphs[i].glyph_id);
    equal(at + ".inline_position", h.glyphs[i].inline_position, v.glyphs[i].inline_position);
    equal(at + ".x_offset", h.glyphs[i].x_offset, v.glyphs[i].x_offset);
    equal(at + ".y_offset", h.glyphs[i].y_offset, v.glyphs[i].y_offset);
  }
}

void Comparer::compare_image(const ImageFragment& h, const ImageFragment& v,
                             const std::string& path) {
  equal_value(path + ".image", h.image, v.image);
  compare_rect(path + ".rect", h.rect, v.rect, options_.block_axis);
  compare_rect(path + ".content_rect", h.content_rect, v.content_rect, options_.block_axis);
  equal_value(path + ".decoration", h.decoration, v.decoration);
}

// 断片の切れ目を無視して、行ごとの「テキスト」と「グリフ列」で比べる（許す差 (3)）。
void Comparer::compare_glyph_run(const LineBox& h, const LineBox& v, const std::string& path) {
  const auto flatten = [](const LineBox& line) {
    std::string text;
    std::vector<PositionedGlyph> glyphs;
    for (const TextFragment* fragment : text_fragments(line)) {
      text += fragment->text;
      glyphs.insert(glyphs.end(), fragment->glyphs.begin(), fragment->glyphs.end());
    }
    return std::pair{text, glyphs};
  };
  const auto [h_text, h_glyphs] = flatten(h);
  const auto [v_text, v_glyphs] = flatten(v);
  if (h_text != v_text) {
    add(path + ".text", std::format(R"(horizontal-tb "{}" / vertical-rl "{}")", h_text, v_text));
  }
  if (h_glyphs.size() != v_glyphs.size()) {
    add(path + ".glyphs", std::format("グリフ数が違う: horizontal-tb {} / vertical-rl {}",
                                      h_glyphs.size(), v_glyphs.size()));
    return;
  }
  for (std::size_t i = 0; i < h_glyphs.size(); ++i) {
    const std::string at = std::format("{}.glyphs[{}]", path, i);
    equal_value(at + ".glyph_id", h_glyphs[i].glyph_id, v_glyphs[i].glyph_id);
    equal(at + ".inline_position", h_glyphs[i].inline_position, v_glyphs[i].inline_position);
  }
}

// ---- 両方の書字方向で組む ---------------------------------------------------------

// 縦書きの viewport 幅。レイアウトには使われない値なので、わざと inline サイズと違う値にする。
inline constexpr float kUnusedViewportWidth = 997;

using MakeTree = std::function<std::vector<Tree>(bool vertical)>;

struct Both {
  Result<BoxTree> horizontal;
  Result<BoxTree> vertical;
};

Both layout_both(const MakeTree& make, float inline_size, bool sideways = false,
                 const ImageLookup* images = nullptr) {
  FakeMeasurer horizontal_measurer;
  FakeMeasurer vertical_measurer;
  horizontal_measurer.sideways_latin_in_vertical = sideways;
  vertical_measurer.sideways_latin_in_vertical = sideways;
  const style::StyledNode horizontal_root = build(make(false));
  const style::StyledNode vertical_root = build_vertical(make(true));
  const Options horizontal_options = make_options(inline_size);
  const Options vertical_opts = vertical_options(kUnusedViewportWidth, inline_size);
  if (images != nullptr) {
    return Both{run_layout(horizontal_root, horizontal_options, horizontal_measurer, *images),
                run_layout(vertical_root, vertical_opts, vertical_measurer, *images)};
  }
  return Both{run_layout(horizontal_root, horizontal_options, horizontal_measurer),
              run_layout(vertical_root, vertical_opts, vertical_measurer)};
}

::testing::AssertionResult same_logical_tree(const MakeTree& make, float inline_size,
                                             const CompareOptions& options = {},
                                             bool sideways = false,
                                             const ImageLookup* images = nullptr) {
  const Both both = layout_both(make, inline_size, sideways, images);
  if (!both.horizontal) {
    return ::testing::AssertionFailure()
           << "horizontal-tb のレイアウトが失敗: " << to_string(both.horizontal.error());
  }
  if (!both.vertical) {
    return ::testing::AssertionFailure()
           << "vertical-rl のレイアウトが失敗: " << to_string(both.vertical.error());
  }
  Comparer comparer(options);
  comparer.compare(*both.horizontal, *both.vertical);
  if (comparer.diffs().empty()) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "論理座標のボックスツリーが一致しない（"
                                       << comparer.diffs().size() << " 件）:" << comparer.text();
}

constexpr std::string_view kJapanese =
    "吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"
    "何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。";

// ---- ブロック ---------------------------------------------------------------------

TEST(WritingModeProperty, NestedBlocks) {
  const auto make = [](bool vertical) {
    return std::vector<Tree>{
        block(
            {
                block({text("あいうえお")},
                      [vertical](ComputedStyle& s) {
                        set_inline_size(s, 120, vertical);
                        set_margin(s, LogicalSpace{.inline_start = 8, .block_start = 6}, vertical);
                      }),
                block({text(kJapanese)},
                      [vertical](ComputedStyle& s) {
                        set_padding(s, LogicalSpace{4, 6, 8, 10}, vertical);
                        s.border_width = 2;
                        s.background_color = Color{200, 220, 255, 255};
                      }),
                block({text("かきくけこ")},
                      [vertical](ComputedStyle& s) { set_block_size(s, 40, vertical); }),
            },
            [vertical](ComputedStyle& s) {
              set_padding(s, LogicalSpace{.inline_start = 12, .inline_end = 4}, vertical);
            }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 300));
}

// マージンの相殺（A10）は block 方向の操作なので、書字方向を変えても同じ値が出るはず。
TEST(WritingModeProperty, MarginCollapsing) {
  const auto make = [](bool vertical) {
    const auto spaced = [vertical](float start, float end) {
      return [vertical, start, end](ComputedStyle& s) {
        set_margin(s, LogicalSpace{.block_start = start, .block_end = end}, vertical);
      };
    };
    return std::vector<Tree>{
        block({text("あ")}, spaced(10, 30)),
        block({text("い")}, spaced(20, 5)),
        block({text("う")}, spaced(-8, 12)),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 200));
}

// ---- インライン -------------------------------------------------------------------

TEST(WritingModeProperty, InlineRuns) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        block({
            text("あいうえお"),
            inline_box({text("かきくけこ")},
                       [](ComputedStyle& s) { s.color = Color{255, 0, 0, 255}; }),
            text("さしすせそ"),
            br(),
            inline_box({text("たちつてと")}, [](ComputedStyle& s) { s.letter_spacing = 3; }),
        }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 120));
}

// インライン背景は「同じ font-size だけの行」なら block 方向まで一致する。
TEST(WritingModeProperty, InlineBackgroundOfTheSameSize) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        block({
            text("あいうえお"),
            inline_box({text("かきくけこ")},
                       [](ComputedStyle& s) { s.background_color = Color{255, 255, 0, 255}; }),
            text("さしすせそ"),
        }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 120));
}

// 行の中に font-size の大小が混ざると、インライン背景の block 方向だけがずれる
// （横書きは ascent / descent、縦書きは font-size の半分ずつ。許す差 (4)）。
TEST(WritingModeProperty, InlineBackgroundOfMixedSizes) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        block({
            text("あいうえお"),
            inline_box({text("かきく")},
                       [](ComputedStyle& s) {
                         s.font_size = 8;
                         s.background_color = Color{255, 255, 0, 255};
                       }),
            text("さしすせそ"),
        }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 200, CompareOptions{.background_block = false}));
}

// 和欧混植 + sideways。断片の切れ目は縦書きだけで増えるが、グリフの inline 位置は同じ。
TEST(WritingModeProperty, MixedScriptsAtGlyphGranularity) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        block({text("これは shashoku という 日本語 typesetting engine です。")}),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 200, CompareOptions{.glyph_granularity = true},
                                /*sideways=*/true));
}

// ---- text-align / justify ----------------------------------------------------------

TEST(WritingModeProperty, TextAlign) {
  for (const style::TextAlign align :
       {style::TextAlign::Start, style::TextAlign::End, style::TextAlign::Center,
        style::TextAlign::Left, style::TextAlign::Right}) {
    const auto make = [align](bool) {
      return std::vector<Tree>{
          block({text(kJapanese)}, [align](ComputedStyle& s) { s.text_align = align; }),
      };
    };
    EXPECT_TRUE(same_logical_tree(make, 170)) << "text-align = " << static_cast<int>(align);
  }
}

TEST(WritingModeProperty, Justify) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        block({text("これは shashoku の 両端揃え です。行末の空きを字間に配分する。")},
              [](ComputedStyle& s) { s.text_align = style::TextAlign::Justify; }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 190, CompareOptions{.glyph_granularity = true},
                                /*sideways=*/true));
}

// ---- flex -------------------------------------------------------------------------

// flex-direction: row は「主軸 = inline 軸」なので、書字方向を変えても論理座標は同じ。
TEST(WritingModeProperty, FlexRow) {
  const auto make = [](bool vertical) {
    return std::vector<Tree>{
        flex(
            {
                block({text("あいう")},
                      [vertical](ComputedStyle& s) { set_inline_size(s, 60, vertical); }),
                block({text("かきくけこさしすせそ")}, [](ComputedStyle& s) { s.flex_grow = 1; }),
                block({text("たちつ")}, [](ComputedStyle& s) { s.flex_shrink = 0; }),
            },
            [](ComputedStyle& s) {
              s.flex_direction = style::FlexDirection::Row;
              s.column_gap = 8;
              s.justify_content = style::JustifyContent::SpaceBetween;
              s.align_items = style::AlignItems::Center;
            }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 300));
}

TEST(WritingModeProperty, FlexColumn) {
  const auto make = [](bool vertical) {
    return std::vector<Tree>{
        flex(
            {
                block({text("あいうえお")}),
                block({text(kJapanese)}),
                block({text("か")},
                      [vertical](ComputedStyle& s) { set_block_size(s, 30, vertical); }),
            },
            [](ComputedStyle& s) {
              s.flex_direction = style::FlexDirection::Column;
              s.row_gap = 6;
              s.align_items = style::AlignItems::FlexEnd;
            }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 220));
}

// 入れ子の flex（A29 のメモが書字方向で効き方を変えていないことも兼ねる）。
TEST(WritingModeProperty, NestedFlex) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        flex({flex({block({text("あいう")}), block({text("えおか")})},
                   [](ComputedStyle& s) {
                     s.flex_direction = style::FlexDirection::Column;
                     s.flex_grow = 1;
                   }),
              flex({block({text("きくけ")}), block({text("こさし")})},
                   [](ComputedStyle& s) { s.flex_grow = 2; })},
             [](ComputedStyle& s) { s.column_gap = 10; }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 240));
}

// ---- ルビ -------------------------------------------------------------------------

// 親文字がブロックと同じ font-size なら、行の高さ（block 方向）まで一致する。
// ルビ自身のベースラインは書字方向で別物なので比べない（許す差 (2)）。
// 行の高さだけは float の丸めで 1 ulp ずれる（24 に対して 23.999998。CompareOptions::tolerance）。
TEST(WritingModeProperty, Ruby) {
  const auto make = [](bool) {
    return std::vector<Tree>{
        block({
            text("この"),
            ruby({text("写植"), rt("しゃしょく")}),
            text("は"),
            ruby({text("日本語"), rt("にほんご")}),
            text("の組版に特化している。"),
        }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 160, CompareOptions{.tolerance = 1.0F / 1024}));
}

// ---- <img> ------------------------------------------------------------------------

// 画像は行の中の block 方向の揃え方が書字方向で違う（横書き = ベースライン揃え、
// 縦書き = 中心軸に中央揃え）ので、inline 方向だけを比べる（許す差 (4)）。
TEST(WritingModeProperty, InlineImage) {
  // 正方形にする: 固有寸法は物理なので、縦長だと inline 方向の大きさまで変わる（許す差 (5)）
  const ImageLookup images =
      image_table({ImageEntry{.src = "icon", .id = 7, .width = 32, .height = 32}});
  const auto make = [](bool vertical) {
    return std::vector<Tree>{
        block({
            text("あいうえお"),
            img("icon", std::nullopt, std::nullopt,
                [vertical](ComputedStyle& s) {
                  set_margin(s, LogicalSpace{.inline_start = 4, .inline_end = 4}, vertical);
                }),
            text("かきくけこさしすせそたちつてと"),
        }),
    };
  };
  EXPECT_TRUE(same_logical_tree(make, 150, CompareOptions{.block_axis = false},
                                /*sideways=*/false, &images));
}

// ---- 種を固定したランダムな木 -------------------------------------------------------

// 小さな文法からランダムに木を作る。同じ種からは同じ形が出るので、書字方向ごとに
// 引き直せば「同じ内容」を 2 通りに組める（ARCHITECTURE.md §4: ファジングは種を固定して
// 通常のテストに含める）。
class TreeMaker {
 public:
  TreeMaker(std::uint32_t seed, bool vertical) : rng_(seed), vertical_(vertical) {}

  std::vector<Tree> make() {
    std::vector<Tree> out;
    const int count = pick(1, 4);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      out.push_back(block_node(3));
    }
    return out;
  }

 private:
  int pick(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }

  std::string_view sentence() {
    constexpr std::array<std::string_view, 6> kTexts = {
        "あいうえお",
        "吾輩は猫である。名前はまだ無い。",
        "組版、それは「文字を並べる」仕事である。",
        "長い長い段落をここに置いて、行分割器が何度も折り返すようにしておく。",
        "ん",
        "きゃりーぱみゅぱみゅ",
    };
    return kTexts.at(static_cast<std::size_t>(pick(0, static_cast<int>(kTexts.size()) - 1)));
  }

  StyleFn text_style() {
    const float font_size = static_cast<float>(pick(3, 6)) * 4;
    const auto spacing = static_cast<float>(pick(0, 2));
    const auto align = static_cast<style::TextAlign>(pick(0, 5));
    return [font_size, spacing, align](ComputedStyle& s) {
      s.font_size = font_size;
      s.letter_spacing = spacing;
      s.text_align = align;
    };
  }

  Tree inline_node() {
    if (pick(0, 3) == 0) {
      return br();
    }
    if (pick(0, 2) == 0) {
      const float font_size = static_cast<float>(pick(3, 5)) * 4;
      return inline_box({text(sentence())},
                        [font_size](ComputedStyle& s) { s.font_size = font_size; });
    }
    return text(sentence());
  }

  Tree block_node(int depth) {
    std::vector<Tree> children;
    const int count = pick(1, 3);
    children.reserve(static_cast<std::size_t>(count));
    const bool leaf = depth <= 0 || pick(0, 1) == 0;
    for (int i = 0; i < count; ++i) {
      children.push_back(leaf ? inline_node() : block_node(depth - 1));
    }
    const float inline_size = static_cast<float>(pick(0, 8)) * 20;
    const LogicalSpace margin{.inline_start = static_cast<float>(pick(0, 3)) * 4,
                              .block_start = static_cast<float>(pick(0, 3)) * 4,
                              .block_end = static_cast<float>(pick(0, 3)) * 4};
    const LogicalSpace padding{.inline_start = static_cast<float>(pick(0, 2)) * 3,
                               .inline_end = static_cast<float>(pick(0, 2)) * 3};
    const bool as_flex = !leaf && pick(0, 3) == 0;
    const bool row = pick(0, 1) == 0;
    // `this` は捕まえない（Tree はこの TreeMaker より長生きする）
    const bool vertical = vertical_;
    StyleFn style = [vertical, inline_size, margin, padding, as_flex, row,
                     text = text_style()](ComputedStyle& s) {
      text(s);
      set_margin(s, margin, vertical);
      set_padding(s, padding, vertical);
      if (inline_size > 0) {
        set_inline_size(s, inline_size, vertical);
      }
      if (as_flex) {
        s.flex_direction = row ? style::FlexDirection::Row : style::FlexDirection::Column;
        s.column_gap = 6;
        s.row_gap = 4;
      }
    };
    return as_flex ? flex(std::move(children), std::move(style))
                   : block(std::move(children), std::move(style));
  }

  std::mt19937 rng_;
  bool vertical_ = false;
};

TEST(WritingModeProperty, RandomTrees) {
  for (std::uint32_t seed = 1; seed <= 40; ++seed) {
    const auto make = [seed](bool vertical) { return TreeMaker(seed, vertical).make(); };
    EXPECT_TRUE(same_logical_tree(make, 180)) << "seed = " << seed;
  }
}

}  // namespace
}  // namespace shashoku::layout::test
