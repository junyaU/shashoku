#include "core/diagnostics.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "shashoku/error.hpp"
#include "shashoku/source_location.hpp"
#include "shashoku/warning.hpp"

namespace shashoku {
namespace {

// 位置は offset だけで比べる（line / column は offset から導かれるので、同じ入力なら
// 同じ順になる）。位置の無いものは「末尾」なので、比較キーの先頭に「位置が無い」を置く。
std::tuple<bool, std::uint32_t> location_key(const std::optional<SourceLocation>& location) {
  return {!location.has_value(), location ? location->offset : 0U};
}

// (location.offset, kind, message) の昇順。位置なしは末尾（ARCHITECTURE.md A46）。
bool error_less(const RenderError& lhs, const RenderError& rhs) {
  const auto key = [](const RenderError& error) {
    return std::tuple{location_key(error.location), static_cast<std::uint8_t>(error.kind),
                      std::string_view{error.message}};
  };
  return key(lhs) < key(rhs);
}

// (location.offset, kind, codepoint, detail) の昇順。位置なしは末尾。
// 豆腐だけの列では ③ が決めた順序（位置 → コードポイント。A31）と一致する。
bool warning_less(const Warning& lhs, const Warning& rhs) {
  const auto key = [](const Warning& warning) {
    return std::tuple{location_key(warning.location), static_cast<std::uint8_t>(warning.kind),
                      static_cast<std::uint32_t>(warning.codepoint),
                      std::string_view{warning.detail}};
  };
  return key(lhs) < key(rhs);
}

// sort() と into_failure() が同じ規則で並べるための 1 か所。
void sort_errors(std::vector<RenderError>& errors) {
  std::stable_sort(errors.begin(), errors.end(), error_less);
}

void sort_warnings(std::vector<Warning>& warnings) {
  std::stable_sort(warnings.begin(), warnings.end(), warning_less);
}

}  // namespace

bool Diagnostics::add_error(RenderError error) {
  if (errors_.size() + warnings_.size() >= max_entries_) {
    truncated_ = true;
    return false;
  }
  errors_.push_back(std::move(error));
  return true;
}

bool Diagnostics::add_warning(Warning warning) {
  if (errors_.size() + warnings_.size() >= max_entries_) {
    truncated_ = true;
    return false;
  }
  warnings_.push_back(std::move(warning));
  return true;
}

// 安定整列にする（同じキーのものは足した順のまま）。同じ入力からは同じ並びになる
// （DESIGN.md §3-5）。
void Diagnostics::sort() {
  sort_errors(errors_);
  sort_warnings(warnings_);
}

RenderFailure Diagnostics::into_failure(std::vector<RenderError> extra) && {
  RenderFailure failure;
  // extra（致命エラーなど、集めた列の外で見つかったもの）を合わせてから、結合した列全体を
  // sort() と同じ規則で整列する。`RenderFailure::errors` は「入力位置の昇順」が契約なので、
  // 致命エラーが先頭に来るとは限らない。extra を先に置いてあるので、同じキーのものは
  // extra が先になる（安定整列）。
  failure.errors = std::move(extra);
  failure.errors.insert(failure.errors.end(), std::make_move_iterator(errors_.begin()),
                        std::make_move_iterator(errors_.end()));
  errors_.clear();
  failure.warnings = std::move(warnings_);
  warnings_.clear();
  sort_errors(failure.errors);
  sort_warnings(failure.warnings);
  failure.truncated = truncated_;
  return failure;
}

}  // namespace shashoku
