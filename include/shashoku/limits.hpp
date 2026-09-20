#pragma once

#include <cstddef>
#include <cstdint>

namespace shashoku {

// 入力に対する上限（ARCHITECTURE.md A21）。`RenderOptions::limits` で渡す。
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
// 根拠は ARCHITECTURE.md A21 の表。
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
  // RenderOptions::scale の上限。
  float scale = 256.0F;

  // ---- (c) 大きな確保の直前（確保する前に判定する）---------------------------

  // 画像 1 枚あたりの画素数。IHDR を読んだ時点で、画素を確保する前に判定する。
  std::uint64_t image_pixels = std::uint64_t{1} << 24U;  // 16,777,216（4096x4096、64 MB）
  // 全画像の合計画素数。
  std::uint64_t total_image_pixels = std::uint64_t{1} << 25U;  // 33,554,432（128 MB）
  // 出力ビットマップのデバイス画素数（幅 x 高さ、scale 込み）。
  std::uint64_t device_pixels = std::uint64_t{1} << 26U;  // 67,108,864（256 MB）

  bool operator==(const RenderLimits&) const = default;
};

}  // namespace shashoku
