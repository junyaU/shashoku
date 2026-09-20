#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "shashoku/error.hpp"

namespace shashoku::html {

struct Attribute {
  std::string name;   // 小文字化済み
  std::string value;  // 文字参照は解決済み
  SourceLocation location;

  bool operator==(const Attribute&) const = default;
};

// ① の出力。意味と構造だけを持ち、座標もスタイルも持たない。
struct Node {
  enum class Type : std::uint8_t { Element, Text };

  Type type = Type::Element;
  std::string tag;               // Element のみ。小文字化済み。合成ルートは "#root"
  std::vector<Attribute> attrs;  // Element のみ。出現順
  // Text のみ。文字参照は解決済み。空白はソースのまま保持する
  // （空白の畳み込みは white-space の意味論なので ③ レイアウトの仕事）。
  // <style> 要素の中身も Text 子ノード 1 個として入る（CSS として解釈するのは ②）。
  std::string text;
  std::vector<Node> children;
  SourceLocation location;  // 開始タグ / テキストの先頭

  [[nodiscard]] const Attribute* find_attr(std::string_view name) const {
    for (const Attribute& attr : attrs) {
      if (attr.name == name) {
        return &attr;
      }
    }
    return nullptr;
  }

  bool operator==(const Node&) const = default;
};

}  // namespace shashoku::html
