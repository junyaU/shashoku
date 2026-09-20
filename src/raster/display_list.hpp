#pragma once

#include <variant>
#include <vector>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/ids.hpp"

namespace shashoku::raster {

// ⑤a の出力 / ⑤b の入力。木を平らに潰した描画命令の列。先頭から順に描く（後勝ち）。
// 座標はすべて物理座標の CSS px（float）。scale を掛けるのはラスタライザ。
// 色はストレートアルファ。合成は source-over。

struct FillRect {
  Rect rect;
  Color color;
  bool operator==(const FillRect&) const = default;
};

// radius は 4 隅共通。min(width, height) / 2 を超える分はラスタライザが切り詰める。
struct FillRoundedRect {
  Rect rect;
  float radius = 0;
  Color color;
  bool operator==(const FillRoundedRect&) const = default;
};

// 枠線。rect は外周（border-box）、radius は外周の角丸。内周の角丸は max(0, radius - width)。
// 「外周の被覆率 − 内周の被覆率」で塗る（背景の上に外周を塗ってから内周を塗り直す、はしない。
// 半透明で破綻するため）。radius = 0 なら普通の矩形の枠。
struct StrokeRoundedRect {
  Rect rect;
  float radius = 0;
  float width = 0;
  Color color;
  bool operator==(const StrokeRoundedRect&) const = default;
};

struct GlyphInstance {
  GlyphId glyph_id = 0;
  Point origin;  // グリフ原点（横書き: ベースライン上の左端）
  bool operator==(const GlyphInstance&) const = default;
};

// 同じフォント・サイズ・色のグリフ列。
struct DrawGlyphs {
  FontId font = 0;
  float size = 0;  // px（scale 前）
  Color color;
  bool sideways = false;  // true: 各グリフを原点まわりに時計回り 90° 回して描く（縦書き中の欧文）
  std::vector<GlyphInstance> glyphs;
  bool operator==(const DrawGlyphs&) const = default;
};

// 画像を dest に拡縮して描く。
struct DrawImage {
  ImageId image = 0;
  Rect dest;
  bool operator==(const DrawImage&) const = default;
};

// 以降の描画を角丸矩形で切り抜く（縁はアンチエイリアス）。入れ子にできる（積集合）。
// 用途: border-radius つきの <img>（丸アイコン）。
struct PushClip {
  Rect rect;
  float radius = 0;
  bool operator==(const PushClip&) const = default;
};

struct PopClip {
  bool operator==(const PopClip&) const = default;
};

using DrawCmd = std::variant<FillRect, FillRoundedRect, StrokeRoundedRect, DrawGlyphs, DrawImage,
                             PushClip, PopClip>;
using DisplayList = std::vector<DrawCmd>;

}  // namespace shashoku::raster
