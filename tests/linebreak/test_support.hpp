#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "linebreak/line_breaker.hpp"

// 行分割器のテスト用ヘルパー。
// フォントなしで「UTF-8 文字列 → Item 列 → 行ごとの文字列」を往復できるようにして、
// テーブル駆動テストの期待値を「読めば仕様が分かる」形（行ごとの文字列）で書けるようにする。
namespace shashoku::linebreak::test {

// テストの既定の 1em。全角 1 文字 = 16px、ASCII 1 文字 = 8px。
inline constexpr float kEm = 16.0F;

// 既定の字送り規則: ASCII は 0.5em、それ以外（かな・漢字・全角約物・絵文字）は 1em。
[[nodiscard]] float default_advance(char32_t cp, float em);

// UTF-8 文字列から Item 列を作る。1 コードポイント = 1 アイテム。
// '\n' は ItemKind::ForcedBreak（幅 0）になる。
[[nodiscard]] std::vector<Item> items_of(std::string_view utf8, float em = kEm);
[[nodiscard]] std::vector<Item> items_of(std::string_view utf8, float em,
                                         const std::function<float(char32_t)>& advance);

// 行の内容（[begin, content_end)）を UTF-8 で返す。テーブル駆動テストの期待値はこの形。
[[nodiscard]] std::vector<std::string> line_texts(std::span<const Item> items,
                                                  const Breaks& breaks);
// 行の全アイテム（[begin, end)）。行末に残した空白の検査に使う。
[[nodiscard]] std::vector<std::string> line_texts_full(std::span<const Item> items,
                                                       const Breaks& breaks);

// 分割可能位置を '|' で可視化する。"あ、い" → "あ、|い"。
[[nodiscard]] std::string mark_opportunities(std::string_view utf8, const Config& config = {});
// アイテムごとのポリシー（Item::strictness / Item::wrap）を設定した列を渡す版。
[[nodiscard]] std::string mark_opportunities(std::span<const Item> items,
                                             const Config& config = {});

// Config を 1 項目だけ変えて作る（素の集成初期化は -Wmissing-field-initializers に掛かる）。
[[nodiscard]] Config with_strictness(Strictness strictness);
[[nodiscard]] Config with_overflow(OverflowPolicy overflow);

[[nodiscard]] std::string to_utf8(char32_t cp);
[[nodiscard]] std::u32string to_utf32(std::string_view utf8);

}  // namespace shashoku::linebreak::test
