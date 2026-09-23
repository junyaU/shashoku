#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/diagnostics.hpp"
#include "core/result.hpp"
#include "html/dom.hpp"
#include "shashoku/error.hpp"
#include "shashoku/limits.hpp"
#include "style/computed_style.hpp"
#include "style/resolver.hpp"

// html パーサは別モジュールなので、テストでは DOM を手で組み立てる。
// dom.hpp はヘッダだけで完結しているので、ここはリンク依存を増やさない。
//
// 子ノードは可変長引数で受けて move する。`std::initializer_list<html::Node>` は必ず
// 要素をコピーし、html::Node のコピーは自分自身を含む（再帰的なデータ構造）ため、
// コピーを使うと clang-tidy の misc-no-recursion に引っかかる。

namespace shashoku::style {

// A46: resolve() は非致命の問題（CssParse / UnsupportedProperty / UnsupportedValue /
// UnsupportedLayout）を Diagnostics に集めて最後まで解決し、致命（LimitExceeded /
// Internal）だけを unexpected で返す。診断まで見るテストはこれを使う。
struct Resolved {
  Result<StyledNode> tree;          // 致命エラーなら unexpected
  std::vector<RenderError> errors;  // 集めた診断（足した順）
  bool truncated = false;

  // 「最初の問題」。集めたものがあればその 1 件目、無ければ致命エラー。
  [[nodiscard]] const RenderError* first_error() const {
    if (!errors.empty()) {
      return &errors.front();
    }
    return tree.has_value() ? nullptr : &tree.error();
  }
};

inline Resolved resolve_collect(const html::Node& root,
                                std::size_t max_style_rules = kMaxStyleRules,
                                float max_length_px = kMaxLengthPx,
                                std::size_t max_diagnostics = RenderLimits{}.max_diagnostics) {
  Diagnostics diagnostics{max_diagnostics};
  Resolved out{.tree = resolve(root, diagnostics, max_style_rules, max_length_px),
               .errors = {},
               .truncated = false};
  out.errors = diagnostics.errors();
  out.truncated = diagnostics.truncated();
  return out;
}

// 集める前から「失敗するはず」を見ていたテスト向けの包み。集めた 1 件目（無ければ致命）を
// unexpected にして、`Result<StyledNode>` を返していた頃と同じ形で読めるようにする。
inline Result<StyledNode> resolve_for_test(const html::Node& root,
                                           std::size_t max_style_rules = kMaxStyleRules,
                                           float max_length_px = kMaxLengthPx) {
  Resolved resolved = resolve_collect(root, max_style_rules, max_length_px);
  if (const RenderError* error = resolved.first_error(); error != nullptr) {
    return std::unexpected(*error);
  }
  return std::move(resolved.tree);
}

inline html::Attribute test_attr(std::string_view name, std::string_view value,
                                 SourceLocation location = {}) {
  return html::Attribute{
      .name = std::string{name}, .value = std::string{value}, .location = location};
}

// 子のない要素。
inline html::Node test_element(std::string_view tag, std::vector<html::Attribute> attrs = {},
                               SourceLocation location = {}) {
  return html::Node{.type = html::Node::Type::Element,
                    .tag = std::string{tag},
                    .attrs = std::move(attrs),
                    .text = {},
                    .children = {},
                    .location = location};
}

inline html::Node test_text(std::string_view content, SourceLocation location = {}) {
  return html::Node{.type = html::Node::Type::Text,
                    .tag = {},
                    .attrs = {},
                    .text = std::string{content},
                    .children = {},
                    .location = location};
}

template <class... Nodes>
std::vector<html::Node> test_children(Nodes&&... nodes) {
  std::vector<html::Node> out;
  out.reserve(sizeof...(Nodes));
  (out.push_back(std::forward<Nodes>(nodes)), ...);
  return out;
}

// 子のある要素。
template <class... Nodes>
html::Node test_parent(std::string_view tag, std::vector<html::Attribute> attrs,
                       Nodes&&... children) {
  html::Node node = test_element(tag, std::move(attrs));
  node.children = test_children(std::forward<Nodes>(children)...);
  return node;
}

// 合成ルート。html::parse() が返すものと同じ形（ARCHITECTURE.md §3.6）。
template <class... Nodes>
html::Node test_root(Nodes&&... children) {
  return test_parent("#root", {}, std::forward<Nodes>(children)...);
}

// <style> の中身はテキスト子ノード 1 個として入る（dom.hpp のコメント）。
inline html::Node test_style_element(std::string_view css, SourceLocation location = {}) {
  return test_parent("style", {}, test_text(css, location));
}

// `<div style="...">` を解決して div の計算値を返す。
inline Result<ComputedStyle> inline_style(std::string_view declarations) {
  const html::Node tree = test_root(test_element("div", {test_attr("style", declarations)}));
  Result<StyledNode> styled = resolve_for_test(tree);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (styled->children.empty()) {
    return fail(ErrorKind::Internal, "the test element was dropped from the tree");
  }
  return styled->children.front().style;
}

// `<style>…</style><div>` を解決して div の計算値を返す。
inline Result<ComputedStyle> sheet_style(std::string_view css,
                                         std::vector<html::Attribute> attrs = {}) {
  const html::Node tree = test_root(test_style_element(css), test_element("div", std::move(attrs)));
  Result<StyledNode> styled = resolve_for_test(tree);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (styled->children.empty()) {
    return fail(ErrorKind::Internal, "the test element was dropped from the tree");
  }
  return styled->children.front().style;
}

// タグだけの要素を解決して計算値を返す（UA スタイルの検査用）。
inline Result<ComputedStyle> tag_style(std::string_view tag) {
  const html::Node tree = test_root(test_element(tag));
  Result<StyledNode> styled = resolve_for_test(tree);
  if (!styled) {
    return std::unexpected(styled.error());
  }
  if (styled->children.empty()) {
    return fail(ErrorKind::Internal, "the test element was dropped from the tree");
  }
  return styled->children.front().style;
}

}  // namespace shashoku::style
