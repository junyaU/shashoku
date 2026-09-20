#include "raster/rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/bitmap.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/display_list.hpp"
#include "raster/glyph_source.hpp"
#include "shashoku/error.hpp"

namespace shashoku::raster {
namespace {

// ===========================================================================
// 8bit 整数の合成。丸めはすべて「四捨五入」に統一する（ARCHITECTURE.md §3.3）。
// ===========================================================================

// x / 255 の四捨五入。x <= 255 * 255 を前提とする。
constexpr std::uint32_t div255_round(std::uint32_t x) { return (x + 127U) / 255U; }

// source-over（ストレートアルファ・sRGB 空間のまま）を 1 ピクセルに適用する。
// src_alpha は「被覆率 × クリップ × 色のアルファ」を掛け合わせた実効アルファ（1..255）。
//
//   out_a = sa + da * (1 - sa)
//   out_c = (sc * sa + dc * da * (1 - sa)) / out_a
void blend_over(std::uint8_t* px, Color src, std::uint32_t src_alpha) {
  const std::uint32_t dst_alpha = px[3];
  // 背景が残る量 = da * (1 - sa)。
  const std::uint32_t keep = div255_round(dst_alpha * (255U - src_alpha));
  const std::uint32_t out_alpha = src_alpha + keep;
  if (out_alpha == 0U) {
    // 呼び出し側が src_alpha > 0 を保証するので通らない。ゼロ除算に対する防御。
    px[0] = 0;
    px[1] = 0;
    px[2] = 0;
    px[3] = 0;
    return;
  }
  const std::uint32_t half = out_alpha / 2U;  // 四捨五入のための加算項
  const std::uint32_t dst_r = px[0];
  const std::uint32_t dst_g = px[1];
  const std::uint32_t dst_b = px[2];
  px[0] = static_cast<std::uint8_t>(((src.r * src_alpha) + (dst_r * keep) + half) / out_alpha);
  px[1] = static_cast<std::uint8_t>(((src.g * src_alpha) + (dst_g * keep) + half) / out_alpha);
  px[2] = static_cast<std::uint8_t>(((src.b * src_alpha) + (dst_b * keep) + half) / out_alpha);
  px[3] = static_cast<std::uint8_t>(out_alpha);
}

// 面積比（0..1）→ 被覆率（0..255）。NaN は 0 に落とす。
std::uint32_t coverage_from_area(float area) {
  if (!(area > 0.0F)) {
    return 0U;
  }
  if (area >= 1.0F) {
    return 255U;
  }
  return static_cast<std::uint32_t>(std::round(area * 255.0F));
}

// ===========================================================================
// 幾何
// ===========================================================================

bool finite(float v) { return std::isfinite(v); }

bool finite(const Rect& r) {
  return finite(r.x) && finite(r.y) && finite(r.width) && finite(r.height);
}

// 区間 [a0, a1) と [b0, b1) の重なりの長さ。
float overlap(float a0, float a1, float b0, float b1) {
  return std::max(0.0F, std::min(a1, b1) - std::max(a0, b0));
}

// float → int32。範囲外と NaN は [lo, hi] に丸め込む（未定義動作を避ける）。
std::int32_t floor_clamped(float v, std::int32_t lo, std::int32_t hi) {
  if (!(v > static_cast<float>(lo))) {
    return lo;
  }
  if (v >= static_cast<float>(hi)) {
    return hi;
  }
  return static_cast<std::int32_t>(std::floor(v));
}

std::int32_t ceil_clamped(float v, std::int32_t lo, std::int32_t hi) {
  if (!(v > static_cast<float>(lo))) {
    return lo;
  }
  if (v >= static_cast<float>(hi)) {
    return hi;
  }
  return static_cast<std::int32_t>(std::ceil(v));
}

// デバイスピクセル座標の矩形（左上 - 右下）。
struct DevRect {
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
};

DevRect to_device(const Rect& r, float scale) {
  return DevRect{r.x * scale, r.y * scale, r.right() * scale, r.bottom() * scale};
}

// 整数のピクセル範囲 [x0, x1) × [y0, y1)。
struct IntBox {
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t x1 = 0;
  std::int32_t y1 = 0;

