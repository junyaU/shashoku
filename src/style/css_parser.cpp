#include "style/css_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/diagnostics.hpp"
#include "core/result.hpp"
#include "core/utf8.hpp"
#include "shashoku/error.hpp"
#include "style/css_chars.hpp"
#include "style/declaration.hpp"
#include "style/style_error.hpp"
#include "style/value_parser.hpp"

namespace shashoku::style {
namespace {

class Parser {
 public:
  Parser(std::string_view text, SourceLocation base, bool map_offsets, Diagnostics& diagnostics)
      : text_(text), base_(base), map_offsets_(map_offsets), diagnostics_(&diagnostics) {}

  Result<void> parse_rules(std::uint32_t& order, Stylesheet& out);
  Result<std::vector<Declaration>> parse_inline_block();

 private:
  [[nodiscard]] bool eof() const { return pos_ >= text_.size(); }
  [[nodiscard]] char at(std::size_t i) const { return i < text_.size() ? text_[i] : '\0'; }
  [[nodiscard]] char peek() const { return at(pos_); }

  // CSS 内のバイトオフセットを入力 HTML 上の位置に直す。
  [[nodiscard]] SourceLocation loc_at(std::size_t offset) const;
  [[nodiscard]] Error error_at(std::size_t offset, std::string message) const;
  [[nodiscard]] std::unexpected<Error> bad(std::size_t offset, std::string message) const;

  // 非致命なら diagnostics に足して空の Result（読み飛ばして続行してよい）、
  // 致命ならその error を unexpected で返す（呼び出し側がそのまま返して止める）。A46。
  [[nodiscard]] Result<void> note(Error error) const;

  Result<bool> skip_trivia();
  std::string_view read_ident();
  Result<std::vector<Selector>> parse_selector_list();
  Result<Selector> parse_compound_selector();
  Result<void> parse_declaration_block(bool braced, std::vector<Declaration>& out);
  Result<void> read_declaration_or_skip(std::vector<Declaration>& out);
  Result<void> parse_one_declaration(std::vector<Declaration>& out);
  bool skip_value_literal();
  void scan_value_end();
  void skip_rule();

