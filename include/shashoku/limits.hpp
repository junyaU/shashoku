#pragma once

#include <cstddef>
#include <cstdint>

namespace shashoku {

// 入力に対する上限（ARCHITECTURE.md A25）。`RenderOptions::limits` で渡す。
//
// なぜ要るか: 出力画像が小さくても、途中の段（DOM・スタイル付きツリー・画像のデコード・
// グリフのビットマップ）は入力の大きさに比例したメモリを使う。最終段の画素数だけを
// 見ていても事故は止まらない（font-size: 30000px の 1 文字を 100x100 の画像に描くだけで
// 1.2 GB を確保していた）。
//
// 上限は「入力の一部」なので純粋関数の性質は壊れない（DESIGN.md §3-5）:
// 同じ HTML と同じ RenderLimits からは常に同じ PNG か同じエラーが出る。判定はすべて
// サイズと個数で行い、経過時間や実際のメモリ使用量では行わない（決定的であること）。
//
// 超過は `ErrorKind::LimitExceeded`。message には「どの上限を・いくつに対して・いくつだったか」と
// ここのどのフィールドで変えられるかが入り、入力位置が分かるものには location が付く。
//
// **0 は「無制限」ではない**。無制限を表す特別な値は用意していないので、実質的に外したいときは
// その型の最大値を入れる（`limits.text_code_points = SIZE_MAX;`）。外した場合、
// 信頼できない入力に対するメモリの保証はなくなる。
//
// 既定値は「OG 画像（1200x630 @2x、数千文字、画像数枚）には十分広く、事故は止まる」ように選んだ。
// 根拠は ARCHITECTURE.md A25 の表。
struct RenderLimits {
  // ---- (a) 入力を受けた時点（パースより前に数えられるもの）--------------------

  // HTML の入力バイト数。4 MiB は和文 100 万字を超える。
  std::size_t html_bytes = std::size_t{4} * 1024 * 1024;
  // ImageSet に入れられる画像の枚数。
  std::size_t images = 64;

  // ---- (b) 構造と計算値（パース・スタイル解決のあとに数えられるもの）----------

  // 要素の入れ子の深さ（合成ルート `#root` は数えない）。
  std::size_t nesting_depth = 256;
  // DOM のノード数（要素 + テキスト。合成ルートは数えない）。
  std::size_t dom_nodes = 20000;
  // テキストノードの総コードポイント数（`<style>` の中身は数えない）。
  std::size_t text_code_points = 50000;
  // `<style>` から読んだ規則の総数（セレクタの照合が 規則数 x ノード数 になるため）。
  std::size_t style_rules = 2000;
  // font-size の上限（**デバイスピクセル** = 計算値の font-size x scale）。
  // グリフのビットマップは pixel_size の 2 乗で大きくなる。`<rt>` を含む全要素が対象。
  float font_size_device_px = 2048.0F;
  // 長さ・座標の絶対値の上限（CSS px）。計算値化で `em` を掛けた結果や、px で直接書かれた
  // 値がこれを超えたら `LimitExceeded`（ARCHITECTURE.md A36）。
  //
  // 2^24 を選んだ根拠:
  //   - 出力の絶対上限（1 辺 2^32-1 px）より十分小さいので、これ以下の長さだけを扱う限り
  //     座標の加算が出力の表せる範囲を大きく踏み越えることはない
  //   - `dom_nodes` = 20,000 段ぶん足しても 2^24 x 2x10^4 = 3.4x10^11 で、float の上限
  //     3.4x10^38 に遠く届かない（入れ子で足し込んでも inf / NaN にならない）
  //   - 2^24 は float が整数を 1 刻みで表せる上限でもあるので、この範囲の長さは
  //     整数部が丸められない
  float length_px = 16777216.0F;  // 2^24
  // RenderOptions::scale の上限。
  float scale = 256.0F;

  // ---- (c) 大きな確保の直前（確保する前に判定する）---------------------------

  // 画像 1 枚あたりの画素数。IHDR を読んだ時点で、画素を確保する前に判定する。
  std::uint64_t image_pixels = std::uint64_t{1} << 24U;  // 16,777,216（4096x4096、64 MB）
  // 全画像の合計画素数。
  std::uint64_t total_image_pixels = std::uint64_t{1} << 25U;  // 33,554,432（128 MB）
  // 出力ビットマップのデバイス画素数（幅 x 高さ、scale 込み）。
  std::uint64_t device_pixels = std::uint64_t{1} << 26U;  // 67,108,864（256 MB）

  // ---- (d) 診断（ARCHITECTURE.md A46）---------------------------------------------

  // 集める診断（エラー + 警告）の件数の上限。達したら記録をやめて解析は続け、
  // `RenderFailure::truncated` / `RenderResult::diagnostics_truncated` を立てる。
  // 大量の不正な入力で診断そのものがメモリの消費源にならないための上限。
  // **実効の下限は 1**: 0 を渡しても最初の 1 件は記録する（エラーが 1 件も記録されずに
  // 「成功」になる穴を作らないため。他の上限と同じく 0 は「無制限」ではない）。
  std::size_t max_diagnostics = 100;

  bool operator==(const RenderLimits&) const = default;
};

}  // namespace shashoku
