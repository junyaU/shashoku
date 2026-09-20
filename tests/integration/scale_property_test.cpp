// 性質テスト（issue #10-2b）: scale を変えても「同じ絵の倍率違い」になる。
//
// パイプラインのどこから scale が効くかで、言えることが 3 段階に分かれる:
//
//   ① ボックスツリーとディスプレイリストは CSS px のまま（layout / paint は scale を
//      受け取らない。src/api/render.cpp の run_layout / build_display_list）。
//      → scale をいくつにしても**ダンプが 1 文字も変わらない**。
//   ② 矩形の塗りは「ピクセルと矩形の重なり面積」で被覆率を出す（rasterizer.cpp）。
//      辺が整数 CSS px なら scale = 2 でも辺は整数デバイス px なので被覆率は必ず 255。
//      → @2x の (2x, 2y)…(2x+1, 2y+1) の 4 ピクセルが @1x の (x, y) と**完全一致**する。
//   ③ グリフは A8 でデバイスピクセルの整数に丸め、FreeType には pixel_size = font-size
//      × scale で描かせる。@2x のグリフビットマップは @1x の 2 倍ではない（輪郭を別の
//      解像度でラスタライズし直したもの）ので、**ピクセルの一致は成り立たない**。
//      固定できるのは「墨の外接矩形がだいたい 2 倍に収まる」ところまで。
//
// ③ で固定できないもの（テストにしない理由）:
//   * グリフのピクセル値: 上のとおり別解像度のラスタライズなので一致しない。
//   * 墨の外接矩形の厳密な 2 倍: 原点の丸め（round(x*2) と 2*round(x) は最大 1 デバイス px
//     ずれる）と、FreeType の bitmap_left / top の切り捨て（floor(2b) と 2*floor(b) で
//     最大 1 ずれる）と、アンチエイリアスの端 1 px が重なるので、数 px の幅で囲むしかない
//     （実測 ±2 → kGlyphSlack = 3）。
//   * グリフの**デバイス原点**の丸め位置: ディスプレイリストから外には出ないので、
//     公開 API からは観測できない。観測できるのは「CSS px の原点が scale に依らない」
//     ことまで（GlyphOriginsLiveInCssPixels）。

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "integration/integration_support.hpp"
#include "shashoku/shashoku.hpp"