  std::string_view text_;
  SourceLocation base_;
  bool map_offsets_ = true;
  Diagnostics* diagnostics_ = nullptr;
  std::size_t pos_ = 0;
};

SourceLocation Parser::loc_at(std::size_t offset) const {
  if (!map_offsets_) {
    return base_;
  }
  const SourceLocation rel = locate(text_, offset);
  SourceLocation out;
  out.offset = base_.offset + rel.offset;
  if (rel.line == 1) {
    out.line = base_.line;
    out.column = base_.column + rel.column - 1;
  } else {
    out.line = base_.line + rel.line - 1;
    out.column = rel.column;
  }
  return out;
}

Error Parser::error_at(std::size_t offset, std::string message) const {
  return error_with_hint(ErrorKind::CssParse, std::move(message), loc_at(offset), {});
}

std::unexpected<Error> Parser::bad(std::size_t offset, std::string message) const {
  return std::unexpected(error_at(offset, std::move(message)));
}

Result<void> Parser::note(Error error) const {
  if (!is_recoverable(error.kind)) {
    return std::unexpected(std::move(error));  // 致命。呼び出し側が止める
  }
  // 上限に達していれば捨てられる（truncated が立つ）。続行の判断は変わらない
  diagnostics_->add_error(std::move(error));
  return {};
}

Result<bool> Parser::skip_trivia() {
  bool skipped = false;
  while (!eof()) {
    if (is_css_space(peek())) {
      ++pos_;
      skipped = true;
      continue;
    }
    if (peek() != '/' || at(pos_ + 1) != '*') {
      break;
    }
    const std::size_t start = pos_;
    const std::size_t end = text_.find("*/", pos_ + 2);
    if (end == std::string_view::npos) {
      return bad(start, "unclosed comment `/*`");
    }
    pos_ = end + 2;
    skipped = true;
  }
  return skipped;
}

std::string_view Parser::read_ident() {
  const std::size_t start = pos_;
  while (!eof() && is_css_ident_char(peek())) {
    ++pos_;
  }
  return text_.substr(start, pos_ - start);
}

Result<Selector> Parser::parse_compound_selector() {
  const std::size_t start = pos_;
  Selector selector;
  bool any = false;
  if (peek() == '*') {
    ++pos_;
    any = true;  // 全称セレクタ。詳細度には数えない
  } else if (is_css_ident_start(peek())) {
    selector.tag = ascii_lower(read_ident());
    selector.specificity.tag_count = 1;
    any = true;
  }

  while (!eof()) {
    const char c = peek();
    if (c == ':') {
      return bad(pos_,
                 "pseudo-classes and pseudo-elements (`:hover`, `::before`) are not "
                 "supported in selectors");
    }
    if (c == '[') {
      return bad(pos_, "attribute selectors (`[href]`) are not supported in selectors");
    }
    if (c != '.' && c != '#') {
      break;
    }
    const std::size_t marker = pos_;
    ++pos_;
    const std::string_view name = read_ident();
    if (name.empty()) {
      return bad(marker, std::format("expected a name after `{}` in a selector", c));
    }
    if (c == '.') {
      selector.classes.emplace_back(name);
      ++selector.specificity.class_count;
    } else {
      if (!selector.id.empty()) {
        return bad(marker, "a selector cannot contain two `#id` parts");
      }
      selector.id = name;
      selector.specificity.id_count = 1;
    }
    any = true;
  }

  if (!any) {
    return bad(start, "expected a selector (`tag`, `.class`, `#id` or `*`)");
  }
  return selector;
}

Result<std::vector<Selector>> Parser::parse_selector_list() {
  std::vector<Selector> selectors;
  while (true) {
    if (Result<bool> trivia = skip_trivia(); !trivia) {
      return std::unexpected(trivia.error());
    }
    if (eof()) {
      return bad(pos_, "expected `{` after a selector");
    }
    Result<Selector> selector = parse_compound_selector();
    if (!selector) {
      return std::unexpected(selector.error());
    }
    selectors.push_back(*std::move(selector));

    const Result<bool> space = skip_trivia();
    if (!space) {
      return std::unexpected(space.error());
    }
    if (eof()) {
      return bad(pos_, "expected `{` after a selector");
    }
    const char c = peek();
    if (c == '{') {
      return selectors;
    }
    if (c == ',') {
      ++pos_;
      continue;
    }
    if (c == '>' || c == '+' || c == '~') {
      return bad(pos_,
                 "selector combinators (`>`, `+`, `~`) are not supported; only `tag`, "
                 "`.class`, `#id`, their combinations and `,` are");
    }
    if (*space) {
      return bad(pos_,
                 "descendant combinators (a space between selectors) are not supported; "
                 "only `tag`, `.class`, `#id`, their combinations and `,` are");
    }
    return bad(pos_, std::format("unexpected `{}` in a selector", c));
  }
}

// 値の中のコメントか文字列を読み飛ばす。読み飛ばしたら true。
// 閉じていないものは、あとで値のトークナイザが CssParse にする。
bool Parser::skip_value_literal() {
  const char c = peek();
  if (c == '/' && at(pos_ + 1) == '*') {
    const std::size_t end = text_.find("*/", pos_ + 2);
    pos_ = end == std::string_view::npos ? text_.size() : end + 2;
    return true;
  }
  if (c != '"' && c != '\'') {
    return false;
  }
  ++pos_;
  while (!eof() && peek() != c && peek() != '\n') {
    ++pos_;
  }
  if (!eof() && peek() == c) {
    ++pos_;
  }
  return true;
}

// 値の終わり（`;` か `}` か入力の終わり）まで読み進める。
// 文字列・コメント・`(` の中の `;` `}` は終わりに数えない。
void Parser::scan_value_end() {
  int depth = 0;
  while (!eof()) {
    if (skip_value_literal()) {
      continue;
    }
    const char c = peek();
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      depth = depth > 0 ? depth - 1 : 0;
    } else if (depth == 0 && (c == ';' || c == '}')) {
      return;
    }
    ++pos_;
  }
}

Result<void> Parser::parse_one_declaration(std::vector<Declaration>& out) {
  const std::size_t name_start = pos_;
  const std::string_view raw_name = read_ident();
  if (raw_name.empty()) {
    return bad(name_start, std::format("expected a property name, found `{}`", peek()));
  }
  const std::string name = ascii_lower(raw_name);

  if (Result<bool> trivia = skip_trivia(); !trivia) {
    return std::unexpected(trivia.error());
  }
  if (peek() != ':') {
    return bad(pos_, std::format("expected `:` after the property name `{}`", name));
  }
  ++pos_;
  if (Result<bool> trivia = skip_trivia(); !trivia) {
    return std::unexpected(trivia.error());
  }

  const std::size_t value_start = pos_;
  scan_value_end();
  const std::string_view raw_value = text_.substr(value_start, pos_ - value_start);
  return parse_declaration(name, raw_value, loc_at(name_start), loc_at(value_start), out);
}

// 宣言 1 つ。壊れていたら捨てて次の区切りまで進む（A46「一度に全部」）。
Result<void> Parser::read_declaration_or_skip(std::vector<Declaration>& out) {
  const std::size_t written = out.size();
  if (Result<void> declaration = parse_one_declaration(out); !declaration) {
    if (Result<void> noted = note(declaration.error()); !noted) {
      return noted;
    }
    // 途中まで展開された longhand を残さない（捨てた宣言は「書かれなかった」扱い）
    out.resize(written);
    scan_value_end();  // 次の `;` / `}` / 入力の終わりまで捨てる
  }
  if (peek() == ';') {
    ++pos_;
  }
  return {};
}

// 宣言ブロック。壊れた宣言は捨てて次の宣言から読み直す（A46「一度に全部」）。
Result<void> Parser::parse_declaration_block(bool braced, std::vector<Declaration>& out) {
  const std::size_t open = pos_;
  if (braced) {
    ++pos_;  // `{`
  }
  while (true) {
    Result<bool> trivia = skip_trivia();
    if (!trivia) {
      // 未終了のコメント。ここから先はどこまでがコメントか決められないので、
      // 記録だけして残りを捨てる（読み飛ばし先が決められないので回復しない）。
      pos_ = text_.size();
      return note(trivia.error());
    }
    if (eof()) {
      if (!braced) {
        return {};
      }
      return note(error_at(open, "unclosed `{`: the declaration block is never closed"));
    }
    if (peek() == '}') {
      const std::size_t brace = pos_++;
      if (braced) {
        return {};
      }
      // `style` 属性に `}`。宣言の区切りとして捨てて続きを読む
      if (Result<void> noted = note(error_at(brace, "unexpected `}` in a `style` attribute"));
          !noted) {
        return noted;
      }
      continue;
    }
    if (peek() == ';') {
      ++pos_;  // 空の宣言は許す
      continue;
    }
    if (Result<void> declaration = read_declaration_or_skip(out); !declaration) {
      return declaration;
    }
  }
}

// 規則 1 つ分を読み飛ばす（セレクタが読めなかったとき）。`{` が来たら対応する `}` まで、
// `;` が来たらそこまで（`@import …;` のような波括弧の無い規則）。
void Parser::skip_rule() {
  int depth = 0;
  while (!eof()) {
    if (skip_value_literal()) {
      continue;
    }
    const char c = peek();
    ++pos_;
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      if (depth <= 1) {
        return;  // 対応する `}`（または対応しない `}`）まで来たら終わり
      }
      --depth;
    } else if (c == ';' && depth == 0) {
      return;
    }
  }
}