  [[nodiscard]] bool empty() const { return x1 <= x0 || y1 <= y0; }
  [[nodiscard]] std::int32_t width() const { return x1 - x0; }
};

IntBox intersect(const IntBox& a, const IntBox& b) {
  return IntBox{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1),
                std::min(a.y1, b.y1)};
}

// ===========================================================================
// 塗る形
//
// 矩形・角丸矩形・枠線・クリップをこの 1 つの型で表す。inner が有効なら
// 「外周の被覆率 − 内周の被覆率」を塗る（display_list.hpp の StrokeRoundedRect のコメント）。
// ===========================================================================

struct Shape {
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  float radius = 0;
  bool has_inner = false;
  float inner_x0 = 0;
  float inner_y0 = 0;
  float inner_x1 = 0;
  float inner_y1 = 0;
  float inner_radius = 0;
};

// 半径を min(w, h) / 2 に切り詰める（display_list.hpp）。
float clamp_radius(float r, float width, float height) {
  const float limit = std::max(0.0F, std::min(width, height) / 2.0F);
  return std::min(std::max(r, 0.0F), limit);
}

Shape make_fill(const DevRect& d, float radius) {
  Shape s;
  s.x0 = d.x0;
  s.y0 = d.y0;
  s.x1 = d.x1;
  s.y1 = d.y1;
  s.radius = clamp_radius(radius, d.x1 - d.x0, d.y1 - d.y0);
  return s;
}

Shape make_stroke(const DevRect& d, float radius, float width) {
  Shape s = make_fill(d, radius);
  const float w = std::max(0.0F, width);
  const float inner_x0 = d.x0 + w;
  const float inner_y0 = d.y0 + w;
  const float inner_x1 = d.x1 - w;
  const float inner_y1 = d.y1 - w;
  // 内周が潰れるほど太い枠線は、外周の塗りつぶしになる。
  if (inner_x1 <= inner_x0 || inner_y1 <= inner_y0) {
    return s;
  }
  s.has_inner = true;
  s.inner_x0 = inner_x0;
  s.inner_y0 = inner_y0;
  s.inner_x1 = inner_x1;
  s.inner_y1 = inner_y1;
  s.inner_radius =
      clamp_radius(std::max(0.0F, radius - w), inner_x1 - inner_x0, inner_y1 - inner_y0);
  return s;
}

// ===========================================================================
// 被覆率の生成
//
// 半径 0 の形は「ピクセルと矩形の重なり面積」で厳密に求める。だから整数座標の矩形は
// 必ず被覆率 255 になり、半径 0 の FillRoundedRect は FillRect と 1 ビットも違わない。
// 半径つきの形は、1 ピクセル行を 16 本の水平サンプル線に割り、各線で角丸の輪郭との
// 交差区間を解析的に求めて足し合わせる（x 方向は厳密、y 方向は 16 分割）。sqrt しか
// 使わないので A9 を満たし、サンプル位置は行の中心について対称なので鏡像も一致する。
// ===========================================================================

constexpr std::int32_t kSubSamples = 16;
constexpr float kSubWeight = 1.0F / static_cast<float>(kSubSamples);

struct Span {
  float lo = 0;
  float hi = 0;
};

// 角丸矩形を水平線 y で切った区間。交わらなければ nullopt。
std::optional<Span> rounded_span(float x0, float y0, float x1, float y1, float radius, float y) {
  if (y <= y0 || y >= y1 || x1 <= x0) {
    return std::nullopt;
  }
  float dy = 0.0F;
  if (y < y0 + radius) {
    dy = (y0 + radius) - y;
  } else if (y > y1 - radius) {
    dy = y - (y1 - radius);
  }
  float inset = 0.0F;
  if (dy > 0.0F) {
    const float d2 = (radius * radius) - (dy * dy);
    inset = radius - (d2 > 0.0F ? std::sqrt(d2) : 0.0F);
  }
  const Span span{x0 + inset, x1 - inset};
  if (span.hi <= span.lo) {
    return std::nullopt;
  }
  return span;
}

// 区間 [lo, hi) の被覆率を weight 倍して行バッファに足す。
void add_span(std::vector<float>& row, std::int32_t x_begin, float lo, float hi, float weight) {
  if (!(hi > lo)) {
    return;
  }
  const auto count = static_cast<std::int32_t>(row.size());
  const auto origin = static_cast<float>(x_begin);
  const std::int32_t first = floor_clamped(lo - origin, 0, count);
  const std::int32_t last = ceil_clamped(hi - origin, 0, count);
  for (std::int32_t i = first; i < last; ++i) {
    const float fx = origin + static_cast<float>(i);
    const float ov = overlap(fx, fx + 1.0F, lo, hi);
    if (ov > 0.0F) {
      row[static_cast<std::size_t>(i)] += ov * weight;
    }
  }
}

// 半径 0（角丸なし）の行。ピクセルと矩形の重なり面積そのもの。
void scan_row_straight(const Shape& s, std::int32_t x_begin, std::int32_t y,
                       std::vector<float>& row) {
  const auto fy = static_cast<float>(y);
  const float outer_y = overlap(fy, fy + 1.0F, s.y0, s.y1);
  const float inner_y = s.has_inner ? overlap(fy, fy + 1.0F, s.inner_y0, s.inner_y1) : 0.0F;
  for (std::size_t i = 0; i < row.size(); ++i) {
    const float fx = static_cast<float>(x_begin) + static_cast<float>(i);
    float area = outer_y * overlap(fx, fx + 1.0F, s.x0, s.x1);
    if (s.has_inner) {
      area -= inner_y * overlap(fx, fx + 1.0F, s.inner_x0, s.inner_x1);
    }
    row[i] = area;
  }
}

// 半径つきの行。16 本の水平サンプル線で積分する。
void scan_row_rounded(const Shape& s, std::int32_t x_begin, std::int32_t y,
                      std::vector<float>& row) {
  std::ranges::fill(row, 0.0F);
  for (std::int32_t k = 0; k < kSubSamples; ++k) {
    // サンプル位置は 1/32, 3/32, ..., 31/32。行の中心について対称なので鏡像が一致する。
    const float ys = static_cast<float>(y) + (static_cast<float>((2 * k) + 1) / 32.0F);
    const std::optional<Span> outer = rounded_span(s.x0, s.y0, s.x1, s.y1, s.radius, ys);
    if (!outer.has_value()) {
      continue;
    }
    const std::optional<Span> inner = s.has_inner ? rounded_span(s.inner_x0, s.inner_y0, s.inner_x1,
                                                                 s.inner_y1, s.inner_radius, ys)
                                                  : std::nullopt;
    if (!inner.has_value()) {
      add_span(row, x_begin, outer->lo, outer->hi, kSubWeight);
      continue;
    }
    // 外周から内周を引いた 2 つの区間。角が二重に塗られないよう、被覆率の引き算で表す。
    add_span(row, x_begin, outer->lo, std::min(inner->lo, outer->hi), kSubWeight);
    add_span(row, x_begin, std::max(inner->hi, outer->lo), outer->hi, kSubWeight);
  }
}

// Shape の被覆率を 1 行ずつ作り、fn(y, x_begin, 被覆率) に渡す。
template <class Fn>
void scan_shape(const Shape& s, const IntBox& box, std::vector<float>& row, Fn fn) {
  row.assign(static_cast<std::size_t>(box.width()), 0.0F);
  const bool straight = s.radius <= 0.0F;
  for (std::int32_t y = box.y0; y < box.y1; ++y) {
    if (straight) {
      scan_row_straight(s, box.x0, y, row);
    } else {
      scan_row_rounded(s, box.x0, y, row);
    }
    fn(y, box.x0, std::span<const float>(row));
  }
}

// ===========================================================================
// 画像のサンプリング
//
// デバイスピクセル 1 個の逆像を「幅 max(1, 1/scale) の箱」として平均する。
// 縮小では逆像そのもの（= 面積平均）、拡大では幅 1 の箱平均になり、後者は
// ピクセル中心どうしの線形補間（バイリニア）と厳密に一致する。等倍・整数位置なら
// 箱がソースの 1 ピクセルにぴったり重なるので、値はそのまま通る。
// 平均は乗算済みアルファで行う（透明ピクセルの RGB が滲み出さないように）。
// ===========================================================================

class ImageSampler {
 public:
  ImageSampler(const Bitmap& image, const DevRect& dest)
      : image_(&image),
        dest_x_(dest.x0),
        dest_y_(dest.y0),
        inv_x_(static_cast<float>(image.width) / (dest.x1 - dest.x0)),
        inv_y_(static_cast<float>(image.height) / (dest.y1 - dest.y0)),
        half_x_(std::max(1.0F, inv_x_) / 2.0F),
        half_y_(std::max(1.0F, inv_y_) / 2.0F) {}

