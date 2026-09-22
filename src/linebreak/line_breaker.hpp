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

// CSS の overflow-wrap に対応（CSS Text Level 3 §5.4）。分割可能位置がない長い語
// （URL など）を、クラスタ境界で強制的に割ってよいか。
//   Normal   : 割らない。行に収まらなければそのままはみ出す（A4 禁則 > 幅）
//   BreakWord: 行に収まらない行でだけ割る（緊急分割）。min_content_width() は変わらない
//   Anywhere : 緊急分割に加えて、min_content_width() でもその位置で区間を切る
// §5.4 は 2 値の違いをこう定める:「break-word は anywhere と同じだが、break-word が
// もたらす分割位置は min-content intrinsic size の計算では考えない」。
// min-content は flex アイテムの自動最小サイズ（Flexbox §4.5）に使うので、この区別が
// 無いと anywhere を指定した flex の子が親からはみ出す（issue #18 / ARCHITECTURE.md A35）。
//
// 値は「弱い順」に並べてある。位置の両側のアイテムの**弱い方**が、その位置で何ができるかを
// 決める（Item の境界の規則を参照）。
enum class Wrap : std::uint8_t { Normal, BreakWord, Anywhere };

// 段落（インライン整形文脈）全体の既定値。strictness と wrap は
// アイテムごとに Item::strictness / Item::wrap で上書きできる（<span> の指定）。
struct Config {
  Strictness strictness = Strictness::Strict;
  OverflowPolicy overflow = OverflowPolicy::Oidashi;

  // overflow-wrap（CSS Text 3 §5.4）。緊急分割のときも、分離禁則（—— …… 数値と単位）>
  // 行頭禁則・行末禁則 の順にできる限り守る。分離禁則を破らざるをえない位置ばかりでも、
  // その中で行頭禁則を守れる位置を優先する。守れる位置が 1 つもなければ破る
  // （この「最後の逃げ場」があるので、Anywhere の min_content_width() は必ず達成できる）。
  // クラスタの内部では決して割らない。
  Wrap wrap = Wrap::Normal;

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

  // --- はみ出してよい量（ルビの掛け。JLREQ 3.3.8）---
  // このアイテムの箱が、前 / 後ろの隣のアイテムに**掛けてよい量**（px、非負）。
  // 行分割器はこれを「その位置で詰められる空き」と同じに扱う:
  //   * 行の幅は `advance − overhang_before − overhang_after` で測る
  //   * **行頭に来たアイテムの overhang_before と、行末に来たアイテムの overhang_after は
  //     落とす**（trim_line_start / trim_line_end と同じ形。版面の外に出さないため）
  //   * 実際に効いた量は Spacing に負の値で入って返る（Spacing の説明を参照）
  // 掛けてよい**相手か**（JLREQ 3.3.8 の文字クラス。仮名には掛けてよく、漢字等には掛けない）は
  // 呼び出し側が判定して量だけを渡す: linebreak は文字クラスの意味を知らないままでいる。
  // `advance` を超える量は `advance` に丸める（幅が負にならないようにするため。行の幅が
  // アイテムを足すほど減ると、行の決定が成り立たなくなる）。
  float overhang_before = 0;
  float overhang_after = 0;

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
  //   * wrap は位置の両側のアイテムの**弱い方**（Normal < BreakWord < Anywhere）で決める。
  //     緊急分割は両側がともに Normal 以外の位置でだけ許し、min_content_width() の区間は
  //     両側がともに Anywhere の位置でだけ切る（anywhere を指定した要素の内部でだけ割れ、
  //     要素の境界では割れない）。発動条件（分割可能位置が 1 つもない行でだけ）と
  //     位置選びの優先順は Config と同じ
  //
  // = std::nullopt は既定値の明示。designated initializer で Item を作っている呼び出し側が
  // -Wmissing-field-initializers に掛からないように、既定値を必ず書く。
  std::optional<Strictness> strictness = std::nullopt;
  std::optional<Wrap> wrap = std::nullopt;

