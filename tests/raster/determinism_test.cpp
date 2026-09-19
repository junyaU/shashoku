// 決定性（DESIGN.md §3-5）と、種を固定した乱数によるランダム入力の性質テスト
// （ARCHITECTURE.md §4: ファジングは通常のテストに含める）。

#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"
#include "raster/raster_test_util.hpp"
#include "raster/rasterizer.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster::test {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

DisplayList busy_list() {
  DrawGlyphs run;
  run.font = 3;
  run.size = 8;
  run.color = rgba(0, 0, 255, 200);
  run.glyphs = {GlyphInstance{1, Point{2.3F, 9.5F}}, GlyphInstance{2, Point{8.7F, 9.5F}}};
  return DisplayList{
      FillRect{Rect{0, 0, 16, 16}, rgba(240, 240, 240, 255)},
      FillRoundedRect{Rect{1.25F, 1.75F, 13.5F, 12.25F}, 3.5F, rgba(255, 0, 0, 128)},
      StrokeRoundedRect{Rect{1.25F, 1.75F, 13.5F, 12.25F}, 3.5F, 1.5F, rgba(0, 128, 0, 160)},
      PushClip{Rect{2, 2, 12, 12}, 4},
      DrawImage{0, Rect{3.5F, 3.5F, 9.25F, 6.5F}},
      run,
      PopClip{},
  };
}

Target busy_target() {
  Target t;
  t.width = 16;
  t.height = 16;
  t.scale = 1.5F;
  t.background = rgba(0, 0, 0, 0);
  return t;
}

std::vector<Bitmap> busy_images() {
  std::vector<Color> pixels;
  pixels.reserve(16);
  for (int i = 0; i < 16; ++i) {
    pixels.push_back(rgba(i * 17, 255 - (i * 17), (i * 13) % 256, 64 + (i * 12)));
  }
  return std::vector<Bitmap>{make_image(4, 4, pixels)};
}

// GlyphSource はコピーも移動もできないので、参照を受け取って中身を用意する。
void setup_glyphs(FakeGlyphSource& glyphs) {
  glyphs.set(1, glyph_rows(1, 6, {{0, 128, 255}, {128, 255, 128}, {255, 128, 0}}));
  glyphs.set(2, solid_glyph(0, 5, 4, 5, 200));
}

TEST(RasterDeterminism, TheSameInputProducesTheSameBitmap) {
  const DisplayList list = busy_list();
  const Target target = busy_target();
  const std::vector<Bitmap> images = busy_images();

  FakeGlyphSource first_glyphs;
  setup_glyphs(first_glyphs);
  const Bitmap first = must_rasterize(list, target, first_glyphs, std::span<const Bitmap>(images));
  FakeGlyphSource second_glyphs;
  setup_glyphs(second_glyphs);
  const Bitmap second =
      must_rasterize(list, target, second_glyphs, std::span<const Bitmap>(images));

  EXPECT_EQ(first, second);
  EXPECT_EQ(first_glyphs.calls(), second_glyphs.calls());
  EXPECT_GT(coverage_sum(first), 0.0);  // 何かは描けている
}

// ---------------------------------------------------------------------------
// ランダム入力（種は固定）
// ---------------------------------------------------------------------------

class Generator {
 public:
  explicit Generator(std::uint32_t seed) : rng_(seed) {}

  float coordinate() {
    const int kind = pick(20);
    if (kind == 0) {
      return kNaN;
    }
    if (kind == 1) {
      return kInf;
    }
    if (kind == 2) {
      return -kInf;
    }
    if (kind == 3) {
      return 1.0e30F;
    }
    return std::uniform_real_distribution<float>(-8.0F, 24.0F)(rng_);
  }

  float size() { return std::uniform_real_distribution<float>(-2.0F, 20.0F)(rng_); }

  Color color() { return rgba(pick(256), pick(256), pick(256), pick(256)); }

  Rect rect() { return Rect{coordinate(), coordinate(), size(), size()}; }

  int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng_); }

 private:
  std::mt19937 rng_;
};

DrawCmd random_command(Generator& gen) {
  switch (gen.pick(7)) {
    case 0:
      return FillRect{gen.rect(), gen.color()};
    case 1:
      return FillRoundedRect{gen.rect(), gen.size(), gen.color()};
    case 2:
      return StrokeRoundedRect{gen.rect(), gen.size(), gen.size(), gen.color()};
    case 3: {
      DrawGlyphs run;
      run.font = static_cast<FontId>(gen.pick(3));
      run.size = gen.size();
      run.color = gen.color();
      run.sideways = gen.pick(2) == 1;
      const int count = gen.pick(4);
      for (int i = 0; i < count; ++i) {
        run.glyphs.push_back(GlyphInstance{static_cast<GlyphId>(gen.pick(4)),
                                           Point{gen.coordinate(), gen.coordinate()}});
      }
      return run;
    }
    case 4:
      return DrawImage{static_cast<ImageId>(gen.pick(3)), gen.rect()};
    case 5:
      return PushClip{gen.rect(), gen.size()};
    default:
      return PopClip{};
  }
}

// 落ちないこと（ASan で意味を持つ）。成功するか Internal エラーになるかのどちらか。
TEST(RasterDeterminism, RandomDisplayListsDoNotCrash) {
  Generator gen(20260919U);
  const std::vector<Bitmap> images = busy_images();
  for (int iteration = 0; iteration < 300; ++iteration) {
    DisplayList list;
    const int count = gen.pick(24);
    list.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      list.push_back(random_command(gen));
    }
    Target target;
    target.width = 1.0F + static_cast<float>(gen.pick(12));
    target.height = 1.0F + static_cast<float>(gen.pick(12));
    target.scale = 0.5F + (static_cast<float>(gen.pick(5)) / 2.0F);
    target.background = gen.color();

    FakeGlyphSource glyphs;
    setup_glyphs(glyphs);
    const Result<Bitmap> result = rasterize(list, target, glyphs, std::span<const Bitmap>(images));
    if (result) {
      EXPECT_EQ(result->rgba.size(), static_cast<std::size_t>(result->width) * result->height * 4);
      // 同じ入力なら同じ出力。
      FakeGlyphSource again;
      setup_glyphs(again);
      const Result<Bitmap> repeat = rasterize(list, target, again, std::span<const Bitmap>(images));
      ASSERT_TRUE(repeat.has_value());
      EXPECT_EQ(*result, *repeat) << "iteration " << iteration;
    } else {
      // 失敗するのは対応の取れていない PushClip / PopClip と範囲外の画像 ID だけ。
      EXPECT_EQ(result.error().kind, ErrorKind::Internal) << "iteration " << iteration;
    }
  }
}

}  // namespace
}  // namespace shashoku::raster::test