  [[nodiscard]] Color sample(std::int32_t px, std::int32_t py) const {
    const float cx = (static_cast<float>(px) + 0.5F - dest_x_) * inv_x_;
    const float cy = (static_cast<float>(py) + 0.5F - dest_y_) * inv_y_;
    const auto w = static_cast<std::int32_t>(image_->width);
    const auto h = static_cast<std::int32_t>(image_->height);
    const float u0 = std::max(cx - half_x_, 0.0F);
    const float u1 = std::min(cx + half_x_, static_cast<float>(w));
    const float v0 = std::max(cy - half_y_, 0.0F);
    const float v1 = std::min(cy + half_y_, static_cast<float>(h));
    if (u1 <= u0 || v1 <= v0) {
      return kTransparent;
    }
    return average(u0, u1, v0, v1, floor_clamped(u0, 0, w), ceil_clamped(u1, 0, w),
                   floor_clamped(v0, 0, h), ceil_clamped(v1, 0, h));
  }

 private:
  // 乗算済みアルファでの重みつき平均。戻り値はストレートアルファ。
  [[nodiscard]] Color average(float u0, float u1, float v0, float v1, std::int32_t i0,
                              std::int32_t i1, std::int32_t j0, std::int32_t j1) const {
    float sum_r = 0.0F;
    float sum_g = 0.0F;
    float sum_b = 0.0F;
    float sum_a = 0.0F;
    float sum_w = 0.0F;
    for (std::int32_t j = j0; j < j1; ++j) {
      const auto fy = static_cast<float>(j);
      const float wy = overlap(fy, fy + 1.0F, v0, v1);
      if (!(wy > 0.0F)) {
        continue;
      }
      for (std::int32_t i = i0; i < i1; ++i) {
        const auto fx = static_cast<float>(i);
        const float wx = overlap(fx, fx + 1.0F, u0, u1);
        if (!(wx > 0.0F)) {
          continue;
        }
        const float weight = wx * wy;
        const Color c = image_->pixel(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j));
        const float alpha = weight * static_cast<float>(c.a);
        sum_r += alpha * static_cast<float>(c.r);
        sum_g += alpha * static_cast<float>(c.g);
        sum_b += alpha * static_cast<float>(c.b);
        sum_a += alpha;
        sum_w += weight;
      }
    }
    if (!(sum_w > 0.0F) || !(sum_a > 0.0F)) {
      return kTransparent;
    }
    Color out;
    out.r = to_byte(sum_r / sum_a);
    out.g = to_byte(sum_g / sum_a);
    out.b = to_byte(sum_b / sum_a);
    out.a = to_byte(sum_a / sum_w);
    return out;
  }

