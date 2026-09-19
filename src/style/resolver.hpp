#pragma once

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

// 合成ルート `#root`（html::parse() の返り値）を受け取り、全ノードに ComputedStyle が
// 確定した木を返す。`display: none` の要素と `<style>` / `<rp>` は木から落ちる。
//
// fail loudly（DESIGN.md §3-6）: 対応外のプロパティ / 値 / セレクタ / レイアウトは
// すべて入力位置つきのエラーになる。黙って無視する宣言は 1 つもない。
Result<StyledNode> resolve(const html::Node& root);

// --dump-stage=style の出力。キー順は固定（DESIGN.md §3-3）。
std::string dump_json(const StyledNode& root);

}  // namespace shashoku::style
