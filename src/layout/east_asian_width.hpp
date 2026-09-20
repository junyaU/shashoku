#pragma once

namespace shashoku::layout {

// East_Asian_Width が W（Wide）または F（Fullwidth）か。
//
// 用途は A14（ソース中の改行の扱い）だけ: 改行を含む空白の並びの前後がどちらも全角なら、
// 空白を残さず消す（CSS Text 3 §4.1.3 の segment break transformation の日本語向け拡張）。
// HTML を読みやすく折り返して書いても和文に空白が入らないようにするための判定。
//
// 表は EastAsianWidth.txt から W / F の範囲を起こしたもの（east_asian_width.cpp）。
// 絵文字は Emoji_Presentation の既定が W の範囲をまとめて入れてある。
[[nodiscard]] bool is_fullwidth(char32_t cp);

}  // namespace shashoku::layout