  static std::uint8_t to_byte(float v) {
    return static_cast<std::uint8_t>(std::round(std::min(std::max(v, 0.0F), 255.0F)));
  }

  const Bitmap* image_;
  float dest_x_;
  float dest_y_;
  float inv_x_;
  float inv_y_;
  float half_x_;
  float half_y_;
};

// ===========================================================================
// キャンバス
// ===========================================================================

// クリップの被覆率マスク。box の外は 0。入れ子は親との積なので、常に一番上だけ見ればよい。
struct ClipMask {
  IntBox box;
  std::vector<std::uint8_t> coverage;
};

// グリフ原点をこの絶対値までしか受け付けない（int64 への変換をはみ出させないため）。
constexpr float kMaxOrigin = 1.0e9F;

std::string to_text(float v) { return std::to_string(v); }

// std::visit 用のオーバーロード集合。
template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

class Canvas {
 public:
  Canvas(std::uint32_t width, std::uint32_t height, Color background)
      : bitmap_(width, height, background),
        width_(static_cast<std::int32_t>(width)),
        height_(static_cast<std::int32_t>(height)) {}

  Result<void> run(const DisplayList& list, float scale, GlyphSource& glyphs,
                   std::span<const Bitmap> images);

  Bitmap& bitmap() { return bitmap_; }

