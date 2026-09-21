// 性質テスト（issue #10-2c）: 「書き方を変えても組版は変わらない」もの。
//
// ゴールデンは「以前と同じ絵が出る」ことしか見ないので、**入力の書き方を変えたときに
// 結果が変わってしまう**たぐいの壊れ方は捕まえられない（#8 の「色だけの span を挟むと
// グリフ位置が動く」がまさにそれだった）。ここでは 2 通りの書き方をそれぞれ組んで、
// ボックスツリーのダンプが 1 文字も違わないことを確かめる。
//
// 比較は dump_json()（DESIGN.md §3-3 の「各段はダンプ可能」）。ダンプは論理座標と
// グリフ位置と入力位置を全部含むので、これが一致すれば絵も一致する。

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/result.hpp"
#include "layout/layout.hpp"
#include "layout/test_support.hpp"
#include "shashoku/error.hpp"
#include "style/computed_style.hpp"

namespace shashoku::layout::test {
namespace {

using style::ComputedStyle;

std::string dump_of(const style::StyledNode& root, float inline_size, FakeMeasurer& measurer) {
  const Result<BoxTree> tree = run_layout(root, inline_size, measurer);
  if (!tree) {
    ADD_FAILURE() << "layout: " << to_string(tree.error());
    return {};
  }
  return dump_json(*tree);
}

// 2 つのスタイル付きツリーを同じ幅で組み、ダンプが一致することを確かめる。
::testing::AssertionResult same_dump(const style::StyledNode& a, const style::StyledNode& b,
                                     float inline_size) {
  FakeMeasurer measurer_a;
  FakeMeasurer measurer_b;
  const std::string dump_a = dump_of(a, inline_size, measurer_a);
  const std::string dump_b = dump_of(b, inline_size, measurer_b);
  if (dump_a.empty() || dump_b.empty()) {
    return ::testing::AssertionFailure() << "レイアウトに失敗した";
  }
  if (dump_a == dump_b) {
    return ::testing::AssertionSuccess();
  }
  // 最初に食い違う位置を出す（ダンプは長いので全文は出さない）
  std::size_t at = 0;
  while (at < dump_a.size() && at < dump_b.size() && dump_a[at] == dump_b[at]) {
    ++at;
  }
  const std::size_t from = at > 60 ? at - 60 : 0;
  return ::testing::AssertionFailure() << "ボックスツリーのダンプが違う（" << at
                                       << " バイト目から）:\n  A: " << dump_a.substr(from, 160)
                                       << "\n  B: " << dump_b.substr(from, 160);
}

constexpr float kWidth = 160;

// ---- 既定値を明示的に書いても変わらない -------------------------------------------

TEST(LayoutInvariance, WritingTheInheritedDefaultsChangesNothing) {
  const auto plain = build({block({text("あいうえおかきくけこさしすせそ")})});
  const auto explicit_defaults =
      build({block({text("あいうえおかきくけこさしすせそ")}, [](ComputedStyle& s) {
        s.letter_spacing = 0;
        s.text_align = style::TextAlign::Start;
        s.line_break = style::LineBreak::Auto;
        s.overflow_wrap = style::OverflowWrap::Normal;
        s.line_height = style::LineHeight{};
      })});
  EXPECT_TRUE(same_dump(plain, explicit_defaults, kWidth));
}

// A17: `line-break: auto` は「エンジンの既定に従う」。偽の測定器のテストでは
// Options::line_break.strictness の既定が Strict なので、`strict` と同じ結果になる。
TEST(LayoutInvariance, LineBreakAutoMatchesTheEngineDefault) {
  const auto with_auto =
      build({block({text("このテキストは、行末に「約物」が来る。")},
                   [](ComputedStyle& s) { s.line_break = style::LineBreak::Auto; })});
  const auto with_strict =
      build({block({text("このテキストは、行末に「約物」が来る。")},
                   [](ComputedStyle& s) { s.line_break = style::LineBreak::Strict; })});
  EXPECT_TRUE(same_dump(with_auto, with_strict, 120));
}

// 横書きでは text-align の start と left、end と right は同じ扱い（inline_layout.cpp）。
TEST(LayoutInvariance, StartEqualsLeftAndEndEqualsRight) {
  const auto make = [](style::TextAlign align) {
    return build({block({text("あいうえおかきくけこさしすせそたちつてと")},
                        [align](ComputedStyle& s) { s.text_align = align; })});
  };
  EXPECT_TRUE(same_dump(make(style::TextAlign::Start), make(style::TextAlign::Left), kWidth));
  EXPECT_TRUE(same_dump(make(style::TextAlign::End), make(style::TextAlign::Right), kWidth));
}

// ---- 入力の切り方を変えても変わらない -----------------------------------------------

// 素の <span>（スタイルを 1 つも足さない）で包んでも、何も変わってはいけない。
// #8 の「色だけの span を挟むとグリフ位置が動く」の、装飾すら無い版。
TEST(LayoutInvariance, WrappingInAPlainSpanChangesNothing) {
  const auto plain = build({block({text("あいうえお、かきくけこ。さしすせそ")})});
  const auto wrapped = build({block({inline_box({text("あいうえお、かきくけこ。さしすせそ")})})});
  EXPECT_TRUE(same_dump(plain, wrapped, kWidth));

  // 途中だけ包んだ場合も同じ（断片の境界は span ではなくフォントで決まる。A27）
  const auto partly = build(
      {block({text("あいうえお、"), inline_box({text("かきくけこ。")}), text("さしすせそ")})});
  EXPECT_TRUE(same_dump(plain, partly, kWidth));
}

// テキストノードの切れ目は組版に影響しない（平らにしてから 1 回で処理する。
// inline_collect.cpp のコメント）。入力位置が同じになるよう at() は使わない。
TEST(LayoutInvariance, SplittingATextNodeChangesNothing) {
  const auto whole = build({block({text("吾輩は猫である。名前はまだ無い。")})});
  const auto split = build({block({text("吾輩は"), text("猫である。名"), text("前はまだ無い。")})});
  EXPECT_TRUE(same_dump(whole, split, 140));
}

// 空白の畳み込みは冪等: 畳み込んだ後の文字列を入れても同じ結果になる。
// （ASCII の改行は空白 1 個になる。全角どうしの改行を消す A14 とは別の経路なので、
//   ここでは A14 に掛からない欧文で見る）
TEST(LayoutInvariance, CollapsingWhitespaceIsIdempotent) {
  const auto raw = build({block({text("shashoku  is   a\n\tJapanese \n typesetting  engine")})});
  const auto collapsed = build({block({text("shashoku is a Japanese typesetting engine")})});
  EXPECT_TRUE(same_dump(raw, collapsed, 200));
}

// A14: 全角どうしの間の改行は消える（空白にならない）。こちらも冪等。
TEST(LayoutInvariance, CollapsingIsIdempotentForFullwidthText) {
  const auto raw = build({block({text("吾輩は猫である。\n名前はまだ無い。")})});
  const auto collapsed = build({block({text("吾輩は猫である。名前はまだ無い。")})});
  EXPECT_TRUE(same_dump(raw, collapsed, 140));
}

// ---- 入れ子の書き方を変えても変わらない ---------------------------------------------

// 無名ブロックは「インラインの連続」を包むだけなので、明示的な div で包んだ場合と
// 同じ座標になる…とは限らない（div は自分の塗りを持てる）。ここで見るのは
// 「インラインとブロックを混ぜたときの無名ブロックの切れ目」が書き方に依らないこと。
TEST(LayoutInvariance, AnonymousBlocksSplitAtTheSamePlaces) {
  const auto a = build(
      {block({text("あいう"), br(), text("えおか"), block({text("きくけ")}), text("こさし")})});
  const auto b = build({block({inline_box({text("あいう"), br(), text("えおか")}),
                               block({text("きくけ")}), inline_box({text("こさし")})})});
  EXPECT_TRUE(same_dump(a, b, kWidth));
}

}  // namespace
}  // namespace shashoku::layout::test
