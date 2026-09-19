#include "linebreak/line_breaker.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "linebreak/break_class.hpp"
#include "linebreak/punctuation.hpp"

// 行分割器の本体。規則の出典は ARCHITECTURE.md §3.4 と次の一次資料:
//   * UAX #14 Unicode Line Breaking Algorithm（規則 LB1〜LB31）
//   * JLREQ 日本語組版処理の要件 3.1（行頭・行末禁則、約物の空き）、3.8（行の調整処理）
//   * CSS Text Level 3 §5.3（line-break: strict / normal / loose）
namespace shashoku::linebreak {
namespace {

// 幅の比較に使う許容誤差（px）。浮動小数点の積み上げ誤差で「ちょうど収まる」行が
// あふれ扱いになるのを防ぐ。A9 の許す演算だけで組み立てた定数。
constexpr float kWidthEpsilon = 1.0F / 1024.0F;

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// LB4 / LB5 の強制改行クラス。
constexpr bool is_hard(BreakClass cls) {
  return cls == BreakClass::Bk || cls == BreakClass::Cr || cls == BreakClass::Lf ||
         cls == BreakClass::Nl;
}

// 行頭禁則（この字が行頭に来てはいけない）。ARCHITECTURE.md §3.4 (2):
// 句読点・終わり括弧（CL / CP）、感嘆符・疑問符（EX）、中点・繰り返し記号（NS）、
// 数値の区切り（IS）、スラッシュ（SY）、strict の小書き仮名（CJ → NS）。
//
// IN（… ‥）は入れない。UAX #14 の LB22（× IN）は「…… を割らない」という分離禁則であって、
// JIS X 4051 の行頭禁則ではない（… が行頭に来ること自体は許される）。並びの内部を割らない
// ことは LB22 のペア規則と splits_inseparable() が担保する。
constexpr bool is_line_start_prohibited(BreakClass cls) {
  return cls == BreakClass::Cl || cls == BreakClass::Cp || cls == BreakClass::Ex ||
         cls == BreakClass::Ns || cls == BreakClass::Is || cls == BreakClass::Sy;
}

// 行末禁則（この字を行末に置かない）= 始め括弧（OP）。LB14 が空白越しでも守る。
constexpr bool is_line_end_prohibited(BreakClass cls) { return cls == BreakClass::Op; }

// 1 回の呼び出しぶんの解析結果。アイテム列は呼び出し側が所有する。
class Analysis {
 public:
  Analysis(Config config, std::span<const Item> items);

  [[nodiscard]] std::vector<bool> opportunities() const;
  [[nodiscard]] Breaks break_lines(float available_width) const;
  [[nodiscard]] float min_content_width() const;

 private:
  // 1 行ぶんの幅の測り方。content_end は行末の空白と ForcedBreak を落とした終端。
  struct Fit {
    std::size_t content_end = 0;
    float width = 0;
    bool end_trim = false;  // 行末の約物のアキを捨てたか
  };

  // --- 解析 ---
  [[nodiscard]] BreakClass resolve_class(const Item& item) const;
  void resolve_classes();
  void build_spacing_tables();
  void compute_opportunities();

  // --- 分割可能位置の規則（sig_ 上の位置 k = items_[sig_[k - 1]] と items_[sig_[k]] の間）---
  [[nodiscard]] std::optional<bool> rule_core(std::size_t k) const;
  [[nodiscard]] std::optional<bool> rule_punctuation(std::size_t k) const;
  [[nodiscard]] std::optional<bool> rule_alphanumeric(std::size_t k) const;
  [[nodiscard]] std::optional<bool> rule_number(std::size_t k) const;
  [[nodiscard]] std::optional<bool> rule_letter(std::size_t k) const;
  [[nodiscard]] bool can_break_between(std::size_t k) const;

  [[nodiscard]] BreakClass sig_class(std::size_t k) const { return cls_[sig_[k]]; }
  [[nodiscard]] char32_t sig_cp(std::size_t k) const { return items_[sig_[k]].cp; }
  [[nodiscard]] std::size_t sig_count() const { return sig_.size(); }
  // 直前の空白列を飛ばした位置（LB8 / LB14 / LB16 / LB17 の「SP* 越し」）。
  [[nodiscard]] std::size_t before_spaces(std::size_t k) const;
  [[nodiscard]] bool before_spaces_is(std::size_t k, BreakClass cls) const;
  // LB25 の NU (SY | IS)* を k から遡って読む。
  [[nodiscard]] bool numeric_run_ends_at(std::size_t k) const;
  // items_[p] の直前で割ると分離禁則（JIS X 4051）を破るか。緊急分割の位置選びに使う。
  [[nodiscard]] bool splits_inseparable(std::size_t p) const;

