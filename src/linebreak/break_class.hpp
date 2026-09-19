#pragma once

#include <cstdint>

// UAX #14 の分割クラス（Line_Break プロパティ）。
// 表は LineBreak.txt 18.0.0 から起こして break_class.cpp に埋め込んである
// （実行時にファイルを読まない。行分割器は標準ライブラリ以外に依存しない）。
namespace shashoku::linebreak {

// ARCHITECTURE.md §3.4 (1) のクラス集合。日本語と基本ラテンに必要なものだけを持ち、
// 残りは受け皿に寄せる（対応表は break_class.cpp の冒頭コメント）。
enum class BreakClass : std::uint8_t {
  Al,  // Alphabetic: 通常の文字。未知のコードポイントもここ（UAX #14 LB1 の AI/SG/XX/SA）
  Ba,  // Break After: 全角スペース U+3000、ハイフン類の一部
  Bb,  // Break Before: ° ± など前に付く記号
  B2,  // Break Opportunity Before and After: —（U+2014）。LB17 の分離禁則の対象
  Bk,  // Mandatory Break
  Cl,  // Close Punctuation: 、。」』）〕 …（行頭禁則）
  Cm,  // Combining Mark
  Cp,  // Close Parenthesis: ) ]（行頭禁則。LB30 で OP/CP の東アジア幅を見る）
  Cr,  // Carriage Return
  Cj,  // Conditional Japanese Starter: 小書き仮名・長音（Strictness で NS / ID に解決）
  Eb,   // Emoji Base
  Em,   // Emoji Modifier
  Ex,   // Exclamation/Interrogation: ! ? ！ ？（行頭禁則）
  Gl,   // Non-breaking (Glue): NBSP など
  Hy,   // Hyphen: -（UAX #14 18.0 の HH もここに寄せる）
  Id,   // Ideographic: 漢字・かな・全角記号・絵文字・ハングル音節
  In,   // Inseparable: ‥ …（分離禁則）
  Is,   // Infix Numeric Separator: , . : ;（行頭禁則）
  Lf,   // Line Feed
  Nl,   // Next Line
  Ns,   // Nonstarter: 々 ゝ ・ ： ；（行頭禁則）。strict では Cj もここへ
  Nu,   // Numeric
  Op,   // Open Punctuation: 「『（〔（行末禁則）
  Po,   // Postfix Numeric: % ％ ℃
  Pr,   // Prefix Numeric: $ ￥ ＄
  Qu,   // Quotation
  Ri,   // Regional Indicator（LB30a）
  Sp,   // Space
  Sy,   // Symbols Allowing Break After: /
  Wj,   // Word Joiner
  Zw,   // Zero Width Space
  Zwj,  // Zero Width Joiner
};

// コードポイントの分割クラス。表にないものは Al（LB1: AI/SG/XX は AL に解決する）。
[[nodiscard]] BreakClass break_class_of(char32_t cp);

// LB30 の $EastAsian: Line_Break が OP / CP で East_Asian_Width が F / W / H のもの。
// 「日中韓統合漢字拡張G「ユニコード」」を G の後ろで割れるようにするための除外集合。
[[nodiscard]] bool is_east_asian_bracket(char32_t cp);

// CSS Text 3 §5.3 の loose で「前で割ってよい」中点類:
// ・ U+30FB ： U+FF1A ； U+FF1B ･ U+FF65 ‼ U+203C ⁇ U+2047 ⁈ U+2048 ⁉ U+2049 ！ U+FF01 ？ U+FF1F
[[nodiscard]] bool is_loose_centered_punctuation(char32_t cp);

// CSS Text 3 §5.3 の loose で「前で割ってよい」繰り返し記号:
// 々 U+3005 〻 U+303B ゝ U+309D ゞ U+309E ヽ U+30FD ヾ U+30FE
[[nodiscard]] bool is_iteration_mark(char32_t cp);

// CSS Text 3 §5.3 の loose で「前で割ってよい」接尾辞・「後ろで割ってよい」接頭辞:
// Line_Break が PO / PR で East_Asian_Width が A / F / W のもの（％ ℃ ＄ ￥ など）。
[[nodiscard]] bool is_wide_numeric_affix(char32_t cp);

// CSS Text 3 §5.3 の normal / loose で「前で割ってよい」CJK ハイフン類: 〜 U+301C ゠ U+30A0
[[nodiscard]] bool is_cjk_hyphen_like(char32_t cp);

// CSS Text 3 §5.3 の loose で「直前が ID なら前で割ってよい」ハイフン: ‐ U+2010 – U+2013
[[nodiscard]] bool is_loose_hyphen(char32_t cp);

}  // namespace shashoku::linebreak
