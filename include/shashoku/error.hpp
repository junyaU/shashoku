#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace shashoku {

// 入力 HTML 内の位置。line / column は 1 始まり、column は UTF-8 バイト単位ではなく
// コードポイント単位（エディタの表示と一致させる）。offset は先頭からのバイト数。
struct SourceLocation {
  std::uint32_t offset = 0;
  std::uint32_t line = 1;
  std::uint32_t column = 1;

  bool operator==(const SourceLocation&) const = default;
};

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
};

// 失敗は必ずこの型で返す（例外は投げない）。fail loudly の原則により、
// message には「どの入力の何が原因か」を必ず含め、入力由来のエラーには location を付ける。
struct RenderError {
  ErrorKind kind = ErrorKind::Internal;
  std::string message;
  std::optional<SourceLocation> location;

  bool operator==(const RenderError&) const = default;
};

// "unsupported-property" のようなケバブケースの識別子
std::string_view to_string(ErrorKind kind) noexcept;

// 例: "error[unsupported-property] at 3:14: `float` is not supported"
std::string to_string(const RenderError& error);

}  // namespace shashoku
