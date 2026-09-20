#pragma once

#include <cstdint>

// 計算量の回帰を「時間」ではなく「回数」で捕まえるための計測カウンタ（issue #10-3）。
//
// 約束:
//   * 出力（BoxTree とそのダンプ）には一切影響しない。読むのはテストだけ
//   * グローバル状態・static 変数は持たない。layout() の引数で受け取り、LayoutEngine が参照する
//   * 公開 API（include/shashoku/）には出さない。layout モジュールの内部の道具
namespace shashoku::layout {

struct Counters {
  // LayoutEngine::layout_block() の呼び出し回数（flex / <img> へ委譲したものも含む）。
  std::uint64_t layout_block = 0;
  // LayoutEngine::content_intrinsic() の呼び出し回数（固有寸法の計測）。
  // 本番のレイアウトとは別勘定で shape() を呼ぶので、flex の入れ子（#5）ではここが増える。
  std::uint64_t content_intrinsic = 0;
  // インライン整形文脈の準備（収集 → 空白の畳み込み → シェーピング → アイテム化）の回数。
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
};

}  // namespace shashoku::layout
