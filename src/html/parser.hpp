#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "core/result.hpp"
#include "html/dom.hpp"

namespace shashoku::html {

// 入れ子の上限の既定（合成ルート "#root" は数えない）。これを超えたら LimitExceeded。
// 構文解析自体は明示スタックで行い再帰しないが、Node のデストラクタと operator== は
// 木の深さだけ再帰するので、深さそのものに上限が要る。
//
// 利用者が調整する上限は `RenderLimits::nesting_depth` に一本化してあり（A25）、
// api は必ずそちらの値を parse() に渡す。ここの定数は html を単体で使うときの既定で、
// 値は RenderLimits の既定と同じ（食い違わないことを src/api/render.cpp が static_assert する）。
inline constexpr std::size_t kMaxNestingDepth = 256;

// 入力バイト数の**絶対**上限。SourceLocation::offset が std::uint32_t なので、
// これを超えると位置を正しく報告できない（実装の都合による上限で、調整はできない）。
// 利用者が調整する上限は `RenderLimits::html_bytes`（api がパースより前に見る）。
inline constexpr std::size_t kMaxSourceBytes = 0xFFFFFFFFU;

// HTML サブセット（ARCHITECTURE.md §3.6）を DOM に変換する。合成ルート "#root" を返す。
//
// 「寛容なブラウザのパーサ」ではなく「厳格なサブセットのパーサ」であることに注意。
// WHATWG のエラー回復（暗黙の閉じタグ、誤った入れ子の修復、宙に浮いた `<` や `&` の
// 文字扱い）は実装せず、おかしな入力はすべて SourceLocation つきのエラーにする
// （DESIGN.md §3-6 fail loudly）。仕様から読み取れない点についてこの実装が決めたこと:
//
//   - 素の `<` はテキストとして扱わずエラー（`&lt;` と書く）。`&` も同様だが、
//     直後が空白または入力末尾のときだけは素の `&` として許す（`A & B` を通すため）
//   - コメントと `<!DOCTYPE>` は読み飛ばすので、それを挟んで隣り合ったテキストは
//     1 つの Text ノードに連結される（`a<!--x-->b` → "ab"）
//   - タグ名・属性名の前後（`=` の周り）の空白は許すが、属性どうしの間の空白は必須
//   - 引用符なしの属性値に `"` `'` `=` `<` `` ` `` `/` は書けない（`<img src=a/>` の
//     解釈が曖昧になるため。引用符で囲めば通る）
//   - `<style>` の中身は `</style>`（後ろに空白か `>` が続くもの）までの生テキスト
//
// 位置（SourceLocation）の付け方: タグ全体に関するエラー（未対応タグ、閉じ忘れ、
// 空要素の終了タグ、非空要素の自己閉じ）はその `<` の位置、個々の文字・属性・
// 文字参照に関するエラーはその先頭の位置を指す。
//
// max_nesting_depth を超える入れ子と、kMaxSourceBytes を超える入力は LimitExceeded。
Result<Node> parse(std::string_view source, std::size_t max_nesting_depth = kMaxNestingDepth);

// --dump-stage=dom の出力。キー順は固定（element: type / tag / attrs / location / children、
// text: type / text / location）。
std::string dump_json(const Node& root);

}  // namespace shashoku::html