  // --- 幅と字間調整 ---
  [[nodiscard]] bool strippable(std::size_t i) const;
  [[nodiscard]] std::size_t strip_trailing(std::size_t begin, std::size_t end) const;
  // [begin, content_end) の Spacing を決め、内容幅を返す。out が null なら測るだけ。
  //   end_trim: 行末の終わり括弧・句読点の後ろのアキを捨てる
  //   squeeze : 追い込みで「残っているアキ」を詰める割合（0〜1）
  float lay_out(std::size_t begin, std::size_t content_end, bool end_trim, float squeeze,
                std::vector<Spacing>* out) const;
  [[nodiscard]] float squeeze_pool(std::size_t begin, std::size_t content_end, bool end_trim) const;
  [[nodiscard]] Fit fit(std::size_t begin, std::size_t end, float available) const;

  // --- 行の決定 ---
  [[nodiscard]] std::size_t mandatory_limit(std::size_t begin) const;
  void scan_candidates(std::size_t begin, std::size_t limit, float available, std::size_t& best,
                       std::size_t& first, std::size_t& next) const;
  [[nodiscard]] std::size_t break_anywhere_at(std::size_t begin, std::size_t limit,
                                              float available) const;
  [[nodiscard]] bool ends_forced(std::size_t end) const;
  void emit_line(std::size_t begin, std::size_t end, float available, Breaks& out) const;
  bool try_hang(std::size_t begin, std::size_t next, float available, Breaks& out) const;
  bool try_squeeze(std::size_t begin, std::size_t next, float available, Breaks& out) const;

  Config config_;
  std::span<const Item> items_;

  std::vector<BreakClass> cls_;
  std::vector<std::uint8_t> attached_;  // LB9 で直前のアイテムに吸収された
  std::vector<std::uint8_t> raw_zwj_;  // LB9 の吸収前のクラスが ZWJ だった（LB8a 用）
  std::vector<std::uint8_t> start_prohibited_;
  std::vector<std::uint8_t> end_prohibited_;
  std::vector<std::size_t> sig_;  // attached_ でないアイテムの添字
  std::vector<std::size_t> sig_pos_;  // アイテム添字 → sig_ 上の位置（attached_ は kNone）

  std::vector<PunctKind> punct_;
  std::vector<float> trim_before_;  // 字面の前にある詰められるアキ（正の値）
  std::vector<float> trim_after_;   // 字面の後ろにある詰められるアキ（正の値）
  // JLREQ 3.1.4 の連続約物のアキ詰め。同じ行に並んだときだけ効く。
  std::vector<float> collapse_before_;  // 対 (i-1, i) で items_[i] の前から詰める量
  std::vector<float> collapse_after_;   // 対 (i, i+1) で items_[i] の後ろから詰める量

