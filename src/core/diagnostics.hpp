#pragma once

#include <cstddef>
#include <vector>

#include "shashoku/error.hpp"
#include "shashoku/warning.hpp"

namespace shashoku {

// 集める診断（ARCHITECTURE.md A46）。①（html）と②（style）が「安全に解析を続けられる問題」を
// ここに足して続行し、api が段の終わりで `errors()` を見て `RenderFailure` にする。
// 致命（InvalidUtf8 / HtmlParse の構造の破損 / LimitExceeded / 資源の失敗）は今までどおり
// `Result<T>` の unexpected で返し、ここには入れない（api が集めたものと合わせる）。
//
// 上限: errors と warnings の合計が max_entries に達したら、それ以降は記録せず truncated を立てる
// （解析は続ける。時間と入力の大きさは RenderLimits の他の上限が抑える）。**max_entries
// の実効の下限は 1** （0 を渡されても 1 として扱う。`RenderLimits::max_diagnostics` の注記）。
// 順序: 足された順で保持し、`sort()` で (location.offset, kind, message) の昇順に安定に整列する。
// 位置の無いものは末尾。同じ入力からは同じ並びになる（DESIGN.md §3-5）。
class Diagnostics {
 public:
  explicit Diagnostics(std::size_t max_entries) : max_entries_(max_entries) {}

  // 上限に達していれば捨てて truncated にする。戻り値は「記録したか」
  bool add_error(RenderError error);
  bool add_warning(Warning warning);

  void sort();

  [[nodiscard]] const std::vector<RenderError>& errors() const noexcept { return errors_; }
  [[nodiscard]] const std::vector<Warning>& warnings() const noexcept { return warnings_; }
  [[nodiscard]] bool truncated() const noexcept { return truncated_; }
  [[nodiscard]] std::size_t max_entries() const noexcept { return max_entries_; }
  [[nodiscard]] bool has_errors() const noexcept { return !errors_.empty(); }

  // 集めたものを RenderFailure に移す。`extra`（致命エラーなど、集めた列の外で見つかったもの）が
  // あれば合わせ、**結合したあとで sort() と同じ順序に整列する**（RenderFailure::errors の契約
  // 「入力位置の昇順」を保つ。致命エラーが先頭に来るとは限らない）。呼び出し後、この Diagnostics
  // は空。
  RenderFailure into_failure(std::vector<RenderError> extra = {}) &&;

 private:
  std::size_t max_entries_;
  std::vector<RenderError> errors_;
  std::vector<Warning> warnings_;
  bool truncated_ = false;
};

}  // namespace shashoku
