#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "layout/counters.hpp"
#include "linebreak/line_breaker.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

// ③ レイアウト（ARCHITECTURE.md §3.8）: スタイル付きツリー → ボックスツリー。
namespace shashoku::layout {

// 出口の検査（A36）で使う「積み上がる量」の上限（CSS px）。座標も寸法もこれで見る。
//
// **新しい制約ではなく、既存の 2 つの上限から導いた値**である:
// 1 要素あたりの長さが `RenderLimits::length_px`（2^24）以内で、要素が
// `RenderLimits::dom_nodes`（20,000）個以下なら、どれだけ足し込んでもこの値を超えない。
// だからこの上限は**常識的な入力では絶対に発動しない**（縦に長い文書も通る）。
// 発動したということは、`%` の解決や flex の比のように「入力の長さに比例しない計算」が
// 壊れたということで、そこが報告すべき事故（issue #19）。
// float の上限 3.4x10^38 からも 27 桁離れているので、この範囲の値の足し算は
// あふれない。api は `RenderLimits` から同じ式で計算した値を渡す（既定値の一致は
// src/api/render.cpp の static_assert が検査する）。
inline constexpr float kMaxGeometryPx = 3.3554432e11F;  // 2^24 x 20,000

struct Options {
  float viewport_width = 1200;             // 物理 px。0 以下・非有限は InvalidOption
  std::optional<float> viewport_height;    // 物理 px。縦書きでは必須（第 3 段）
  linebreak::Config line_break;            // overflow ポリシー等
  float max_geometry_px = kMaxGeometryPx;  // 出口の検査の上限（A36）
};

// <img> の固有寸法（物理 px）。名前 → 寸法の解決は api の仕事（A12）。
// <img src> から引いた画像の情報。id はボックスツリー（ImageFragment）経由で paint → raster
// に渡る。
struct ImageInfo {
  ImageId id = 0;
  float width = 0;  // 固有寸法（px）
  float height = 0;
};

// 見つからなければ nullopt（レイアウトは ImageNotFound を返す）。
using ImageLookup = std::function<std::optional<ImageInfo>(std::string_view src)>;

// root は style::resolve() が返す合成ルート（"#root"、display: block）。
// measurer は注入される計測器（DESIGN.md §3-4）。テストは偽物を渡す。
// counters は計測カウンタ（counters.hpp）の置き場。非 null なら作業量を足し込む。
// 出力には影響しないので、製品の呼び出し側は渡さなくてよい。
Result<BoxTree> layout(const style::StyledNode& root, const Options& options,
                       text::TextMeasurer& measurer, const ImageLookup& images,
                       Counters* counters = nullptr);

// --dump-stage=box の出力。キー順は固定（box_tree.hpp の型の並び順）。
std::string dump_json(const BoxTree& tree);

}  // namespace shashoku::layout
