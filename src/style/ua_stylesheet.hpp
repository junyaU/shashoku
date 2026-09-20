#pragma once

#include <string_view>

#include "core/result.hpp"
#include "style/declaration.hpp"

namespace shashoku::style {

// UA スタイルシート（ARCHITECTURE.md §3.7）。自前の CSS パーサで読ませる
// （規則を手で組み立てるより、対応範囲の生きたテストになる）。
//
// h1〜h6 の上下マージンはブラウザの既定値（自身の font-size 基準の em）。
// rt の font-size 50% は、親の font-size 基準という点で 0.5em と同じ意味になる
// （font-size の em は親の font-size で解決する。ARCHITECTURE.md §3.7）。
inline constexpr std::string_view kUserAgentCss = R"css(
div, p, h1, h2, h3, h4, h5, h6 { display: block }
span, ruby, rt, img, br { display: inline }
style, rp { display: none }
h1 { font-size: 2em;    font-weight: bold; margin: 0.67em 0 }
h2 { font-size: 1.5em;  font-weight: bold; margin: 0.83em 0 }
h3 { font-size: 1.17em; font-weight: bold; margin: 1em 0 }
h4 { font-size: 1em;    font-weight: bold; margin: 1.33em 0 }
h5 { font-size: 0.83em; font-weight: bold; margin: 1.67em 0 }
h6 { font-size: 0.67em; font-weight: bold; margin: 2.33em 0 }
p { margin: 1em 0 }
rt { font-size: 0.5em }
)css";

// 解析済みの UA スタイルシートを作る。ここで失敗したら shashoku 側のバグなので Internal。
Result<Stylesheet> build_ua_stylesheet();

}  // namespace shashoku::style
