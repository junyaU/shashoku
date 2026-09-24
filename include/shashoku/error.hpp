#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shashoku/source_location.hpp"
#include "shashoku/warning.hpp"

namespace shashoku {

enum class ErrorKind : std::uint8_t {
  InvalidUtf8,     // 入力が正しい UTF-8 でない
  HtmlParse,       // HTML の構文エラー（閉じ忘れ、対応しない終了タグなど）
  UnsupportedTag,  // 対応外のタグ
  UnsupportedAttribute,  // 対応外の属性
  CssParse,              // CSS の構文エラー
  UnsupportedProperty,   // 対応外の CSS プロパティ
  UnsupportedValue,  // プロパティは対応しているが値が対応外（単位・キーワード）
  UnsupportedLayout,  // 対応外のレイアウト（inline への padding、直交する writing-mode など）
  FontLoad,       // フォントのバイト列を解釈できない
  NoFonts,        // FontSet が空
  ImageDecode,    // 画像のバイト列を解釈できない / 対応外の形式
  ImageNotFound,  // <img src> に対応する画像が ImageSet にない
  InvalidOption,  // RenderOptions の値が不正
  LimitExceeded,  // 入力が RenderLimits の上限を超えた（limits.hpp）
  OutOfMemory,  // メモリを確保できなかった（最善努力。limits.hpp / ARCHITECTURE.md A26）
  Internal,  // ここに来たら shashoku のバグ
  WarningAsError,  // `warnings_as_errors` で警告を格上げしたもの。元の種類は `RenderError::warning`
};

// 診断 1 件。fail loudly の原則（DESIGN.md §3-6、ARCHITECTURE.md A46）により、
// message には「どの入力の何が原因か」を必ず含め、入力由来のエラーには location を付ける。
// 機械が頼ってよい（安定した契約）のは kind / warning / location。message と hint は
// 人（と AI）が読むための文面で、版が変われば変わりうる。
struct RenderError {
  ErrorKind kind = ErrorKind::Internal;
  std::string message;
  std::optional<SourceLocation> location;
  // 「代わりにどう書くか」。shashoku で同じ結果が出せると確かめた代替だけを書く。無ければ空。
  // message には混ぜない（機械側が分けて読める。CLI は別の行に出す）。
  std::string hint;
  // kind == WarningAsError のとき、格上げ前の警告の種類。それ以外は nullopt
  std::optional<WarningKind> warning;

  bool operator==(const RenderError&) const = default;
};

// render() / dump() の失敗。①（HTML）と②（スタイル）の段は見つけた問題を**集めてから**失敗し、
// errors に全部入れる。③ 以降は 1 件目で止まる（ARCHITECTURE.md A46）。
struct RenderFailure {
  // 1 件以上。入力位置の昇順（位置なしは末尾）→ kind → message で安定に整列してある。
  std::vector<RenderError> errors;
  // 失敗するまでに集まった警告。`warnings_as_errors` で格上げしたものは errors 側に移し、
  // ここには残さない。
  std::vector<Warning> warnings;
  // errors と warnings の合計が `RenderLimits::max_diagnostics` に達して記録を打ち切った
  bool truncated = false;

  bool operator==(const RenderFailure&) const = default;
};

// "unsupported-property" のようなケバブケースの識別子
std::string_view to_string(ErrorKind kind) noexcept;

// 例: "error[unsupported-property] at 3:14: `float` is not supported"
// hint は含めない（含めるのは to_string(RenderFailure) と CLI）。
// WarningAsError の message は警告の detail から末尾の " at L:C" を除いたもの（位置は location
// にある。 文面に位置が二重に出ないようにする）。
std::string to_string(const RenderError& error);

// 1 行 1 件。各エラーは to_string(RenderError) の行、hint があれば続けて "  hint: <hint>" の行、
// 警告は "warning[<kind>]: <detail>" の行。truncated なら最後に
// "(diagnostics truncated at <n>)" の行。末尾に改行は付けない。
std::string to_string(const RenderFailure& failure);

}  // namespace shashoku
