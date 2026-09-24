#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/diagnostics.hpp"
#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "style/declaration.hpp"

// CSS の構造（規則 / セレクタ / 宣言ブロック）を読む。値の中身は value_parser.hpp が読む。
//
// 対応するのは ARCHITECTURE.md §3.7 の範囲だけ:
//   セレクタは `tag` `.class` `#id` とその結合、カンマ区切り、全称 `*`
//   結合子（空白 / `>` / `+` / `~`）・擬似クラス・擬似要素・属性セレクタ・`@` 規則・
//   `!important` はいずれも CssParse（理由を message に書く）
//
// A46「一度に全部」: 非致命のエラー（CssParse / UnsupportedProperty / UnsupportedValue /
// UnsupportedTag）は `diagnostics` に足し、**宣言の単位で読み飛ばして続行する**。
// セレクタが読めなければ規則の単位（`{…}` か次の `;` まで）で読み飛ばす。捨てた宣言は
// 「書かれなかった」扱いで、out には 1 つも残さない。
// `unexpected` を返すのは致命（LimitExceeded / Internal）だけ。
//
// A55 で 2 つ足した:
//   * **セレクタを使えない規則の宣言も、構文の診断だけは出す**（読めないセレクタ / 対応外の
//     タグ）。宣言ブロックの括弧が対応していて安全に読めるときだけ通常の宣言パーサに通し、
//     結果は捨てる（out に入れない = カスケードに入らないので、その規則を適用した前提の
//     計算値の診断は出ない）。安全に読めなければ今までどおり規則ごと読み飛ばす
//   * **対応外のタグを名指しするタイプセレクタは `UnsupportedTag`**（① がそのタグを要素に
//     しないので決して一致しない）。カンマ区切りならその部分だけ落として残りは適用する。
//     一致しないクラス / ID は「正常な選択の結果」なので報告しない

namespace shashoku::style {

// `<style>` 要素の中身を解析して out に規則を足す。
// base は CSS の先頭が入力 HTML 上のどこにあるか。CSS 内の行・桁を足して位置を報告する。
// order は複数の `<style>` をまたいで通し番号を振るためのカウンタ（呼び出し側が持つ）。
Result<void> parse_stylesheet(std::string_view css, SourceLocation base, std::uint32_t& order,
                              Stylesheet& out, Diagnostics& diagnostics);

// `style` 属性の中身（宣言だけ。`{}` はない）を解析する。
// 属性値は文字参照が解決済みでオフセットが原文と対応しないので、位置は常に attribute を指す。
Result<std::vector<Declaration>> parse_inline_style(std::string_view css, SourceLocation attribute,
                                                    Diagnostics& diagnostics);

}  // namespace shashoku::style
