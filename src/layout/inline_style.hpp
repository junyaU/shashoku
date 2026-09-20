#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "core/color.hpp"
#include "linebreak/line_breaker.hpp"
#include "style/computed_style.hpp"
#include "text/text_measurer.hpp"

// 平坦化したインライン列の「文字ごとの属性」の表（ARCHITECTURE.md A27 / issue #8, #2）。
//
// インライン整形文脈の中の 1 文字が持つスタイルを **層に分けて** 持つ。層ごとに値の表があり、
// 文字は層ごとの添字の組（CharStyle）1 つだけを指す。いまの層は 3 つ:
//
//   * **シェーピング属性**（text::TextStyle）… font-family / font-weight / font-size / direction。
//     `shape()` はこの層が等しい連続ごとに 1 回だけ呼ぶ。色や line-height の境界で切ると
//     その位置のカーニング・合字が消える（issue #8）
//   * **装飾・行高属性**（DecorationStyle）… color / letter-spacing / line-height。
//     シェーピングの結果を変えない。フラグメントを作るときにクラスタ境界で対応付ける
//   * **行分割ポリシー**（BreakingStyle）… line-break / overflow-wrap。`linebreak::Item` の
//     `strictness` / `break_anywhere`（A23）に写す。見た目には一切効かない
//
// **どの層を見るかは用途ごとに違う**（層を足しても、関係のない処理が細切れにならないように）:
//
//   | 用途 | 見る層 |
//   |---|---|
//   | `shape()` の区間 | シェーピング |
//   | `TextFragment`（= `DrawGlyphs`）の区間 | シェーピング + 装飾 |
//   | 行の高さ | シェーピング + 装飾 |
//   | `linebreak::Item` のポリシー | 行分割ポリシー |
//
// 層を足すときは BreakingStyle と同じ形で表を 1 本増やし、CharStyle に添字を 1 本足して
// intern() で登録し、上の表に「どの用途が見るか」を書く。予定しているもの:
//
//   * **#9**: 元のノードの位置（SourceLocation）→ 豆腐の警告に入力位置を付ける
//     （見た目にもシェーピングにも効かないので、上の表のどの行にも入らない層になる）
//
// 添字の割り当ては intern() を呼んだ順（= 木を辿った順）なので決定的。
// 索引に使う std::map は「同じ内容に同じ添字を与える」ためだけのもので、反復しない。
namespace shashoku::layout {

// シェーピングの結果を変えない属性。
//   color          : TextFragment を分ける（= paint の DrawGlyphs を分ける）
//   letter_spacing : クラスタの送りに足す
//   line_height    : 行の高さに効く
struct DecorationStyle {
  Color color = kBlack;
  float letter_spacing = 0;
  style::LineHeight line_height;

  bool operator==(const DecorationStyle&) const = default;
};

// 行分割器に渡すポリシー（issue #2）。CSS の計算値のまま持ち、`linebreak::Item` に写すときに
// エンジンの既定（A17）で解決する。**見た目にもシェーピングにも効かない。**
struct BreakingStyle {
  style::LineBreak line_break = style::LineBreak::Auto;
  style::OverflowWrap overflow_wrap = style::OverflowWrap::Normal;

  bool operator==(const BreakingStyle&) const = default;
};

// 1 文字が持つ属性の組。層ごとに添字を 1 本ずつ。
struct CharStyle {
  std::size_t shaping = 0;
  std::size_t decoration = 0;
  std::size_t breaking = 0;

  bool operator==(const CharStyle&) const = default;
};

// ComputedStyle からシェーピング属性だけを取り出す（表に登録せずに使う口）。
[[nodiscard]] text::TextStyle shaping_style_of(const style::ComputedStyle& style,
                                               text::Direction direction);

// A17: `line-break: auto` は「エンジンの既定に従う」= `Options::line_break.strictness` を使う。
[[nodiscard]] linebreak::Strictness resolve_strictness(style::LineBreak value,
                                                       linebreak::Strictness fallback);
// `overflow-wrap: anywhere` / `break-word` はどちらも緊急分割を許す
// （両者の区別は `linebreak::Config::break_anywhere` の意味論の問題。A23 の最後）。
[[nodiscard]] bool resolve_break_anywhere(style::OverflowWrap value);

namespace detail {

// std::map の索引に使うだけの順序。**出力には一切使わない**（DESIGN.md §3-5）。
// 呼ばれた回数を数えるのは計測カウンタ（A21）のため。数えた値で分岐はしない。
struct ShapingLess {
  std::uint64_t* probes = nullptr;