  std::vector<std::uint8_t> opp_;        // items_[i-1] と items_[i] の間で割ってよい
  std::vector<std::uint8_t> mandatory_;  // その位置で必ず割る（LB4 / LB5）
};

Analysis::Analysis(Config config, std::span<const Item> items)
    : config_(std::move(config)), items_(items) {
  const std::size_t n = items_.size();
  cls_.assign(n, BreakClass::Al);
  attached_.assign(n, 0);
  raw_zwj_.assign(n, 0);
  start_prohibited_.assign(n, 0);
  end_prohibited_.assign(n, 0);
  punct_.assign(n, PunctKind::None);
  trim_before_.assign(n, 0.0F);
  trim_after_.assign(n, 0.0F);
  collapse_before_.assign(n, 0.0F);
  collapse_after_.assign(n, 0.0F);
  opp_.assign(n, 0);
  mandatory_.assign(n, 0);
  resolve_classes();
  build_spacing_tables();
  compute_opportunities();
}

BreakClass Analysis::resolve_class(const Item& item) const {
  if (item.kind == ItemKind::Atomic) {
    return BreakClass::Id;  // ARCHITECTURE.md §3.4 (2): 分割不能な箱は ID 扱い
  }
  if (item.kind == ItemKind::ForcedBreak) {
    return BreakClass::Bk;  // LB4
  }

  const char32_t cp = item.cp;
  // 利用者が足した禁則は該当文字を NS / OP に格上げする（ARCHITECTURE.md §3.4 (2)）。
  // 両方に入っている文字は OP（行末禁則）を採り、行頭側は start_prohibited_ で担保する。
  if (config_.extra_line_end_prohibited.find(cp) != std::u32string::npos) {
    return BreakClass::Op;
  }
  if (config_.extra_line_start_prohibited.find(cp) != std::u32string::npos) {
    return BreakClass::Ns;
  }

  BreakClass cls = break_class_of(cp);
  // CSS Text 3 §5.3。CJ の解決は line_breaker.hpp の Strictness の定義に従う
  // （strict = NS、normal / loose = ID）。
  if (config_.strictness == Strictness::Strict) {
    if (cls == BreakClass::Cj) {
      cls = BreakClass::Ns;
    }
    return cls;
  }
  if (cls == BreakClass::Cj || is_cjk_hyphen_like(cp)) {
    cls = BreakClass::Id;  // normal / loose: 小書き仮名・長音、〜 ゠ の前で割ってよい
  }
  if (config_.strictness == Strictness::Loose &&
      (cls == BreakClass::In || is_iteration_mark(cp) || is_loose_centered_punctuation(cp) ||
       is_wide_numeric_affix(cp))) {
    cls = BreakClass::Id;  // loose: ‥ … 々 ・ ！ ？ ％ ￥ などの前でも割ってよい
  }
  return cls;
}

void Analysis::resolve_classes() {
  const std::size_t n = items_.size();
  for (std::size_t i = 0; i < n; ++i) {
    cls_[i] = resolve_class(items_[i]);
  }

  // LB9: X (CM | ZWJ)* を X として扱う。LB10: 残った CM / ZWJ は AL。
  for (std::size_t i = 0; i < n; ++i) {
    if (cls_[i] != BreakClass::Cm && cls_[i] != BreakClass::Zwj) {
      continue;
    }
    raw_zwj_[i] = static_cast<std::uint8_t>(cls_[i] == BreakClass::Zwj);
    const bool has_base = i > 0 && !is_hard(cls_[i - 1]) && cls_[i - 1] != BreakClass::Sp &&
                          cls_[i - 1] != BreakClass::Zw;
    if (has_base) {
      attached_[i] = 1;
      cls_[i] = cls_[i - 1];
    } else {
      cls_[i] = BreakClass::Al;
    }
  }

  sig_.reserve(n);
  sig_pos_.assign(n, kNone);
  for (std::size_t i = 0; i < n; ++i) {
    if (attached_[i] == 0) {
      sig_pos_[i] = sig_.size();
      sig_.push_back(i);
    }
    const bool text = items_[i].kind == ItemKind::Text;
    start_prohibited_[i] = static_cast<std::uint8_t>(
        is_line_start_prohibited(cls_[i]) ||
        (text && config_.extra_line_start_prohibited.find(items_[i].cp) != std::u32string::npos));
    end_prohibited_[i] = static_cast<std::uint8_t>(
        is_line_end_prohibited(cls_[i]) ||
        (text && config_.extra_line_end_prohibited.find(items_[i].cp) != std::u32string::npos));
  }
}

void Analysis::build_spacing_tables() {
  const std::size_t n = items_.size();
  for (std::size_t i = 0; i < n; ++i) {
    const Item& item = items_[i];
    if (item.kind != ItemKind::Text) {
      continue;
    }
    punct_[i] = punct_kind(item.cp);
    // 空き量は em から出す。em が 0 のときは advance を 1em とみなす（契約ヘッダ）。
    const float em = item.em > 0 ? item.em : item.advance;
    const float half = 0.5F * em;
    const float quarter = 0.25F * em;
    float before = 0.0F;
    float after = 0.0F;
    if (punct_[i] == PunctKind::Open) {
      before = half;
    } else if (is_closing_group(punct_[i])) {
      after = half;
    } else if (punct_[i] == PunctKind::MiddleDot) {
      before = quarter;
      after = quarter;
    }
    // 送りより広いアキは詰められない（詰めても幅が減らない）。幅の単調性を保つための上限。
    before = std::min(before, std::max(item.advance, 0.0F));
    after = std::min(after, std::max(item.advance - before, 0.0F));
    trim_before_[i] = before;
    trim_after_[i] = after;
  }

  if (!config_.collapse_punctuation_spacing) {
    return;
  }
  // JLREQ 3.1.4: 終わり括弧類・句読点 → 始め括弧類 / 終わり括弧類・句読点どうし /
  // 始め括弧類どうし が並んだら、間のアキを半角ぶん詰める。
  for (std::size_t i = 1; i < n; ++i) {
    const PunctKind prev = punct_[i - 1];
    const PunctKind next = punct_[i];
    if (is_closing_group(prev) && (next == PunctKind::Open || is_closing_group(next))) {
      collapse_after_[i - 1] = trim_after_[i - 1];
    } else if (prev == PunctKind::Open && next == PunctKind::Open) {
      collapse_before_[i] = trim_before_[i];
    }
  }
}

std::size_t Analysis::before_spaces(std::size_t k) const {
  std::size_t j = k;
  while (j > 0) {
    --j;
    if (sig_class(j) != BreakClass::Sp) {
      return j;
    }
  }
  return kNone;
}

bool Analysis::before_spaces_is(std::size_t k, BreakClass cls) const {
  const std::size_t j = before_spaces(k);
  return j != kNone && sig_class(j) == cls;
}

bool Analysis::numeric_run_ends_at(std::size_t k) const {
  if (k == kNone || k >= sig_count()) {
    return false;
  }
  std::size_t j = k;
  while (sig_class(j) == BreakClass::Sy || sig_class(j) == BreakClass::Is) {
    if (j == 0) {
      return false;
    }
    --j;
  }
  return sig_class(j) == BreakClass::Nu;
}

// LB4〜LB12a: 強制改行・空白・結合・グルー。
std::optional<bool> Analysis::rule_core(std::size_t k) const {
  const BreakClass prev = sig_class(k - 1);
  const BreakClass next = sig_class(k);
  if (prev == BreakClass::Cr && next == BreakClass::Lf) {
    return false;  // LB5 CR × LF
  }
  if (is_hard(prev)) {
    return true;  // LB4 BK ! / LB5 CR ! LF ! NL !
  }
  if (is_hard(next)) {
    return false;  // LB6 × (BK | CR | LF | NL)
  }
  if (next == BreakClass::Sp || next == BreakClass::Zw) {
    return false;  // LB7 × SP / × ZW
  }
  if (before_spaces_is(k, BreakClass::Zw)) {
    return true;  // LB8 ZW SP* ÷
  }
  if (prev == BreakClass::Zwj) {
    return false;  // LB8a ZWJ ×
  }
  if (next == BreakClass::Wj || prev == BreakClass::Wj) {
    return false;  // LB11 × WJ / WJ ×
  }
  if (prev == BreakClass::Gl) {
    return false;  // LB12 GL ×
  }
  if (next == BreakClass::Gl && prev != BreakClass::Sp && prev != BreakClass::Hy) {
    return false;  // LB12a [^SP HY] × GL
  }
  return std::nullopt;
}

// LB13〜LB22: 括弧・約物・空白越し・ハイフン・分離禁則。日本語の禁則はここに乗る。
std::optional<bool> Analysis::rule_punctuation(std::size_t k) const {
  const BreakClass prev = sig_class(k - 1);
  const BreakClass next = sig_class(k);
  if (next == BreakClass::Cl || next == BreakClass::Cp || next == BreakClass::Ex ||
      next == BreakClass::Sy) {
    return false;  // LB13 × CL × CP × EX × SY（行頭禁則: 。、」）！ ？）
  }
  if (before_spaces_is(k, BreakClass::Op)) {
    return false;  // LB14 OP SP* ×（行末禁則: 「『（ の後ろで割らない）
  }
  if (prev == BreakClass::Sp && next == BreakClass::Is && k + 1 < sig_count() &&
      sig_class(k + 1) == BreakClass::Nu) {
    return true;  // LB15c SP ÷ IS NU（'subtract .5'）
  }
  if (next == BreakClass::Is) {
    return false;  // LB15d × IS
  }
  if (next == BreakClass::Ns &&
      (before_spaces_is(k, BreakClass::Cl) || before_spaces_is(k, BreakClass::Cp))) {
    return false;  // LB16 (CL | CP) SP* × NS
  }
  if (next == BreakClass::B2 && before_spaces_is(k, BreakClass::B2)) {
    return false;  // LB17 B2 SP* × B2（分離禁則: ——）
  }
  if (prev == BreakClass::Sp) {
    return true;  // LB18 SP ÷
  }
  if (next == BreakClass::Qu || prev == BreakClass::Qu) {
    return false;  // LB19 × QU / QU ×
  }
  if (prev == BreakClass::Hy && next == BreakClass::Al) {
    // LB20a (sot | BK | CR | LF | NL | SP | ZW | CB | GL) HY × AL: 語頭のハイフンの後ろ
    const bool word_initial =
        k == 1 || is_hard(sig_class(k - 2)) || sig_class(k - 2) == BreakClass::Sp ||
        sig_class(k - 2) == BreakClass::Zw || sig_class(k - 2) == BreakClass::Gl;
    if (word_initial) {
      return false;
    }
  }
  if (config_.strictness == Strictness::Loose && prev == BreakClass::Id &&
      is_loose_hyphen(sig_cp(k))) {
    return true;  // CSS Text 3 §5.3: loose では ID の後ろの ‐ – の前で割ってよい
  }
  if (next == BreakClass::Ba || next == BreakClass::Hy || next == BreakClass::Ns) {
    return false;  // LB21 × BA × HY × NS（行頭禁則: 々 ゝ ・ ： ；、strict の小書き仮名）
  }
  if (prev == BreakClass::Bb) {
    return false;  // LB21 BB ×
  }
  if (next == BreakClass::In) {
    return false;  // LB22 × IN（分離禁則: …… ‥‥）
  }
  return std::nullopt;
}

// LB23〜LB24: 英数字と接頭辞・接尾辞をくっつける。
std::optional<bool> Analysis::rule_alphanumeric(std::size_t k) const {
  const BreakClass prev = sig_class(k - 1);
  const BreakClass next = sig_class(k);
  if ((prev == BreakClass::Al && next == BreakClass::Nu) ||
      (prev == BreakClass::Nu && next == BreakClass::Al)) {
    return false;  // LB23 AL × NU / NU × AL
  }
  const bool prev_ideograph =
      prev == BreakClass::Id || prev == BreakClass::Eb || prev == BreakClass::Em;
  const bool next_ideograph =
      next == BreakClass::Id || next == BreakClass::Eb || next == BreakClass::Em;
  if ((prev == BreakClass::Pr && next_ideograph) || (prev_ideograph && next == BreakClass::Po)) {
    return false;  // LB23a PR × ID / ID × PO
  }
  const bool prev_affix = prev == BreakClass::Pr || prev == BreakClass::Po;
  const bool next_affix = next == BreakClass::Pr || next == BreakClass::Po;
  if ((prev_affix && next == BreakClass::Al) || (prev == BreakClass::Al && next_affix)) {
    return false;  // LB24 (PR | PO) × AL / AL × (PR | PO)
  }
  return std::nullopt;
}

// LB25: 数値の内部では割らない（￥1,000 や 12.5% を分離しない）。JLREQ 3.1.1 の分離禁則。
std::optional<bool> Analysis::rule_number(std::size_t k) const {
  const BreakClass prev = sig_class(k - 1);
  const BreakClass next = sig_class(k);
  const bool prev_affix = prev == BreakClass::Pr || prev == BreakClass::Po;
  const bool prev_close = prev == BreakClass::Cl || prev == BreakClass::Cp;
  if (next == BreakClass::Po || next == BreakClass::Pr) {
    // NU (SY | IS)* × PO / PR と NU (SY | IS)* (CL | CP) × PO / PR
    if (numeric_run_ends_at(k - 1) || (prev_close && k >= 2 && numeric_run_ends_at(k - 2))) {
      return false;
    }
  }
  if (prev_affix && next == BreakClass::Op && k + 1 < sig_count()) {
    // PO / PR × OP NU と PO / PR × OP IS NU
    const bool digit = sig_class(k + 1) == BreakClass::Nu;
    const bool point = sig_class(k + 1) == BreakClass::Is && k + 2 < sig_count() &&
                       sig_class(k + 2) == BreakClass::Nu;
    if (digit || point) {
      return false;
    }
  }
  if (next == BreakClass::Nu && (prev_affix || prev == BreakClass::Hy || prev == BreakClass::Is ||
                                 numeric_run_ends_at(k - 1))) {
    return false;  // PO / PR / HY / IS × NU、NU (SY | IS)* × NU
  }
  return std::nullopt;
}

// items_[p] の直前で割ると「離してはいけない組」を割ることになるか。
// 緊急分割（break_anywhere）の位置選びだけに使う判定で、通常の分割可能位置の判定
// （can_break_between）とは別物。対象は JIS X 4051 の分離禁則:
//   * ——（B2 の並び。LB17 B2 SP* × B2）
//   * …… ‥‥（IN の並び）。並びの *直前* は分離禁則ではないので割ってよい
//   * 数値の内部と、数値と前置・後置記号（LB23a / LB25）
//   * 呼び出し側が指定した no_break_before
bool Analysis::splits_inseparable(std::size_t p) const {
  if (items_[p].no_break_before) {
    return true;
  }
  const std::size_t k = sig_pos_[p];
  if (k == kNone || k == 0) {
    return false;
  }
  const BreakClass prev = sig_class(k - 1);
  const BreakClass next = sig_class(k);
  if (next == BreakClass::B2 && before_spaces_is(k, BreakClass::B2)) {
    return true;  // —— の途中
  }
  if (prev == BreakClass::In && next == BreakClass::In) {
    return true;  // …… ‥‥ の途中
  }
  const bool prev_ideograph =
      prev == BreakClass::Id || prev == BreakClass::Eb || prev == BreakClass::Em;
  const bool next_ideograph =
      next == BreakClass::Id || next == BreakClass::Eb || next == BreakClass::Em;
  if ((prev == BreakClass::Pr && next_ideograph) || (prev_ideograph && next == BreakClass::Po)) {
    return true;  // LB23a: 通貨記号・単位記号と表意文字を離さない
  }
  const std::optional<bool> number = rule_number(k);
  return number.has_value() && !*number;  // LB25: 数値と前置・後置記号を離さない
}

// LB28〜LB30b: 欧文の語・括弧・地域表示記号・絵文字。
std::optional<bool> Analysis::rule_letter(std::size_t k) const {
  const BreakClass prev = sig_class(k - 1);
  const BreakClass next = sig_class(k);
  if (prev == BreakClass::Al && next == BreakClass::Al) {
    return false;  // LB28 AL × AL（欧文の語は空白でしか割らない）
  }
  if (prev == BreakClass::Is && next == BreakClass::Al) {
    return false;  // LB29 IS × AL（'e.g.'）
  }
  if ((prev == BreakClass::Al || prev == BreakClass::Nu) && next == BreakClass::Op &&
      !is_east_asian_bracket(sig_cp(k))) {
    return false;  // LB30 (AL | NU) × [OP - $EastAsian]（'person(s)'）
  }
  if (prev == BreakClass::Cp && (next == BreakClass::Al || next == BreakClass::Nu) &&
      !is_east_asian_bracket(sig_cp(k - 1))) {
    return false;  // LB30 [CP - $EastAsian] × (AL | NU)
  }
  if (prev == BreakClass::Ri && next == BreakClass::Ri) {
    // LB30a: 直前に並ぶ RI が奇数個ならその 2 個で 1 組になるので割らない
    std::size_t run = 0;
    std::size_t j = k;
    while (j > 0 && sig_class(j - 1) == BreakClass::Ri) {
      ++run;
      --j;
    }
    if (run % 2 == 1) {
      return false;
    }
  }
  if (prev == BreakClass::Eb && next == BreakClass::Em) {
    return false;  // LB30b EB × EM（肌色修飾子）
  }
  return std::nullopt;
}

bool Analysis::can_break_between(std::size_t k) const {
  if (const std::optional<bool> result = rule_core(k); result.has_value()) {
    return *result;
  }
  if (const std::optional<bool> result = rule_punctuation(k); result.has_value()) {
    return *result;
  }
  if (const std::optional<bool> result = rule_alphanumeric(k); result.has_value()) {
    return *result;
  }
  if (const std::optional<bool> result = rule_number(k); result.has_value()) {
    return *result;
  }
  if (const std::optional<bool> result = rule_letter(k); result.has_value()) {
    return *result;
  }
  return true;  // LB31 ALL ÷
}

void Analysis::compute_opportunities() {
  // LB2 sot ×: 先頭では割らない（opp_[0] は常に false）。
  for (std::size_t k = 1; k < sig_count(); ++k) {
    const std::size_t i = sig_[k];
    const BreakClass prev = sig_class(k - 1);
    // LB5 CR × LF: CR と LF の間では改行しない（改行するのは LF の後ろ）。
    const bool cr_lf = prev == BreakClass::Cr && sig_class(k) == BreakClass::Lf;
    const bool hard = is_hard(prev) && !cr_lf;
    mandatory_[i] = static_cast<std::uint8_t>(hard);
    opp_[i] = static_cast<std::uint8_t>(can_break_between(k));
  }

  for (std::size_t i = 1; i < items_.size(); ++i) {
    if (mandatory_[i] != 0) {
      continue;  // LB4 / LB5 は非適合化できない。禁則や no_break_before より強い
    }
    if (raw_zwj_[i - 1] != 0) {
      opp_[i] = 0;  // LB8a ZWJ ×（LB9 で吸収したあとも直後では割らない）
    }
    // 行頭禁則の最終保証（DESIGN.md Phase 4「行頭に句読点が絶対に出ない」）。
    // UAX #14 では LB18（SP ÷）が LB21（× NS）より先に効くので、空白の直後だけは
    // NS の前で割れてしまう。日本語組版ではそれを許さない。
    if (start_prohibited_[i] != 0 || items_[i].no_break_before) {
      opp_[i] = 0;
    }
  }
}

bool Analysis::strippable(std::size_t i) const {
  return items_[i].kind == ItemKind::ForcedBreak || cls_[i] == BreakClass::Sp || is_hard(cls_[i]);
}

std::size_t Analysis::strip_trailing(std::size_t begin, std::size_t end) const {
  std::size_t content_end = end;
  while (content_end > begin && strippable(content_end - 1)) {
    --content_end;
  }
  return content_end;
}

float Analysis::lay_out(std::size_t begin, std::size_t content_end, bool end_trim, float squeeze,
                        std::vector<Spacing>* out) const {
  float width = 0.0F;
  for (std::size_t i = begin; i < content_end; ++i) {
    float before = i > begin ? collapse_before_[i] : 0.0F;
    float after = i + 1 < content_end ? collapse_after_[i] : 0.0F;
    if (i == begin && config_.trim_line_start && punct_[i] == PunctKind::Open) {
      before = trim_before_[i];  // 天付き（行頭の始め括弧の前のアキを捨てる）
    }
    if (i + 1 == content_end && end_trim && is_closing_group(punct_[i])) {
      after = trim_after_[i];  // 行末の終わり括弧・句読点の後ろのアキを捨てる
    }
    if (squeeze > 0.0F) {
      before += std::max(trim_before_[i] - before, 0.0F) * squeeze;
      after += std::max(trim_after_[i] - after, 0.0F) * squeeze;
    }
    if (out != nullptr) {
      (*out)[i] = Spacing{-before, -after};
    }
    width += items_[i].advance - before - after;
  }
  return width;
}

float Analysis::squeeze_pool(std::size_t begin, std::size_t content_end, bool end_trim) const {
  float pool = 0.0F;
  for (std::size_t i = begin; i < content_end; ++i) {
    float before = i > begin ? collapse_before_[i] : 0.0F;
    float after = i + 1 < content_end ? collapse_after_[i] : 0.0F;
    if (i == begin && config_.trim_line_start && punct_[i] == PunctKind::Open) {
      before = trim_before_[i];
    }
    if (i + 1 == content_end && end_trim && is_closing_group(punct_[i])) {
      after = trim_after_[i];
    }
    pool += std::max(trim_before_[i] - before, 0.0F) + std::max(trim_after_[i] - after, 0.0F);
  }
  return pool;
}

Analysis::Fit Analysis::fit(std::size_t begin, std::size_t end, float available) const {
  Fit result;
  result.content_end = strip_trailing(begin, end);
  result.width = lay_out(begin, result.content_end, false, 0.0F, nullptr);
  if (config_.trim_line_end && result.width > available + kWidthEpsilon) {
    const float trimmed = lay_out(begin, result.content_end, true, 0.0F, nullptr);
    if (trimmed < result.width) {
      result.width = trimmed;
      result.end_trim = true;
    }
  }
  return result;
}

std::size_t Analysis::mandatory_limit(std::size_t begin) const {
  for (std::size_t i = begin + 1; i < items_.size(); ++i) {
    if (mandatory_[i] != 0) {
      return i;
    }
  }
  return items_.size();
}

void Analysis::scan_candidates(std::size_t begin, std::size_t limit, float available,
                               std::size_t& best, std::size_t& first, std::size_t& next) const {
  best = kNone;
  first = kNone;
  next = kNone;
  float raw = 0.0F;        // [begin, e) の幅（アキ詰め込み。行末の空白も含む）
  float tail = 0.0F;       // 末尾に溜まった空白・ForcedBreak の幅
  float pool_hint = 0.0F;  // 追い込みで詰められる量の上限
  float hang_hint = 0.0F;  // ぶら下げで外に出せる量の上限
  for (std::size_t e = begin + 1; e <= limit; ++e) {
    const std::size_t i = e - 1;
    raw += items_[i].advance;
    if (i > begin) {
      raw -= collapse_before_[i] + collapse_after_[i - 1];
    }
    tail = strippable(i) ? tail + items_[i].advance : 0.0F;
    pool_hint += trim_before_[i] + trim_after_[i];
    hang_hint = std::max(hang_hint, items_[i].advance);

    if (e != limit && opp_[e] == 0) {
      // 候補でない位置。どう詰めても収まらないところまで来たら走査を打ち切る
      // （長い分割不能列の後ろを毎行なめ直さないため。幅の単調増加を使う）。
      if (first != kNone && raw - tail - pool_hint - hang_hint > available + kWidthEpsilon) {
        return;
      }
      continue;
    }
    if (first == kNone) {
      first = e;
    }
    if (fit(begin, e, available).width <= available + kWidthEpsilon) {
      best = e;
    } else {
      next = best != kNone ? e : kNone;
      return;
    }
  }
}

std::size_t Analysis::break_anywhere_at(std::size_t begin, std::size_t limit,
                                        float available) const {
  // overflow-wrap: anywhere。クラスタ境界で強制的に割る（line_breaker.hpp の
  // Config::break_anywhere）。禁則は守れる限り守るので、幅に収まる位置を次の優先順位で選ぶ。
  // 各段の中では「収まる最後の位置」= できるだけ長い行を採る。
  //   0: 通常の分割可能位置（ここに来た時点で普通は無いが、あれば最優先）
  //   1: 分離禁則にも行頭禁則・行末禁則にも掛からない位置
  //   2: 分離禁則に掛からない位置（「は|……」のように、並びの直前で割る）
  //   3: 収まる最後のクラスタ境界。ここで初めて禁則を破る（—— が 1 行に収まらない等）
  // クラスタの内部（結合文字・異体字セレクタ・ZWJ 列の吸収）では、3 でも絶対に割らない。
  std::array<std::size_t, 4> choice{kNone, kNone, kNone, kNone};
  for (std::size_t p = begin + 1; p < limit; ++p) {
    if (attached_[p] != 0 || raw_zwj_[p - 1] != 0) {
      continue;
    }
    if (fit(begin, p, available).width > available + kWidthEpsilon) {
      break;
    }
    choice[3] = p;
    if (splits_inseparable(p)) {
      continue;
    }
    choice[2] = p;
    if (start_prohibited_[p] != 0 || end_prohibited_[p - 1] != 0) {
      continue;
    }
    choice[1] = p;
    if (opp_[p] != 0) {
      choice[0] = p;
    }
  }
  for (const std::size_t p : choice) {
    if (p != kNone) {
      return p;
    }
  }
  // 1 クラスタも収まらない幅。それでもクラスタ 1 個だけを置く（その行ははみ出す）。
  for (std::size_t p = begin + 1; p < limit; ++p) {
    if (attached_[p] == 0 && raw_zwj_[p - 1] == 0) {
      return p;
    }
  }
  return kNone;
}

bool Analysis::ends_forced(std::size_t end) const {
  const Item& last = items_[end - 1];
  return last.kind == ItemKind::ForcedBreak || is_hard(cls_[end - 1]);
}

void Analysis::emit_line(std::size_t begin, std::size_t end, float available, Breaks& out) const {
  const Fit measured = fit(begin, end, available);
  Line line;
  line.begin = begin;
  line.end = end;
  line.content_end = measured.content_end;
  line.width = lay_out(begin, measured.content_end, measured.end_trim, 0.0F, &out.spacing);
  line.forced = ends_forced(end);
  line.overflows = line.width > available + kWidthEpsilon;
  out.lines.push_back(line);
}

bool Analysis::try_hang(std::size_t begin, std::size_t next, float available, Breaks& out) const {
  // ぶら下げ（JLREQ 3.8.2）: 行末に来た句読点 1 文字だけを行の外に出す。
  const std::size_t content_end = strip_trailing(begin, next);
  if (content_end <= begin + 1) {
    return false;
  }
  const std::size_t last = content_end - 1;
  if (items_[last].kind != ItemKind::Text || !is_hanging_punctuation(items_[last].cp)) {
    return false;  // 句読点以外はぶら下げない（追い出しになる）
  }
  const float full = lay_out(begin, content_end, false, 0.0F, nullptr);
  const float hang = items_[last].advance - collapse_before_[last];
  const float body = full - hang;
  if (body > available + kWidthEpsilon) {
    return false;
  }

  lay_out(begin, content_end, false, 0.0F, &out.spacing);
  Line line;
  line.begin = begin;
  line.end = next;
  line.content_end = content_end;
  line.width = body;
  line.hang = hang;
  line.forced = ends_forced(next);
  line.overflows = false;
  out.lines.push_back(line);
  return true;
}

bool Analysis::try_squeeze(std::size_t begin, std::size_t next, float available,
                           Breaks& out) const {
  // 追い込み（JLREQ 3.8.1）: 行内の約物のアキを詰めて次の分割可能位置まで引き込む。
  const Fit measured = fit(begin, next, available);
  const float needed = measured.width - available;
  if (needed <= 0.0F) {
    return false;
  }
  const float pool = squeeze_pool(begin, measured.content_end, measured.end_trim);
  if (pool <= 0.0F || needed > pool + kWidthEpsilon) {
    return false;  // 詰めきれない → 追い出し
  }
  // 詰め量は各約物の詰め可能量に比例配分する（ARCHITECTURE.md §3.4 (5)）。
  const float squeeze = std::min(needed / pool, 1.0F);
  const float width = lay_out(begin, measured.content_end, measured.end_trim, squeeze, nullptr);
  if (width > available + kWidthEpsilon) {
    return false;
  }

  lay_out(begin, measured.content_end, measured.end_trim, squeeze, &out.spacing);
  Line line;
  line.begin = begin;
  line.end = next;
  line.content_end = measured.content_end;
  line.width = width;
  line.forced = ends_forced(next);
  line.overflows = false;
  out.lines.push_back(line);
  return true;
}

std::vector<bool> Analysis::opportunities() const {
  std::vector<bool> result(items_.size(), false);
  for (std::size_t i = 0; i < items_.size(); ++i) {
    result[i] = opp_[i] != 0;
  }
  return result;
}

Breaks Analysis::break_lines(float available_width) const {
  Breaks result;
  const std::size_t n = items_.size();
  if (n == 0) {
    return result;
  }
  result.spacing.assign(n, Spacing{});

  // kUnbounded（max-content の計測）では ForcedBreak でしか改行しない。
  const bool unbounded = available_width >= kUnbounded;
  std::size_t begin = 0;
  while (begin < n) {
    const std::size_t limit = mandatory_limit(begin);
    if (unbounded) {
      emit_line(begin, limit, available_width, result);
      begin = limit;
      continue;
    }

    std::size_t best = kNone;
    std::size_t first = kNone;
    std::size_t next = kNone;
    scan_candidates(begin, limit, available_width, best, first, next);

    if (best == kNone) {
      // 収まる分割位置がない。break_anywhere が許されていればクラスタ境界で割り、
      // それも駄目なら A4「禁則 > 幅」で、割らずにはみ出す。
      std::size_t end = first != kNone ? first : limit;
      if (config_.break_anywhere) {
        const std::size_t forced = break_anywhere_at(begin, end, available_width);
        if (forced != kNone) {
          end = forced;
        }
      }
      emit_line(begin, end, available_width, result);
      begin = end;
      continue;
    }

    if (next != kNone && config_.overflow == OverflowPolicy::Burasage &&
        try_hang(begin, next, available_width, result)) {
      begin = next;
      continue;
    }
    if (next != kNone && config_.overflow == OverflowPolicy::Oikomi &&
        try_squeeze(begin, next, available_width, result)) {
      begin = next;
      continue;
    }
    emit_line(begin, best, available_width, result);
    begin = best;
  }
  return result;
}

float Analysis::min_content_width() const {
  // 分割不能な最長区間の幅。連続約物のアキ詰めは反映し、行末のアキ詰めは反映しない
  // （min-content はこの幅で必ず収まる上限として使うため、詰める側に倒さない）。
  float widest = 0.0F;
  std::size_t start = 0;
  const std::size_t n = items_.size();
  for (std::size_t i = 1; i <= n; ++i) {
    if (i != n && opp_[i] == 0) {
      continue;
    }
    const std::size_t content_end = strip_trailing(start, i);
    widest = std::max(widest, lay_out(start, content_end, false, 0.0F, nullptr));
    start = i;
  }
  return widest;
}

}  // namespace

LineBreaker::LineBreaker(Config config) : config_(std::move(config)) {}

Breaks LineBreaker::break_lines(std::span<const Item> items, float available_width) const {
  return Analysis(config_, items).break_lines(available_width);
}

float LineBreaker::min_content_width(std::span<const Item> items) const {
  return Analysis(config_, items).min_content_width();
}

std::vector<bool> LineBreaker::break_opportunities(std::span<const Item> items) const {
  return Analysis(config_, items).opportunities();
}

}  // namespace shashoku::linebreak
