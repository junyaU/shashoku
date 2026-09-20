#pragma once

#include <cstdint>

#include "linebreak/line_breaker.hpp"

// 計算量の回帰を「時間」ではなく「回数」で捕まえるための計測カウンタ（issue #10-3）。
//
// 約束:
//   * 出力（BoxTree とそのダンプ）には一切影響しない。読むのはテストだけ
//   * グローバル状態・static 変数は持たない。layout() の引数で受け取り、LayoutEngine が参照する
//   * 公開 API（include/shashoku/）には出さない。layout モジュールの内部の道具
namespace shashoku::layout {

// 数えるのは「実際に行った仕事」で、呼び出し回数ではない（A29 のメモが効いた分は増えない）。
// メモが効いているかどうかは、この 3 つが入れ子の深さでどう増えるかで見る。
struct Counters {
  // LayoutEngine::layout_block() が実際に箱を組んだ回数（flex / <img> へ委譲したものも含む）。
  std::uint64_t layout_block = 0;
  // 固有寸法（content_intrinsic）を実際に計算した回数。
  std::uint64_t content_intrinsic = 0;
  // インライン整形文脈の準備（収集 → 空白の畳み込み → シェーピング → アイテム化）の回数。
  // 同じ段落は計測と配置で共有するので、ふつうは「段落の数」に一致する（A6 / A29）。
  std::uint64_t inline_prepare = 0;
  // TextMeasurer::shape() の呼び出し回数と、渡した文字数（コードポイント）の合計。
  std::uint64_t shape_calls = 0;
  std::uint64_t shaped_chars = 0;
  // TextMeasurer::metrics() の呼び出し回数。
  std::uint64_t metrics_calls = 0;
  // 組み上げた行ボックスの数（1 行あたりの作業量を見るときの分母）。
  std::uint64_t line_boxes = 0;
  // 行の構築で確保・初期化した作業バッファの要素数の合計。
  // 行ごとに段落全体ぶんを確保していると（#4 の O(N×L)）ここが行数×アイテム数に膨らむ。
  std::uint64_t line_scratch = 0;
  // インライン背景の構築で調べた（行, 背景スコープ）の組の数。
  // 行ごとに全スコープを舐めていると（#4）ここが行数×スコープ数に膨らむ。
  std::uint64_t background_probes = 0;
  // 文字ごとの属性の表（inline_style.hpp）を引くのに行ったスタイルの比較の回数。
  // 登録のたびに既存のスタイルを線形探索していると、色違いの span が S 個ある段落で
  // ここが S² に膨らむ（issue #10 の L1 からの申し送り）。二分探索なら S×log S。
  std::uint64_t style_probes = 0;
  // 行分割器の中の作業量（A24）。linebreak は何にも依存しないので自前のカウンタを持っており、
  // layout は break_lines() / min_content_width() / break_opportunities() にこれを渡して
  // 足し込む。layout 側の線形性（A22）と合わせて「1 つの IFC の仕事は N に線形」を検査する。
  linebreak::Counters line_breaker;
};

}  // namespace shashoku::layout
