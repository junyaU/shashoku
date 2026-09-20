#pragma once

#include <cstdint>

// 約物の空き（JLREQ 3.1.2〜3.1.5 / ARCHITECTURE.md §3.4 (4)）。
//
// 全角の約物は 1em の字送りのうち半分（中点類は両側 1/4 ずつ）が字面のない「空き」になっている。
// 行分割器はその空きを *減らす* 方向にだけ触る（Spacing の負の値）。
// 空きを足す処理（欧文との間の四分アキなど）はレイアウト側の仕事。
namespace shashoku::linebreak {

enum class PunctKind : std::uint8_t {
  None,  // 約物ではない
  Open,  // 始め括弧類 「『（〔［｛〈《【〖〘〝: 空きは字面の左（before を詰められる）
  Close,  // 終わり括弧類 」』）〕］｝〉》】〗〙〟: 空きは字面の右（after を詰められる）
  Comma,  // 読点 、，: 空きは字面の右
  Period,     // 句点 。．: 空きは字面の右
  MiddleDot,  // 中点類 ・：；: 空きは両側 1/4 ずつ
};

[[nodiscard]] PunctKind punct_kind(char32_t cp);

// 字面の前に空きを持つ約物か（始め括弧類・中点類）。
[[nodiscard]] constexpr bool has_space_before(PunctKind kind) {
  return kind == PunctKind::Open || kind == PunctKind::MiddleDot;
}

// 字面の後ろに空きを持つ約物か（終わり括弧類・句読点・中点類）。
[[nodiscard]] constexpr bool has_space_after(PunctKind kind) {
  return kind == PunctKind::Close || kind == PunctKind::Comma || kind == PunctKind::Period ||
         kind == PunctKind::MiddleDot;
}

// JLREQ 3.1.4 のアキ詰めで「後ろの空きを詰める側」になる約物（終わり括弧類・句読点）。
[[nodiscard]] constexpr bool is_closing_group(PunctKind kind) {
  return kind == PunctKind::Close || kind == PunctKind::Comma || kind == PunctKind::Period;
}

// ぶら下げ組みの対象（JLREQ 3.8.2 / ARCHITECTURE.md §3.4 (5)）: 、。，． の 4 文字だけ。
[[nodiscard]] bool is_hanging_punctuation(char32_t cp);

}  // namespace shashoku::linebreak
