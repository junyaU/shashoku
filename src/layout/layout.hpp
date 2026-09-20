#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "layout/box_tree.hpp"
#include "linebreak/line_breaker.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

// ③ レイアウト（ARCHITECTURE.md §3.8）: スタイル付きツリー → ボックスツリー。
namespace shashoku::layout {

struct Options {
  float viewport_width = 1200;           // 物理 px。0 以下・非有限は InvalidOption
  std::optional<float> viewport_height;  // 物理 px。縦書きでは必須（第 3 段）
  linebreak::Config line_break;          // overflow ポリシー等
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
Result<BoxTree> layout(const style::StyledNode& root, const Options& options,
                       text::TextMeasurer& measurer, const ImageLookup& images);

// --dump-stage=box の出力。キー順は固定（box_tree.hpp の型の並び順）。
std::string dump_json(const BoxTree& tree);

}  // namespace shashoku::layout