 private:
  // 唯一の書き込み口。被覆率 × クリップ × 色のアルファ → 実効アルファ。
  void blend_pixel(std::int32_t x, std::int32_t y, Color color, std::uint32_t coverage);

  [[nodiscard]] std::uint32_t clip_coverage(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] IntBox target_box() const { return IntBox{0, 0, width_, height_}; }
  [[nodiscard]] IntBox pixel_box(const Shape& s) const;

  void fill_shape(const Shape& s, Color color);
  Result<void> run_command(const DrawCmd& cmd, float scale, GlyphSource& glyphs,
                           std::span<const Bitmap> images);
  Result<void> push_clip(const PushClip& cmd, float scale);
  Result<void> pop_clip();
  Result<void> draw_glyphs(const DrawGlyphs& cmd, float scale, GlyphSource& glyphs);
  Result<void> draw_image(const DrawImage& cmd, float scale, std::span<const Bitmap> images);
  void blit_glyph(const GlyphBitmap& bmp, std::int64_t left, std::int64_t top, Color color);

  Bitmap bitmap_;
  std::int32_t width_;
  std::int32_t height_;
  std::vector<ClipMask> clips_;
  std::vector<float> row_;  // 被覆率のスクラッチ（1 行ぶん）
};

std::uint32_t mask_coverage(const ClipMask& mask, std::int32_t x, std::int32_t y) {
  if (x < mask.box.x0 || x >= mask.box.x1 || y < mask.box.y0 || y >= mask.box.y1) {
    return 0U;
  }
  const auto stride = static_cast<std::size_t>(mask.box.width());
  const auto index = (static_cast<std::size_t>(y - mask.box.y0) * stride) +
                     static_cast<std::size_t>(x - mask.box.x0);
  return mask.coverage[index];
}

std::uint32_t Canvas::clip_coverage(std::int32_t x, std::int32_t y) const {
  return mask_coverage(clips_.back(), x, y);
}

