#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

// 行分割器: shashoku の存在理由（DESIGN.md §5, §6）。
//
// このモジュールは **標準ライブラリ以外の何にも依存しない**（core にも依存しない）。
// フォントもレイアウトも知らず、「送り幅つきのアイテム列」と「行の幅」だけから行を決める。
// だからテーブル駆動テストがフォントなしで書ける。
//
// 設計書のスケッチ（MeasuredChar / LineRange）からの変更点と理由:
//   * 入力の単位は「文字」ではなく Item（= 1 クラスタ or 1 個の分割不能なインライン要素）。
//     結合文字・異体字セレクタ・絵文字の途中で割らないため。また画像やルビのまとまりも
//     同じ列に流せる（行分割器から見ればどれも「幅を持つ分割不能な箱」）。
//   * 出力は行の範囲だけでなく、アイテムごとの字間調整（Spacing）を含む。
//     追い込み・約物連続のアキ詰め・行末約物の半角化は「約物の前後の空きを削る」操作で、
//     行の範囲だけでは表現できないため。
namespace shashoku::linebreak {

// CSS の line-break に対応（CSS Text Level 3 §5.3）。小書きの仮名・長音（UAX #14 の CJ）の前で
//   Strict: 割らない（CJ を NS として扱う）。JIS X 4051 の標準的な行頭禁則
//   Normal: 割ってよい（CJ を ID として扱う）
//   Loose : Normal に加えて、繰り返し記号（々 ゝ ゞ ヽ ヾ 〻）などの前でも割ってよい
enum class Strictness : std::uint8_t { Strict, Normal, Loose };

// 行に収まらなかったときの処理（JIS X 4051 / JLREQ）。
enum class OverflowPolicy : std::uint8_t {
  Oidashi,  // 追い出し: 禁則に掛かる文字を道連れにして次の行へ送る
  Oikomi,  // 追い込み: 約物の空きを詰めて行内に収める。詰めきれなければ追い出し
  Burasage,  // ぶら下げ: 行末の句読点 1 文字を行の外にはみ出させる。対象外の文字なら追い出し
};

// 段落（インライン整形文脈）全体の既定値。strictness と break_anywhere は
// アイテムごとに Item::strictness / Item::break_anywhere で上書きできる（<span> の指定）。
struct Config {
  Strictness strictness = Strictness::Strict;
  OverflowPolicy overflow = OverflowPolicy::Oidashi;

  // overflow-wrap: anywhere / break-word。分割可能位置がなく行に収まらない語
  // （長い URL など）を、クラスタ境界で強制的に割る。
  // min_content_width() には影響しない（CSS の break-word 相当。CSS Text 3 の
  // overflow-wrap: anywhere は min-content に効くが、ここでは両者を区別していない）。
  // このときも分離禁則（—— …… 数値と単位）> 行頭禁則・行末禁則 の順にできる限り守る。
  // 分離禁則を破らざるをえない位置ばかりでも、その中で行頭禁則を守れる位置を優先する。
  // 守れる位置が 1 つもなければ破る。クラスタの内部では決して割らない。
  bool break_anywhere = false;

  // 約物が連続するときの空きを詰める（JLREQ 3.1.4）。「」」「」や「。」」が間延びしない。
  bool collapse_punctuation_spacing = true;
  // 行末に来た終わり括弧・句読点が収まらないとき、後ろの半角空きを詰めてよい
  // （CSS text-spacing-trim: normal 相当）。
  bool trim_line_end = true;
  // 行頭に来た始め括弧の前の半角空きを詰める（天付き）。既定は詰めない（CSS の既定と同じ）。
  bool trim_line_start = false;

  // 禁則テーブルへの追加（利用者が「この文字も行頭に置きたくない」を足すための口）
  std::u32string extra_line_start_prohibited;
  std::u32string extra_line_end_prohibited;

  bool operator==(const Config&) const = default;
};

enum class ItemKind : std::uint8_t {
  Text,    // 1 クラスタ。cp で分割クラスを決める
  Atomic,  // 分割不能なインライン要素（画像、ルビのまとまり）。UAX #14 の ID として扱う
  ForcedBreak,  // <br>。このアイテムの直後で必ず改行する。幅は 0
};

struct Item {
  ItemKind kind = ItemKind::Text;
  char32_t cp = 0;    // クラスタ先頭のコードポイント（Text のみ）
  float advance = 0;  // 字送り方向の幅。letter-spacing 込み
  // このアイテムのフォントサイズ（px）。全角約物の空き量（0.5em）の計算に使う。
  // 0 のときは advance を 1em とみなす。
  float em = 0;
  // このアイテムの直前での分割を禁止する（呼び出し側の都合。例: 複数クラスタからなるルビ内部）
  bool no_break_before = false;

