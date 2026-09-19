#pragma once

#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "style/declaration.hpp"

namespace shashoku::style {

// 宣言 1 つ（`margin: 1em 0`）を longhand の宣言列に展開して out に追加する。
// name は小文字化済みのプロパティ名、raw_value は元の値の文字列（エラーメッセージに使う）。
//
// fail loudly（DESIGN.md §3-6）: 対応外のプロパティは UnsupportedProperty、
// 対応プロパティの対応外の値・単位は UnsupportedValue、値がそもそも読めなければ CssParse。
// 黙って無視する宣言は 1 つもない。
Result<void> parse_declaration(std::string_view name, std::string_view raw_value,
                               SourceLocation name_location, SourceLocation value_location,
                               std::vector<Declaration>& out);

}  // namespace shashoku::style