void Canvas::blend_pixel(std::int32_t x, std::int32_t y, Color color, std::uint32_t coverage) {
  if (coverage == 0U || x < 0 || y < 0 || x >= width_ || y >= height_) {
    return;
  }
  std::uint32_t cov = coverage;
  if (!clips_.empty()) {
    cov = div255_round(cov * clip_coverage(x, y));
  }
  const std::uint32_t alpha = div255_round(cov * color.a);
  if (alpha == 0U) {
    return;
  }
  const std::size_t off =
      bitmap_.offset(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
  blend_over(bitmap_.rgba.data() + off, color, alpha);
}

IntBox Canvas::pixel_box(const Shape& s) const {
  const IntBox box{floor_clamped(s.x0, 0, width_), floor_clamped(s.y0, 0, height_),
                   ceil_clamped(s.x1, 0, width_), ceil_clamped(s.y1, 0, height_)};
  return intersect(box, target_box());
}

void Canvas::fill_shape(const Shape& s, Color color) {
  const IntBox box = pixel_box(s);
  if (box.empty()) {
    return;
  }
  scan_shape(s, box, row_, [&](std::int32_t y, std::int32_t x_begin, std::span<const float> cov) {
    for (std::size_t i = 0; i < cov.size(); ++i) {
      blend_pixel(x_begin + static_cast<std::int32_t>(i), y, color, coverage_from_area(cov[i]));
    }
  });
}

Result<void> Canvas::push_clip(const PushClip& cmd, float scale) {
  IntBox box{0, 0, 0, 0};
  Shape shape;
  if (finite(cmd.rect) && finite(cmd.radius) && !cmd.rect.empty()) {
    shape = make_fill(to_device(cmd.rect, scale), cmd.radius * scale);
    box = pixel_box(shape);
    if (!clips_.empty()) {
      box = intersect(box, clips_.back().box);
    }
  }
  // 非有限・空の矩形は「何も通さないクリップ」になる（空の box）。
  if (box.empty()) {
    clips_.push_back(ClipMask{IntBox{0, 0, 0, 0}, {}});
    return {};
  }
  ClipMask mask;
  mask.box = box;
  mask.coverage.assign(
      static_cast<std::size_t>(box.width()) * static_cast<std::size_t>(box.y1 - box.y0), 0U);
  const bool nested = !clips_.empty();
  scan_shape(shape, box, row_,
             [&](std::int32_t y, std::int32_t x_begin, std::span<const float> cov) {
               const auto row_off =
                   static_cast<std::size_t>(y - box.y0) * static_cast<std::size_t>(box.width());
               for (std::size_t i = 0; i < cov.size(); ++i) {
                 const std::int32_t x = x_begin + static_cast<std::int32_t>(i);
                 std::uint32_t c = coverage_from_area(cov[i]);
                 if (nested && c != 0U) {
                   c = div255_round(c * clip_coverage(x, y));  // 入れ子は積
                 }
                 mask.coverage[row_off + static_cast<std::size_t>(x - box.x0)] =
                     static_cast<std::uint8_t>(c);
               }
             });
  clips_.push_back(std::move(mask));
  return {};
}

Result<void> Canvas::pop_clip() {
  if (clips_.empty()) {
    return fail(ErrorKind::Internal, "raster: PopClip without a matching PushClip");
  }
  clips_.pop_back();
  return {};
}

void Canvas::blit_glyph(const GlyphBitmap& bmp, std::int64_t left, std::int64_t top, Color color) {
  const auto bw = static_cast<std::int64_t>(bmp.width);
  const auto bh = static_cast<std::int64_t>(bmp.height);
  const std::int64_t x0 = std::max<std::int64_t>(left, 0);
  const std::int64_t y0 = std::max<std::int64_t>(top, 0);
  const std::int64_t x1 = std::min<std::int64_t>(left + bw, width_);
  const std::int64_t y1 = std::min<std::int64_t>(top + bh, height_);
  for (std::int64_t y = y0; y < y1; ++y) {
    const auto row_off = static_cast<std::size_t>(y - top) * static_cast<std::size_t>(bw);
    for (std::int64_t x = x0; x < x1; ++x) {
      const std::uint32_t cov = bmp.coverage[row_off + static_cast<std::size_t>(x - left)];
      blend_pixel(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), color, cov);
    }
  }
}

Result<void> Canvas::draw_glyphs(const DrawGlyphs& cmd, float scale, GlyphSource& glyphs) {
  if (!finite(cmd.size) || cmd.size <= 0.0F) {
    return {};
  }
  const float pixel_size = cmd.size * scale;
  if (!finite(pixel_size) || pixel_size <= 0.0F) {
    return {};  // size * scale があふれた（非有限の寸法を持つコマンドは無視する）
  }
  for (const GlyphInstance& g : cmd.glyphs) {
    if (!finite(g.origin.x) || !finite(g.origin.y)) {
      continue;
    }
    // A8: グリフ位置はデバイスピクセルの整数に四捨五入する。
    const float ox = std::round(g.origin.x * scale);
    const float oy = std::round(g.origin.y * scale);
    if (std::fabs(ox) > kMaxOrigin || std::fabs(oy) > kMaxOrigin) {
      continue;
    }
    // 失敗は握りつぶさずに伝播する（glyph_source.hpp: 空のビットマップは「描くものが
    // ないグリフ」だけを意味する。issue #3）。
    const Result<GlyphBitmap> bmp =
        glyphs.rasterize(cmd.font, g.glyph_id, pixel_size, cmd.sideways);
    if (!bmp) {
      return std::unexpected(bmp.error());
    }
    const std::size_t expected =
        static_cast<std::size_t>(bmp->width) * static_cast<std::size_t>(bmp->height);
    if (bmp->coverage.size() != expected) {
      return fail(ErrorKind::Internal,
                  "raster: GlyphSource returned a malformed bitmap for glyph " +
                      std::to_string(g.glyph_id) + " (coverage " +
                      std::to_string(bmp->coverage.size()) + " bytes, expected " +
                      std::to_string(expected) + ")");
    }
    if (bmp->width == 0U || bmp->height == 0U) {
      continue;  // 空白グリフ（成功して空のビットマップ）
    }
    // glyph_source.hpp: 左上ピクセルのデバイス座標は (origin.x + left, origin.y - top)。
    blit_glyph(*bmp, static_cast<std::int64_t>(ox) + bmp->left,
               static_cast<std::int64_t>(oy) - bmp->top, cmd.color);
  }
  return {};
}