namespace shashoku::test {
namespace {

// 整数座標の矩形だけでできた HTML（テキストなし・角丸なし）。
constexpr std::string_view kRectsHtml = R"(
<style>
  .outer { width: 200px; height: 100px; padding: 10px; background-color: #123456; }
  .a { width: 50px; height: 20px; margin: 5px; background-color: #ff0000; }
  .b { width: 30px; height: 30px; border: 2px solid #0000ff; background-color: #00ff00; }
  .row { display: flex; gap: 8px; }
  .c { width: 24px; height: 16px; background-color: #ffff00; }
</style>
<div class="outer">
  <div class="a"></div>
  <div class="b"></div>
  <div class="row"><div class="c"></div><div class="c"></div></div>
</div>
)";

constexpr std::string_view kTextHtml = R"(
<style>
  body, div { font-size: 32px; line-height: 1.5; color: #102030; }
</style>
<div>組版、それは文字を並べる仕事。</div>
<div>shashoku 1234</div>
)";

RenderOptions rect_options(float scale) {
  RenderOptions options;
  options.viewport_width = 300;
  options.viewport_height = 220;
  options.scale = scale;
  return options;
}

std::string dump_stage(std::string_view html, float scale, DumpStage stage) {
  RenderOptions options = rect_options(scale);
  const auto dumped = dump(html, japanese_fonts(), ImageSet{}, options, stage);
  if (!dumped) {
    ADD_FAILURE() << "dump: " << to_string(dumped.error());
    return {};
  }
  return *dumped;
}

// 不透明なピクセルの外接矩形。[x0, x1) × [y0, y1)。空なら x1 <= x0。
struct InkBox {
  std::int64_t x0 = 0;
  std::int64_t y0 = 0;
  std::int64_t x1 = 0;
  std::int64_t y1 = 0;

  [[nodiscard]] bool empty() const { return x1 <= x0 || y1 <= y0; }
};

InkBox ink_box(const Bitmap& bitmap, std::uint8_t threshold) {
  InkBox box{.x0 = bitmap.width, .y0 = bitmap.height, .x1 = 0, .y1 = 0};
  for (std::uint32_t y = 0; y < bitmap.height; ++y) {
    for (std::uint32_t x = 0; x < bitmap.width; ++x) {
      if (bitmap.pixel(x, y).a <= threshold) {
        continue;
      }
      box.x0 = std::min<std::int64_t>(box.x0, x);
      box.y0 = std::min<std::int64_t>(box.y0, y);
      box.x1 = std::max<std::int64_t>(box.x1, std::int64_t{x} + 1);
      box.y1 = std::max<std::int64_t>(box.y1, std::int64_t{y} + 1);
    }
  }
  return box;
}

::testing::AssertionResult near_twice(std::string_view what, std::int64_t at_1x, std::int64_t at_2x,
                                      std::int64_t slack) {
  const std::int64_t difference = at_2x - (2 * at_1x);
  if (difference <= slack && -difference <= slack) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << what << ": @1x " << at_1x << " → 2 倍なら " << (2 * at_1x) << " だが @2x は " << at_2x
         << "（差 " << difference << "、許容 ±" << slack << "）";
}

// ---- ① CSS px の中間表現は scale に依らない -----------------------------------------

TEST(ScaleProperty, BoxTreeDoesNotDependOnScale) {
  const std::string at_1x = dump_stage(kTextHtml, 1.0F, DumpStage::Box);
  EXPECT_FALSE(at_1x.empty());
  EXPECT_EQ(at_1x, dump_stage(kTextHtml, 2.0F, DumpStage::Box));
  EXPECT_EQ(at_1x, dump_stage(kTextHtml, 0.5F, DumpStage::Box));
  EXPECT_EQ(at_1x, dump_stage(kTextHtml, 3.0F, DumpStage::Box));
}

TEST(ScaleProperty, DisplayListDoesNotDependOnScale) {
  for (const std::string_view html : {kRectsHtml, kTextHtml}) {
    const std::string at_1x = dump_stage(html, 1.0F, DumpStage::DisplayList);
    EXPECT_FALSE(at_1x.empty());
    EXPECT_EQ(at_1x, dump_stage(html, 2.0F, DumpStage::DisplayList));
    EXPECT_EQ(at_1x, dump_stage(html, 0.5F, DumpStage::DisplayList));
    EXPECT_EQ(at_1x, dump_stage(html, 3.0F, DumpStage::DisplayList));
  }
}

// ---- ② 整数座標の矩形は @2x で厳密に 2x2 ピクセルに広がる ------------------------------

TEST(ScaleProperty, IntegerRectanglesScaleExactly) {
  const FontSet fonts = japanese_fonts();
  const Bitmap at_1x = render_bitmap(kRectsHtml, fonts, rect_options(1.0F));
  const Bitmap at_2x = render_bitmap(kRectsHtml, fonts, rect_options(2.0F));
  ASSERT_GT(at_1x.width, 0U);
  ASSERT_EQ(at_2x.width, at_1x.width * 2);
  ASSERT_EQ(at_2x.height, at_1x.height * 2);

  std::uint64_t mismatches = 0;
  std::string first;
  for (std::uint32_t y = 0; y < at_1x.height; ++y) {
    for (std::uint32_t x = 0; x < at_1x.width; ++x) {
      const Color expected = at_1x.pixel(x, y);
      for (std::uint32_t dy = 0; dy < 2; ++dy) {
        for (std::uint32_t dx = 0; dx < 2; ++dx) {
          const Color actual = at_2x.pixel((2 * x) + dx, (2 * y) + dy);
          if (actual == expected) {
            continue;
          }
          ++mismatches;
          if (first.empty()) {
            first = "@1x (" + std::to_string(x) + ", " + std::to_string(y) + ") と @2x (" +
                    std::to_string((2 * x) + dx) + ", " + std::to_string((2 * y) + dy) + ")";
          }
        }
      }
    }
  }
  EXPECT_EQ(mismatches, 0U) << "最初の食い違い: " << first;
}

// ---- ③ グリフは「だいたい 2 倍」までしか言えない ---------------------------------------

// 墨の外接矩形は「2 倍の ±3 デバイス px」に収まる。ずれの出どころ:
//   原点の丸め round(x*2) − 2*round(x) で ±1、FreeType の bitmap_left / top の
//   切り捨て（floor(2b) と 2*floor(b)）で ±1、アンチエイリアスの端 1 px。
// 下の 6 ケースの実測は最大 ±2（16px 前後の小さい字の下端が @2x で 2 デバイス px
// 内側に入る = @1x 換算で 1 px）だったので、1 だけ余裕を持たせて 3 にする。
// 実測値はテストが落ちたときのメッセージに全辺ぶん出る。
constexpr std::int64_t kGlyphSlack = 3;

::testing::AssertionResult ink_scales(std::string_view html) {
  const FontSet fonts = japanese_fonts();
  RenderOptions options = rect_options(1.0F);
  options.viewport_height.reset();  // 高さは内容に追従（CSS px の整数に切り上げられる）
  const Bitmap at_1x = render_bitmap(html, fonts, options);
  options.scale = 2.0F;
  const Bitmap at_2x = render_bitmap(html, fonts, options);
  if (at_1x.width == 0 || at_2x.width != at_1x.width * 2 || at_2x.height != at_1x.height * 2) {
    // 出力の寸法はちょうど 2 倍になるはず（CSS px の寸法が整数なので ceil が効かない）
    return ::testing::AssertionFailure()
           << "出力の寸法が 2 倍ではない: @1x " << at_1x.width << "x" << at_1x.height << " / @2x "
           << at_2x.width << "x" << at_2x.height;
  }
  const InkBox box_1x = ink_box(at_1x, 0);
  const InkBox box_2x = ink_box(at_2x, 0);
  if (box_1x.empty() || box_2x.empty()) {
    return ::testing::AssertionFailure() << "墨が 1 ピクセルもない";
  }
  std::string failures;
  for (const auto& [what, v1, v2] :
       {std::tuple{"墨の左端", box_1x.x0, box_2x.x0}, std::tuple{"墨の上端", box_1x.y0, box_2x.y0},
        std::tuple{"墨の右端", box_1x.x1, box_2x.x1},
        std::tuple{"墨の下端", box_1x.y1, box_2x.y1}}) {
    if (const ::testing::AssertionResult result = near_twice(what, v1, v2, kGlyphSlack); !result) {
      failures += "\n  ";
      failures += result.message();
    }
  }
  if (failures.empty()) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << failures;
}

TEST(ScaleProperty, GlyphInkBoxIsRoughlyTwiceAsLarge) {
  EXPECT_TRUE(ink_scales(kTextHtml));
  EXPECT_TRUE(ink_scales(R"(<div style="font-size: 48px">あ</div>)"));
  EXPECT_TRUE(ink_scales(R"(<div style="font-size: 11px">あ</div>)"));
  EXPECT_TRUE(ink_scales(R"(<div style="font-size: 23px">shashoku</div>)"));
  EXPECT_TRUE(
      ink_scales(R"(<div style="font-size: 17px; letter-spacing: 2px">写植、組版。</div>)"));
  EXPECT_TRUE(ink_scales(
      R"(<div style="font-size: 20px"><ruby>写植<rt>しゃしょく</rt></ruby>の見本</div>)"));
}

// グリフの原点（= 行のベースラインとペン位置）はディスプレイリストの中では CSS px の
// ままなので、scale を変えても 1 ビットも動かない。デバイスピクセルへの丸め（A8）は
// ラスタライザの中だけで起きる。上の DisplayListDoesNotDependOnScale と合わせて、
// 「@2x でグリフの絵が変わるのはラスタライズだけが原因」と言い切れる。
TEST(ScaleProperty, GlyphOriginsLiveInCssPixels) {
  const std::string at_1x = dump_stage(kTextHtml, 1.0F, DumpStage::DisplayList);
  ASSERT_NE(at_1x.find("glyphs"), std::string::npos);
  EXPECT_EQ(at_1x, dump_stage(kTextHtml, 2.0F, DumpStage::DisplayList));
}

}  // namespace
}  // namespace shashoku::test