  bool operator==(const Item&) const = default;
};

// アイテム 1 個に対する字間調整。負の値 = 詰め。
//   描画側の手順: pen += before; （pen の位置にグリフを置く）; pen += advance + after;
// 始め括弧の空きはグリフの左半分にあるので before を負にしてグリフごと左へ寄せ、
// 終わり括弧・句読点の空きは右半分にあるので after を負にして次の文字を寄せる。
//
// **約物のアキ詰めと、実際に効いたルビの掛け（Item::overhang_*）の両方がここに入る。**
// どちらも「アイテムの箱を前後にはみ出させる / 次の文字を寄せる」操作なので、描画側は
// 上の 1 つの手順で両方を満たせる。掛けが効いたアイテムでは、箱（= `[pen, pen + advance]`）が
// 行の送りの範囲より前後にはみ出す（それが掛け）。**アキ詰めの対象は Text のアイテムだけ**
// なので、Atomic のアイテムに付く負の値は掛けだけ（呼び出し側はそれを読んで、掛けを含まない
// 送りの範囲 = `[pen − before, pen + advance + after]` を知ることができる）。
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

// 計算量の回帰を「時間」ではなく「回数」で測るための計測カウンタ（ARCHITECTURE.md A21 / A24）。
// linebreak は何にも依存しないので layout::Counters は使えず、同じ約束で自前に持つ:
//   * 出力（Breaks）には一切影響しない。値を読んで分岐しない。読むのはテストだけ
//   * グローバル状態・static を持たない。呼び出しごとに引数で受け取る
//   * 既定は nullptr なので、既存の呼び出し側は書き換えずに済む
struct Counters {
  std::uint64_t lines = 0;  // 出した行の数（1 行あたりの作業量を見るときの分母）
  // 行の決定で調べた位置の数。行ごとに段落の残り全体を舐めていると行数 × N に膨らむ。
  std::uint64_t line_scan = 0;
  // 幅と字間調整の計算で舐めたアイテム数の合計（lay_out / squeeze_pool / strip_trailing）。
  std::uint64_t width_items = 0;
  // 次の強制改行（<br>）を探して進んだ位置の数。行ごとに前方走査すると行数 × N になる。
  std::uint64_t mandatory_scan = 0;
  // 緊急分割（Wrap）の位置選びと、min_content_width() の区間の切れ目の判定で見た位置の数。
  std::uint64_t anywhere_scan = 0;
  // 分割可能位置の判定で前に遡った位置の数（空白越し・数値の並び・地域表示記号の並び）。
  std::uint64_t rule_scan = 0;
};

class LineBreaker {
 public:
  explicit LineBreaker(Config config = {});

  // available_width に kUnbounded を渡すと ForcedBreak でしか改行しない（max-content の計測）。
  // counters は省略可能な計測の口（出力には影響しない）。
  [[nodiscard]] Breaks break_lines(std::span<const Item> items, float available_width,
                                   Counters* counters = nullptr) const;

  // 分割不能な最長区間の幅（min-content）。flex アイテムの最小幅の計算に使う。
  // 区間の切れ目は「分割可能位置」と「両側がともに Wrap::Anywhere のクラスタ境界」
  // （CSS Text 3 §5.4）。Wrap::BreakWord では切らない。
  // 返す幅はこの行分割器で必ず達成できる（この幅で break_lines() を回すと、
  // どの行も overflows にならない）。
  [[nodiscard]] float min_content_width(std::span<const Item> items,
                                        Counters* counters = nullptr) const;

  // items[i - 1] と items[i] の間で改行してよいか（i は 1..size-1）。
  // 幅を考えない純粋な UAX #14 + 禁則の判定。テストとデバッグダンプ用に公開する。
  [[nodiscard]] std::vector<bool> break_opportunities(std::span<const Item> items,
                                                      Counters* counters = nullptr) const;

 private:
  Config config_;
};

}  // namespace shashoku::linebreak