  // --- アイテムごとのポリシー上書き（CSS の line-break / overflow-wrap）---
  // どちらも CSS ではテキスト（インラインボックス）に適用される継承プロパティなので、
  // 段落の途中の <span> で値が変わりうる。nullopt なら Config の値を使う
  // （既定のまま = Config だけを使っていたころと出力は完全に同じ）。
  //
  // 境界の規則（ARCHITECTURE.md A23。CSS Text Level 3 の "Line Breaking Details" は、
  // 要素の境界にまたがる分割位置でどの要素の line-break / word-break / overflow-wrap が
  // 効くかを "undefined in this level" としているので、ここで決める）:
  //
  //   * strictness は「分割クラスの解決」に使い、アイテム自身の値で解決する
  //     （CJ を NS とみなすか ID とみなすか、loose の追加規則で ID に格下げするかは
  //     その文字 1 個の問題なので、境界が曖昧にならない）。ペア表・文脈規則は
  //     解決済みのクラスに対して従来どおり働く。例外は loose の
  //     「直前が ID ならハイフン ‐ – の前で割ってよい」だけで、これは 2 アイテムに
  //     またがるので、行頭に来る側（= 後ろのアイテム = ハイフン自身）の値で決める
  //   * break_anywhere の緊急分割は、位置の両側のアイテムがともに true のときだけ許す
  //     （anywhere を指定した要素の内部でだけ割れ、要素の境界では割れない）。
  //     発動条件（分割可能位置が 1 つもない行でだけ）と位置選びの優先順は Config と同じ
  //
  // = std::nullopt は既定値の明示。designated initializer で Item を作っている呼び出し側が
  // -Wmissing-field-initializers に掛からないように、既定値を必ず書く。
  std::optional<Strictness> strictness = std::nullopt;
  std::optional<bool> break_anywhere = std::nullopt;

  bool operator==(const Item&) const = default;
};

// アイテム 1 個に対する字間調整。負の値 = 詰め。
//   描画側の手順: pen += before; （pen の位置にグリフを置く）; pen += advance + after;
// 始め括弧の空きはグリフの左半分にあるので before を負にしてグリフごと左へ寄せ、
// 終わり括弧・句読点の空きは右半分にあるので after を負にして次の文字を寄せる。
struct Spacing {
  float before = 0;
  float after = 0;

  bool operator==(const Spacing&) const = default;
};

struct Line {
  std::size_t begin = 0;  // items の [begin, end)。行末の空白と ForcedBreak を含む
  std::size_t end = 0;
  std::size_t content_end = 0;  // 行末の空白と ForcedBreak を除いた終端（描画・幅計算はここまで）
  // [begin, content_end) の幅（Spacing 適用後）。ぶら下げた文字の幅は含まない。
  // text-align はこの幅で計算する。
  float width = 0;
  // ぶら下げた句読点の幅（なければ 0）。その文字は [begin, content_end) の最後の 1 個。
  float hang = 0;
  bool forced = false;  // ForcedBreak で終わった（両端揃えでは最終行扱いにする）
  // 禁則を守るため、または分割可能位置がないために available_width を超えた。
  // 「禁則 > 幅」: 幅 1em の箱に「あ。」を流したら、割らずにはみ出す。
  bool overflows = false;

  bool operator==(const Line&) const = default;
};

struct Breaks {
  std::vector<Line> lines;  // 入力が空なら空。それ以外は全アイテムを隙間なく覆う
  std::vector<Spacing> spacing;  // items と同じ長さ

  bool operator==(const Breaks&) const = default;
};

inline constexpr float kUnbounded = std::numeric_limits<float>::infinity();

class LineBreaker {
 public:
  explicit LineBreaker(Config config = {});

  // available_width に kUnbounded を渡すと ForcedBreak でしか改行しない（max-content の計測）。
  [[nodiscard]] Breaks break_lines(std::span<const Item> items, float available_width) const;

  // 分割不能な最長区間の幅（min-content）。flex アイテムの最小幅の計算に使う。
  [[nodiscard]] float min_content_width(std::span<const Item> items) const;

  // items[i - 1] と items[i] の間で改行してよいか（i は 1..size-1）。
  // 幅を考えない純粋な UAX #14 + 禁則の判定。テストとデバッグダンプ用に公開する。
  [[nodiscard]] std::vector<bool> break_opportunities(std::span<const Item> items) const;

 private:
  Config config_;
};

}  // namespace shashoku::linebreak