  bool operator()(const text::TextStyle& a, const text::TextStyle& b) const {
    ++*probes;
    return std::tie(a.font_weight, a.font_size, a.direction, a.font_family) <
           std::tie(b.font_weight, b.font_size, b.direction, b.font_family);
  }
};

struct DecorationLess {
  std::uint64_t* probes = nullptr;

  bool operator()(const DecorationStyle& a, const DecorationStyle& b) const {
    ++*probes;
    return std::tie(a.color.r, a.color.g, a.color.b, a.color.a, a.letter_spacing,
                    a.line_height.kind, a.line_height.value) <
           std::tie(b.color.r, b.color.g, b.color.b, b.color.a, b.letter_spacing,
                    b.line_height.kind, b.line_height.value);
  }
};

struct BreakingLess {
  std::uint64_t* probes = nullptr;

  bool operator()(const BreakingStyle& a, const BreakingStyle& b) const {
    ++*probes;
    return std::tie(a.line_break, a.overflow_wrap) < std::tie(b.line_break, b.overflow_wrap);
  }
};

}  // namespace detail

class CharStyleTable {
 public:
  // ComputedStyle を層に分けて登録し、文字が指す添字を返す。内容が同じなら同じ添字。
  // 1 ノードにつき 1 回呼ぶ想定で、1 回のスタイルの比較は O(log(表の大きさ))
  // （線形探索だと色違いの span が S 個ある段落で O(S²) になる。issue #10）。
  std::size_t intern(const style::ComputedStyle& style, text::Direction direction);

  // 索引を引くのに行ったスタイルの比較の回数（計測カウンタ用。A21）。
  [[nodiscard]] std::uint64_t probes() const { return *probes_; }

  [[nodiscard]] std::size_t size() const { return styles_.size(); }
  [[nodiscard]] std::size_t shaping_count() const { return shaping_.size(); }

  // 文字の添字 → 層ごとの値。
  [[nodiscard]] std::size_t shaping_index(std::size_t style) const {
    return styles_[style].shaping;
  }
  // 「見た目が同じか」を判定する鍵（= TextFragment を切る単位）。行分割ポリシーは含めない。
  [[nodiscard]] std::size_t decoration_index(std::size_t style) const {
    return styles_[style].decoration;
  }
  [[nodiscard]] const text::TextStyle& shaping(std::size_t style) const {
    return shaping_[styles_[style].shaping];
  }
  [[nodiscard]] const DecorationStyle& decoration(std::size_t style) const {
    return decoration_[styles_[style].decoration];
  }
  [[nodiscard]] const BreakingStyle& breaking(std::size_t style) const {
    return breaking_[styles_[style].breaking];
  }
  // シェーピング属性の添字 → 値（metrics の表を引くときに使う）。
  [[nodiscard]] const text::TextStyle& shaping_at(std::size_t shaping) const {
    return shaping_[shaping];
  }

 private:
  std::vector<text::TextStyle> shaping_;
  std::vector<DecorationStyle> decoration_;
  std::vector<BreakingStyle> breaking_;
  std::vector<CharStyle> styles_;

  // 比較の回数。比較器が指すので、表を move しても指し先が動かないようヒープに置く
  // （宣言順に初期化されるので、索引より先に置くこと）。
  std::unique_ptr<std::uint64_t> probes_ = std::make_unique<std::uint64_t>(0);

  std::map<text::TextStyle, std::size_t, detail::ShapingLess> shaping_index_{
      detail::ShapingLess{probes_.get()}};
  std::map<DecorationStyle, std::size_t, detail::DecorationLess> decoration_index_{
      detail::DecorationLess{probes_.get()}};
  std::map<BreakingStyle, std::size_t, detail::BreakingLess> breaking_index_{
      detail::BreakingLess{probes_.get()}};
  std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::size_t> style_index_;
};

}  // namespace shashoku::layout
