#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "shashoku/limits.hpp"

namespace shashoku {

// 行に収まらなかったときの処理（DESIGN.md §5 / JIS X 4051・JLREQ）。
enum class OverflowPolicy : std::uint8_t {
  Oidashi,  // 追い出し: 禁則に掛かる文字を道連れにして次の行へ送る（既定）
  Oikomi,  // 追い込み: 約物の空きを詰めて行内に収める。詰めきれなければ追い出し
  Burasage,  // ぶら下げ: 行末の句読点 1 文字を行の外にはみ出させる。対象外の文字なら追い出し
};

// CSS の line-break（CSS Text Level 3 §5.3）。小書きの仮名・長音の前で
//   Strict: 割らない（JIS X 4051 の標準的な行頭禁則）
//   Normal: 割ってよい
//   Loose : Normal に加えて繰り返し記号（々 ゝ ゞ）などの前でも割ってよい
enum class LineBreakStrictness : std::uint8_t { Strict, Normal, Loose };

// 行分割器の設定。CSS で指定できないもの（利用者のポリシー）をここに置く。
// CSS の `line-break` を書いた要素では strictness がその値で上書きされる。
struct LineBreakConfig {
  OverflowPolicy overflow = OverflowPolicy::Oidashi;
  // CSS の line-break が auto のときの既定
  LineBreakStrictness strictness = LineBreakStrictness::Strict;
  // 約物が連続するときの空きを詰める（JLREQ 3.1.4）。「」」「」が間延びしない
  bool collapse_punctuation_spacing = true;
  // 行末に来た終わり括弧・句読点が収まらないとき、後ろの半角空きを詰めてよい
  bool trim_line_end = true;
  // 行頭に来た始め括弧の前の半角空きを詰める（天付き）
  bool trim_line_start = false;
  // 禁則テーブルへの追加（「この文字も行頭 / 行末に置きたくない」を足す口）
  std::u32string extra_line_start_prohibited;
  std::u32string extra_line_end_prohibited;

  bool operator==(const LineBreakConfig&) const = default;
};

// render() に渡す設定（DESIGN.md §8）。
struct RenderOptions {
  int viewport_width = 1200;           // CSS px。OG 画像の定番サイズを既定にする
  std::optional<int> viewport_height;  // CSS px。未指定なら内容の高さに追従する
  float scale = 1.0F;                  // 2.0 で Retina 向けの 2 倍解像度
  LineBreakConfig line_break;
  RenderLimits limits;  // 入力の上限（limits.hpp）。既定値で OG 画像には十分広い

  // PNG（zlib deflate）の圧縮レベル。0（無圧縮・最速）〜 9（最小・最遅）。範囲外は
  // `InvalidOption`。既定は zlib の既定と同じ 6 で、9 に比べて時間がおよそ半分、
  // ファイルは 6% ほど大きくなる（ARCHITECTURE.md A33 の実測）。
  //
  // レベルは**入力の一部**なので純粋関数の性質は壊れない（DESIGN.md §3-5）:
  // 同じ HTML と同じ RenderOptions からは常にバイト単位で同じ PNG が出る。
  // レベルを変えるとファイルのバイト列は変わるが、デコードした画素は 1 ビットも変わらない。
  int compression_level = 6;

  // 警告（豆腐・紙面からのはみ出し）を失敗にする（ARCHITECTURE.md A46）。
  // true のとき、描画が終わって警告が 1 件以上あれば PNG を返さず `RenderFailure` を返す。
  // errors には警告 1 件につき `ErrorKind::WarningAsError` のエラーが入り、
  // 元の種類・位置・詳細を保つ。サーバーで「検出した問題のある画像は配らない」判断に使う。
  // 既定は false（警告つきで PNG を返す）。
  bool warnings_as_errors = false;

  bool operator==(const RenderOptions&) const = default;
};

}  // namespace shashoku