Result<void> Canvas::draw_image(const DrawImage& cmd, float scale, std::span<const Bitmap> images) {
  if (cmd.image >= images.size()) {
    return fail(ErrorKind::Internal, "raster: DrawImage refers to image " +
                                         std::to_string(cmd.image) + " but only " +
                                         std::to_string(images.size()) + " were given");
  }
  const Bitmap& image = images[cmd.image];
  const std::size_t expected =
      static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U;
  if (image.rgba.size() != expected) {
    return fail(ErrorKind::Internal, "raster: image " + std::to_string(cmd.image) +
                                         " is malformed (rgba " +
                                         std::to_string(image.rgba.size()) + " bytes, expected " +
                                         std::to_string(expected) + ")");
  }
  if (image.width == 0U || image.height == 0U || !finite(cmd.dest) || cmd.dest.empty()) {
    return {};
  }
  const DevRect dest = to_device(cmd.dest, scale);
  const Shape shape = make_fill(dest, 0.0F);
  const IntBox box = pixel_box(shape);
  if (box.empty() || dest.x1 <= dest.x0 || dest.y1 <= dest.y0) {
    return {};
  }
  const ImageSampler sampler(image, dest);
  scan_shape(shape, box, row_,
             [&](std::int32_t y, std::int32_t x_begin, std::span<const float> cov) {
               for (std::size_t i = 0; i < cov.size(); ++i) {
                 const std::uint32_t c = coverage_from_area(cov[i]);
                 if (c == 0U) {
                   continue;
                 }
                 const std::int32_t x = x_begin + static_cast<std::int32_t>(i);
                 blend_pixel(x, y, sampler.sample(x, y), c);
               }
             });
  return {};
}

Result<void> Canvas::run_command(const DrawCmd& cmd, float scale, GlyphSource& glyphs,
                                 std::span<const Bitmap> images) {
  return std::visit(
      Overloaded{
          [&](const FillRect& c) -> Result<void> {
            if (finite(c.rect) && !c.rect.empty()) {
              fill_shape(make_fill(to_device(c.rect, scale), 0.0F), c.color);
            }
            return {};
          },
          [&](const FillRoundedRect& c) -> Result<void> {
            if (finite(c.rect) && finite(c.radius) && !c.rect.empty()) {
              fill_shape(make_fill(to_device(c.rect, scale), c.radius * scale), c.color);
            }
            return {};
          },
          [&](const StrokeRoundedRect& c) -> Result<void> {
            if (finite(c.rect) && finite(c.radius) && finite(c.width) && !c.rect.empty() &&
                c.width > 0.0F) {
              fill_shape(make_stroke(to_device(c.rect, scale), c.radius * scale, c.width * scale),
                         c.color);
            }
            return {};
          },
          [&](const DrawGlyphs& c) { return draw_glyphs(c, scale, glyphs); },
          [&](const DrawImage& c) { return draw_image(c, scale, images); },
          [&](const PushClip& c) { return push_clip(c, scale); },
          [&](const PopClip&) { return pop_clip(); },
      },
      cmd);
}