Result<void> Parser::parse_rules(std::uint32_t& order, Stylesheet& out) {
  while (true) {
    Result<bool> trivia = skip_trivia();
    if (!trivia) {
      return note(trivia.error());  // 未終了のコメント。残りは全部コメントなので終わる
    }
    if (eof()) {
      return {};
    }
    if (peek() == '@') {
      if (Result<void> noted = note(
              error_at(pos_, "at-rules (`@media`, `@import`, `@font-face`, …) are not supported"));
          !noted) {
        return noted;
      }
      skip_rule();
      continue;
    }
    if (peek() == '}') {
      if (Result<void> noted =
              note(error_at(pos_, "unexpected `}` outside of a declaration block"));
          !noted) {
        return noted;
      }
      ++pos_;  // 余分な `}` を 1 つ捨てて、次の規則から読み直す
      continue;
    }

    Result<std::vector<Selector>> selectors = parse_selector_list();
    if (!selectors) {
      // セレクタが読めなければ、その規則の宣言はどの要素に当たるか決められない。
      // 規則の単位で読み飛ばす（A46）
      if (Result<void> noted = note(selectors.error()); !noted) {
        return noted;
      }
      skip_rule();
      continue;
    }
    Rule rule;
    rule.selectors = *std::move(selectors);
    rule.order = order++;
    if (Result<void> block = parse_declaration_block(true, rule.declarations); !block) {
      return block;
    }
    out.push_back(std::move(rule));
  }
}

Result<std::vector<Declaration>> Parser::parse_inline_block() {
  std::vector<Declaration> out;
  if (Result<void> block = parse_declaration_block(false, out); !block) {
    return std::unexpected(block.error());
  }
  return out;
}

}  // namespace

Result<void> parse_stylesheet(std::string_view css, SourceLocation base, std::uint32_t& order,
                              Stylesheet& out, Diagnostics& diagnostics) {
  Parser parser(css, base, true, diagnostics);
  return parser.parse_rules(order, out);
}

Result<std::vector<Declaration>> parse_inline_style(std::string_view css, SourceLocation attribute,
                                                    Diagnostics& diagnostics) {
  Parser parser(css, attribute, false, diagnostics);
  return parser.parse_inline_block();
}

}  // namespace shashoku::style
