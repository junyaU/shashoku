#pragma once

#include <cmath>
#include <format>
#include <string>

// エラーメッセージに数値を入れるときの表記。
namespace shashoku {

// `std::format("{}", value)` を、**環境に依らない**表記に直したもの。
//
// なぜ要るか: NaN の符号ビットは CPU によって違う。`inf / inf` は x86 の SSE では
// 負の NaN、ARM では正の NaN になる。`std::format` はその符号をそのまま出すので、
// 同じ HTML から出たエラーの文面が `-nan` になったり `nan` になったりする。
// PNG のバイト列の決定性（A32）の外だが、文面を見るテストや利用者のスクリプトに響くので、
// **NaN は符号に依らず `NaN`** と書く。
//
// 無限大の符号は入力で決まる（`1e38em x 16px` は `inf`、`-1e38em x 16px` は `-inf`）ので
// 環境に依らない。`std::format` の表記をそのまま使う。
// 有限の値の表記も変えない（`std::format` と 1 文字も違わない）。
//
// 使うのは判定 2 つ（`isnan` / `isinf`）だけなので A9（浮動小数点の決定性）の許可リスト内。
// そもそも**出力（座標・画素）には一切関わらない**。エラーの文面を作るためだけの関数。
inline std::string number_text(float value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  return std::format("{}", value);
}

}  // namespace shashoku