Result<void> Canvas::run(const DisplayList& list, float scale, GlyphSource& glyphs,
                         std::span<const Bitmap> images) {
  for (const DrawCmd& cmd : list) {
    if (Result<void> r = run_command(cmd, scale, glyphs, images); !r) {
      return r;
    }
  }
  if (!clips_.empty()) {
    return fail(ErrorKind::Internal, "raster: " + std::to_string(clips_.size()) +
                                         " PushClip command(s) were never popped");
  }
  return {};
}

// ===========================================================================
// ターゲットの検証
// ===========================================================================

// 1 辺の**絶対**上限。Bitmap::width / height が std::uint32_t なので、これを超えると
// 型に収まらない（実装の都合による上限で、Target::max_device_pixels では緩められない）。
constexpr std::int64_t kMaxDeviceSide = 0xFFFFFFFFLL;

Result<std::pair<std::uint32_t, std::uint32_t>> device_size(const Target& target) {
  if (!finite(target.scale) || target.scale <= 0.0F) {
    return fail(ErrorKind::InvalidOption, "raster: scale must be a positive finite number (got " +
                                              to_text(target.scale) + ")");
  }
  if (!finite(target.width) || !finite(target.height) || target.width <= 0.0F ||
      target.height <= 0.0F) {
    return fail(ErrorKind::InvalidOption, "raster: target size must be positive (got " +
                                              to_text(target.width) + " x " +
                                              to_text(target.height) + " CSS px)");
  }
  const float fw = std::ceil(target.width * target.scale);
  const float fh = std::ceil(target.height * target.scale);
  const auto side_limit = static_cast<float>(kMaxDeviceSide);
  if (fw > side_limit || fh > side_limit) {
    return fail(ErrorKind::LimitExceeded, "raster: device size " + to_text(fw) + " x " +
                                              to_text(fh) + " px exceeds the absolute limit of " +
                                              std::to_string(kMaxDeviceSide) + " px per side");
  }
  const auto w = static_cast<std::int64_t>(fw);
  const auto h = static_cast<std::int64_t>(fh);
  if (w < 1 || h < 1) {
    return fail(ErrorKind::InvalidOption, "raster: device size rounds down to " +
                                              std::to_string(w) + " x " + std::to_string(h) +
                                              " px");
  }
  // ピクセルバッファを確保する前に判定する（A25 の検査点 (c)）。
  const std::uint64_t pixels = static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
  if (pixels > target.max_device_pixels) {
    return fail(ErrorKind::LimitExceeded, "raster: device size " + std::to_string(w) + " x " +
                                              std::to_string(h) + " = " + std::to_string(pixels) +
                                              " px exceeds the limit of " +
                                              std::to_string(target.max_device_pixels) + " pixels");
  }
  // 絶対上限: Bitmap::rgba は 1 画素 4 バイトなので、画素数 x 4 が std::size_t に収まること。
  // max_device_pixels を極端に緩めたときに、掛け算が黙って一周するのを防ぐ。
  if (pixels > std::numeric_limits<std::size_t>::max() / 4) {
    return fail(ErrorKind::LimitExceeded,
                "raster: device size " + std::to_string(w) + " x " + std::to_string(h) + " = " +
                    std::to_string(pixels) +
                    " px exceeds the absolute limit of size_t/4 pixels (4 bytes per pixel)");
  }
  return std::pair{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
}

// アルファ 0 のピクセルの RGB は 0 に正規化する（決定性のため）。
Color normalized(Color c) { return c.a == 0 ? kTransparent : c; }

}  // namespace

Result<Bitmap> rasterize(const DisplayList& list, const Target& target, GlyphSource& glyphs,
                         std::span<const Bitmap> images) {
  const Result<std::pair<std::uint32_t, std::uint32_t>> size = device_size(target);
  if (!size) {
    return std::unexpected(size.error());
  }
  Canvas canvas(size->first, size->second, normalized(target.background));
  if (Result<void> r = canvas.run(list, target.scale, glyphs, images); !r) {
    return std::unexpected(r.error());
  }
  return std::move(canvas.bitmap());
}

}  // namespace shashoku::raster
