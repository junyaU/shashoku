#pragma once

#include <cstddef>
#include <string>

#include "core/result.hpp"
#include "html/dom.hpp"
#include "style/computed_style.hpp"

// ② スタイル解決: DOM → スタイル付きツリー（ARCHITECTURE.md §3.7）。
//
//   CSS のトークナイズ / 宣言パース  … css_tokens.hpp, css_parser.hpp, value_parser.hpp
//   セレクタとスタイルシート          … declaration.hpp, css_parser.hpp
//   プロパティごとの値と計算値化      … value_parser.hpp, css_color.hpp
//   カスケードと継承の木走査          … resolver.cpp

namespace shashoku::style {

// `<style>` から読む規則の総数の上限の既定。セレクタの照合は「規則数 x 要素数」なので、
// 規則だけを大量に書いた入力で時間を食い潰さないための上限（A25）。
//
// 利用者が調整する上限は `RenderLimits::style_rules` に一本化してあり、api は必ず
// そちらの値を渡す。ここの定数は style を単体で使うときの既定で、値は RenderLimits の
// 既定と同じ（食い違わないことを src/api/render.cpp が static_assert する）。
inline constexpr std::size_t kMaxStyleRules = 2000;

// 合成ルート `#root`（html::parse() の返り値）を受け取り、全ノードに ComputedStyle が
// 確定した木を返す。`display: none` の要素と `<style>` / `<rp>` は木から落ちる。
//
// fail loudly（DESIGN.md §3-6）: 対応外のプロパティ / 値 / セレクタ / レイアウトは
// すべて入力位置つきのエラーになる。黙って無視する宣言は 1 つもない。
// 規則が max_style_rules を超えたら LimitExceeded。
Result<StyledNode> resolve(const html::Node& root, std::size_t max_style_rules = kMaxStyleRules);

// --dump-stage=style の出力。キー順は固定（DESIGN.md §3-3）。
std::string dump_json(const StyledNode& root);

}  // namespace shashoku::style
