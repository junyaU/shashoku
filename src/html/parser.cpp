#include "html/parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/diagnostics.hpp"
#include "core/json_writer.hpp"
#include "core/result.hpp"
#include "core/utf8.hpp"
#include "html/dom.hpp"
#include "html/tags.hpp"
#include "shashoku/error.hpp"

namespace shashoku::html {
namespace {

constexpr std::uint32_t kMaxCodePoint = 0x10FFFF;
constexpr std::uint32_t kSurrogateFirst = 0xD800;
constexpr std::uint32_t kSurrogateLast = 0xDFFF;

// 文字参照の名前をどこまで読んで諦めるか（`&` の取り違えで延々読まないため）。
constexpr std::size_t kMaxReferenceNameLength = 32;

constexpr std::string_view kRootTag = "#root";
// 対応済みの生テキスト要素（中身は ② が読む CSS なので、文字参照を解決せずそのまま残す）。
constexpr std::string_view kStyleTag = "style";

// 対応属性（ARCHITECTURE.md §3.6）。エラーメッセージに並べるので辞書順に持つ。
// 対応タグ（`kSupportedTags` / `is_supported_tag()`）は ② style も引くので `html/tags.hpp`。
constexpr std::array<std::string_view, 2> kVoidTags{"br", "img"};
// HTML の空要素のうち shashoku が対応していないもの（A49）。対応外の要素は透過にするが、
// 空要素には終了タグが無いので、開いている要素のスタックに積むと直後の `</head>` が
// 入れ子の誤りになってしまう。辞書順（contains は線形探索なので順序は速度に効かない）。
constexpr std::array<std::string_view, 12> kUnsupportedVoidTags{"area",  "base",   "col",   "embed",
                                                                "hr",    "input",  "link",  "meta",
                                                                "param", "source", "track", "wbr"};
// 対応外の生テキスト要素（A49）。対応する終了タグまでを生テキストとして読み飛ばす
// （中の `<` と `&` を解釈しない）。WHATWG の raw text / escapable raw text のうち
// 対応外のもので、`<style>` だけは対応済みなので別扱い。辞書順。
constexpr std::array<std::string_view, 7> kUnsupportedRawTextTags{
    "iframe", "noembed", "noframes", "script", "textarea", "title", "xmp"};
constexpr std::array<std::string_view, 3> kCommonAttributes{"class", "id", "style"};
constexpr std::array<std::string_view, 7> kImgAttributes{"alt", "class", "height", "id",
                                                         "src", "style", "width"};

struct NamedReference {
  std::string_view name;
  char32_t code_point;
};

constexpr std::array<NamedReference, 6> kNamedReferences{{{"amp", U'&'},
                                                          {"apos", U'\''},
                                                          {"gt", U'>'},
                                                          {"lt", U'<'},
                                                          {"nbsp", U'\u00A0'},
                                                          {"quot", U'"'}}};

bool is_ascii_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

bool is_ascii_alnum(char c) { return is_ascii_alpha(c) || is_ascii_digit(c); }

// タグ名・属性名に使える文字。対応外の名前（`<my-widget>` など）も読み切ってから
// エラーにしたいので、HTML で実際に使われる範囲を広めに取る。
bool is_name_char(char c) { return is_ascii_alnum(c) || c == '-' || c == '_'; }

char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool contains(std::span<const std::string_view> items, std::string_view value) {
  return std::find(items.begin(), items.end(), value) != items.end();
}

bool is_void_tag(std::string_view tag) { return contains(kVoidTags, tag); }

bool is_unsupported_void_tag(std::string_view tag) { return contains(kUnsupportedVoidTags, tag); }

bool is_unsupported_raw_text_tag(std::string_view tag) {
  return contains(kUnsupportedRawTextTags, tag);
}

std::span<const std::string_view> allowed_attributes(std::string_view tag) {
  if (tag == "img") {
    return kImgAttributes;
  }
  return kCommonAttributes;
}

std::string join(std::span<const std::string_view> items) {
  std::string out;
  for (const std::string_view item : items) {
    if (!out.empty()) {
      out += ", ";
    }
    out += item;
  }
  return out;
}

std::string supported_reference_names() {
  std::string out;
  for (const NamedReference& ref : kNamedReferences) {
    if (!out.empty()) {
      out += ' ';
    }
    out += std::format("&{};", ref.name);
  }
  return out;
}

const NamedReference* find_named_reference(std::string_view name) {
  for (const NamedReference& ref : kNamedReferences) {
    if (ref.name == name) {
      return &ref;
    }
  }
  return nullptr;
}

// 検証済み UTF-8 の先頭バイトから系列長を求める。
std::size_t sequence_length(unsigned char lead) {
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  if ((lead & 0xF8) == 0xF0) {
    return 4;
  }
  return 1;
}

// 10 進 / 16 進の 1 桁。桁でなければ -1。
int digit_value(char c, bool hex) {
  if (is_ascii_digit(c)) {
    return c - '0';
  }
  if (!hex) {
    return -1;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Node は素の struct なので、全フィールドを並べる指示付き初期化ではなく小さな組み立て関数を使う
// （指示付き初期化で途中のフィールドを飛ばすと -Wmissing-field-initializers に掛かる）。
Node make_element(std::string tag, std::vector<Attribute> attrs, const SourceLocation& location) {
  Node node;
  node.type = Node::Type::Element;
  node.tag = std::move(tag);
  node.attrs = std::move(attrs);
  node.location = location;
  return node;
}

Node make_text(std::string value, const SourceLocation& location) {
  Node node;
  node.type = Node::Type::Text;
  node.text = std::move(value);
  node.location = location;
  return node;
}

// 集めて続行する診断（A46）。fail() と同じ形の RenderError を作る。
Error make_error(ErrorKind kind, std::string message, const SourceLocation& location) {
  return Error{.kind = kind,
               .message = std::move(message),
               .location = location,
               .hint = {},
               .warning = std::nullopt};
}

// 開いている要素 1 つ。対応外のタグは**透過**（transparent）として積む: 終了タグの対応は
// 取るが、木には残さず、子は親の子になる（A46 / A49）。
struct OpenElement {
  Node node;
  bool transparent = false;
};

// 開始タグの `>` までを読んだ結果。
struct StartTagTail {
  std::vector<Attribute> attrs;
  bool self_closing = false;
};

// 明示スタックで構文解析する（再帰しない）。入力は UTF-8 として検証済みであることを前提に、
// ASCII の区切り文字だけを見て進む（多バイト列のバイトはすべて 0x80 以上なので衝突しない）。
class Parser {
 public:
  Parser(std::string_view source, Diagnostics& diagnostics, std::size_t max_nesting_depth)
      : src_(source), diagnostics_(&diagnostics), max_nesting_depth_(max_nesting_depth) {}

  Result<Node> run();

 private:
  // ---- 位置と先読み -------------------------------------------------------
  [[nodiscard]] bool eof() const { return pos_ >= src_.size(); }

  // 入力に NUL は無いので、終端を '\0' で表しても本物の文字と紛れない。
  [[nodiscard]] char peek(std::size_t ahead = 0) const {
    return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
  }

  [[nodiscard]] SourceLocation here() const {
    return SourceLocation{
        .offset = static_cast<std::uint32_t>(pos_), .line = line_, .column = column_};
  }

  void advance();
  void advance_to(std::size_t target);
  std::size_t skip_whitespace();
  [[nodiscard]] std::size_t skip_whitespace_from(std::size_t index) const;
  [[nodiscard]] bool starts_with_ci(std::size_t index, std::string_view ascii) const;
  [[nodiscard]] std::string describe_char_at(std::size_t index) const;
  [[nodiscard]] std::string_view span_from(const SourceLocation& start) const;

  // ---- 字句 ---------------------------------------------------------------
  std::string read_name();
  Result<void> consume_reference(std::string& out);
  Result<void> consume_named_reference(std::string& out, const SourceLocation& start);
  Result<void> consume_numeric_reference(std::string& out, const SourceLocation& start);

  // ---- 木構築 -------------------------------------------------------------
  Result<void> check_input();
  void begin_text();
  void flush_text();
  Result<void> parse_markup();
  Result<void> skip_comment();
  Result<void> skip_doctype();
  Result<void> parse_start_tag();
  Result<void> open_element(Node element, const SourceLocation& start, bool transparent);
  void close_top();
  [[nodiscard]] bool has_open_element(std::string_view tag) const;
  Result<StartTagTail> parse_attributes(std::string_view tag, const SourceLocation& start,
                                        bool keep_attributes);
  Result<void> parse_attribute(std::string_view tag, std::vector<Attribute>& attrs,
                               bool keep_attributes);
  Result<std::string> read_attribute_value(std::string_view tag, std::string_view name, bool keep);
  Result<std::string> read_quoted_value(std::string_view name, bool keep);
  Result<std::string> read_unquoted_value(std::string_view name, bool keep);
  Result<void> parse_end_tag();
  Result<std::string_view> read_raw_text(std::string_view tag, const SourceLocation& start);
  [[nodiscard]] bool matches_raw_text_end(std::size_t index, std::string_view tag) const;

  std::string_view src_;
  // 参照メンバにすると cppcoreguidelines-avoid-const-or-ref-data-members に掛かるので生ポインタ。
  // 構築時に必ず非 null で、Parser は呼び出しの間だけ生きる。
  Diagnostics* diagnostics_ = nullptr;
  std::size_t max_nesting_depth_ = kMaxNestingDepth;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t column_ = 1;
  bool after_cr_ = false;  // 直前のバイトが CR（CRLF を 1 行として数えるため）

  std::string text_;  // 未確定のテキスト（文字参照は解決済み）
  std::optional<SourceLocation> text_start_;  // テキスト実行の先頭
  std::vector<OpenElement> open_;             // open_[0] は合成ルート
};

void Parser::advance() {
  const auto b = static_cast<unsigned char>(src_[pos_]);
  if (b == '\n') {
    // CRLF は 1 つの改行（core/utf8.cpp の locate() と同じ数え方）
    if (!after_cr_) {
      ++line_;
      column_ = 1;
    }
    after_cr_ = false;
  } else if (b == '\r') {
    ++line_;
    column_ = 1;
    after_cr_ = true;
  } else {
    after_cr_ = false;
    if ((b & 0xC0) != 0x80) {  // 継続バイトは桁に数えない（桁はコードポイント単位）
      ++column_;
    }
  }
  ++pos_;
}

void Parser::advance_to(std::size_t target) {
  const std::size_t limit = std::min(target, src_.size());
  while (pos_ < limit) {
    advance();
  }
}

std::size_t Parser::skip_whitespace() {
  const std::size_t start = pos_;
  while (!eof() && is_ascii_space(peek())) {
    advance();
  }
  return pos_ - start;
}

std::size_t Parser::skip_whitespace_from(std::size_t index) const {
  std::size_t i = index;
  while (i < src_.size() && is_ascii_space(src_[i])) {
    ++i;
  }
  return i;
}

bool Parser::starts_with_ci(std::size_t index, std::string_view ascii) const {
  if (index + ascii.size() > src_.size()) {
    return false;
  }
  for (std::size_t k = 0; k < ascii.size(); ++k) {
    if (to_lower(src_[index + k]) != ascii[k]) {
      return false;
    }
  }
  return true;
}

std::string Parser::describe_char_at(std::size_t index) const {
  if (index >= src_.size()) {
    return "the end of the input";
  }
  const auto lead = static_cast<unsigned char>(src_[index]);
  if (lead >= 0x21 && lead <= 0x7E) {
    return std::format("`{}`", src_[index]);
  }
  // 非 ASCII・制御文字・空白はメッセージに生のまま入れない（U+XXXX で示す）
  const std::size_t length = std::min(sequence_length(lead), src_.size() - index);
  const Result<std::u32string> decoded = decode_utf8(src_.substr(index, length));
  const char32_t cp = (decoded && !decoded->empty()) ? decoded->front() : U'�';
  return std::format("U+{:04X}", static_cast<std::uint32_t>(cp));
}

// start から現在位置までの生の入力（エラーメッセージで「何を読んだか」を示す）。
std::string_view Parser::span_from(const SourceLocation& start) const {
  return src_.substr(start.offset, pos_ - start.offset);
}

std::string Parser::read_name() {
  std::string name;
  while (!eof() && is_name_char(peek())) {
    name.push_back(to_lower(peek()));
    advance();
  }
  return name;
}

Result<void> Parser::consume_reference(std::string& out) {
  const SourceLocation start = here();
  advance();  // `&`
  // `A & B` のように直後が空白（または入力末尾）のときだけ素の `&` として許す。
  if (eof() || is_ascii_space(peek())) {
    out.push_back('&');
    return {};
  }
  if (peek() == '#') {
    return consume_numeric_reference(out, start);
  }
  return consume_named_reference(out, start);
}

Result<void> Parser::consume_named_reference(std::string& out, const SourceLocation& start) {
  std::string name;
  while (!eof() && is_ascii_alnum(peek()) && name.size() < kMaxReferenceNameLength) {
    name.push_back(peek());
    advance();
  }
  if (name.empty()) {
    return fail(ErrorKind::HtmlParse,
                std::format("`&` must be followed by a character reference name or `#`, got {}; "
                            "write `&amp;` for a literal `&`",
                            describe_char_at(pos_)),
                start);
  }
  if (eof() || peek() != ';') {
    return fail(
        ErrorKind::HtmlParse,
        std::format("character reference `{}` is missing the closing `;`", span_from(start)),
        start);
  }
  advance();  // `;`
  const NamedReference* ref = find_named_reference(name);
  if (ref == nullptr) {
    return fail(ErrorKind::HtmlParse,
                std::format("unknown character reference `{}` (supported: {})", span_from(start),
                            supported_reference_names()),
                start);
  }
  append_utf8(out, ref->code_point);
  return {};
}

Result<void> Parser::consume_numeric_reference(std::string& out, const SourceLocation& start) {
  advance();  // `#`
  bool hex = false;
  if (!eof() && (peek() == 'x' || peek() == 'X')) {
    hex = true;
    advance();
  }

  const std::uint32_t base = hex ? 16U : 10U;
  std::uint32_t value = 0;
  std::size_t digits = 0;
  bool overflow = false;
  while (!eof()) {
    const int digit = digit_value(peek(), hex);
    if (digit < 0) {
      break;
    }
    ++digits;
    if (!overflow) {
      value = (value * base) + static_cast<std::uint32_t>(digit);
      overflow = value > kMaxCodePoint;
    }
    advance();
  }

  if (digits == 0) {
    return fail(ErrorKind::HtmlParse,
                std::format("`{}` must be followed by one or more {} digits", span_from(start),
                            hex ? "hexadecimal" : "decimal"),
                start);
  }
  if (eof() || peek() != ';') {
    return fail(
        ErrorKind::HtmlParse,
        std::format("character reference `{}` is missing the closing `;`", span_from(start)),
        start);
  }
  advance();  // `;`

  if (overflow) {
    return fail(ErrorKind::HtmlParse,
                std::format("character reference `{}` is above the maximum code point U+10FFFF",
                            span_from(start)),
                start);
  }
  if (value >= kSurrogateFirst && value <= kSurrogateLast) {
    return fail(ErrorKind::HtmlParse,
                std::format("character reference `{}` is the surrogate code point U+{:04X}, which "
                            "is not a character",
                            span_from(start), value),
                start);
  }
  if (value == 0) {
    return fail(
        ErrorKind::HtmlParse,
        std::format("character reference `{}` is U+0000, which is not allowed", span_from(start)),
        start);
  }
  append_utf8(out, static_cast<char32_t>(value));
  return {};
}

Result<void> Parser::check_input() {
  // 入力全体をまず UTF-8 として検証する（位置つきの InvalidUtf8 をそのまま返す）。
  Result<std::u32string> decoded = decode_utf8(src_);
  if (!decoded) {
    return std::unexpected(std::move(decoded).error());
  }
  const std::size_t nul = src_.find('\0');
  if (nul != std::string_view::npos) {
    return fail(ErrorKind::HtmlParse, "NUL character (U+0000) is not allowed in the HTML input",
                locate(src_, nul));
  }
  return {};
}

void Parser::begin_text() {
  if (!text_start_) {
    text_start_ = here();
  }
}

void Parser::flush_text() {
  if (!text_start_) {
    return;
  }
  Node node = make_text(std::move(text_), *text_start_);
  text_.clear();
  text_start_.reset();
  open_.back().node.children.push_back(std::move(node));
}

Result<void> Parser::parse_markup() {
  const char next = peek(1);
  if (next == '!') {
    if (peek(2) == '-' && peek(3) == '-') {
      return skip_comment();
    }
    return skip_doctype();
  }
  if (next == '/') {
    return parse_end_tag();
  }
  if (is_ascii_alpha(next)) {
    return parse_start_tag();
  }
  return fail(ErrorKind::HtmlParse,
              std::format("`<` must start a tag, a comment or a DOCTYPE, but it is followed by {}; "
                          "write `&lt;` for a literal `<`",
                          describe_char_at(pos_ + 1)),
              here());
}

Result<void> Parser::skip_comment() {
  const SourceLocation start = here();
  const std::size_t end = src_.find("-->", pos_ + 4);
  if (end == std::string_view::npos) {
    return fail(ErrorKind::HtmlParse,
                "unterminated comment: expected `-->` before the end of the input", start);
  }
  // テキストは確定させない: コメントを挟んで隣り合うテキストは 1 ノードに連結する
  advance_to(end + 3);
  return {};
}

Result<void> Parser::skip_doctype() {
  const SourceLocation start = here();
  if (!starts_with_ci(pos_, "<!doctype")) {
    return fail(ErrorKind::HtmlParse,
                "`<!` must start a comment (`<!-- ... -->`) or a DOCTYPE (`<!DOCTYPE html>`)",
                start);
  }
  const std::size_t end = src_.find('>', pos_);
  if (end == std::string_view::npos) {
    return fail(ErrorKind::HtmlParse,
                "unterminated DOCTYPE: expected `>` before the end of the input", start);
  }
  advance_to(end + 1);
  return {};
}

Result<void> Parser::parse_start_tag() {
  const SourceLocation start = here();
  advance();  // `<`
  const std::string tag = read_name();
  const bool supported = is_supported_tag(tag);
  if (!supported) {
    // A46: 集めて続行する。要素は透過にして、中身の問題も同じ 1 回で報告する
    diagnostics_->add_error(make_error(
        ErrorKind::UnsupportedTag,
        std::format("`<{}>` is not supported (supported tags: {})", tag, join(kSupportedTags)),
        start));
  }

  // 透過する要素の属性は報告しない（A49）: 「`<tag>` が対応外」の 1 件で足りるので、
  // `<meta charset="utf-8">` のように「class / id / style なら使える」と読める報告を並べない。
  Result<StartTagTail> tail = parse_attributes(tag, start, /*keep_attributes=*/supported);
  if (!tail) {
    return std::unexpected(std::move(tail).error());
  }
  if (supported && tail->self_closing && !is_void_tag(tag)) {
    return fail(ErrorKind::HtmlParse,
                std::format("`<{}/>` is not allowed: only {} are void elements; close `<{}>` with "
                            "`</{}>`",
                            tag, join(kVoidTags), tag, tag),
                start);
  }

  flush_text();
  if (!supported) {
    // 透過（A46）: 開始タグを無いものとして読む。空要素（`<meta>` など）と `/>` はその場で
    // 終わり、生テキスト要素（`<script>` など）は中身ごと読み飛ばし、それ以外は終了タグの
    // 対応を取るためにスタックへ積む（木には残さない）。
    if (is_unsupported_void_tag(tag) || tail->self_closing) {
      return {};
    }
    if (is_unsupported_raw_text_tag(tag)) {
      // 中身は生テキストとして捨てる（子は作らない）。`UnsupportedTag` は上で 1 件出している
      Result<std::string_view> raw = read_raw_text(tag, start);
      if (!raw) {
        return std::unexpected(std::move(raw).error());
      }
      return {};
    }
    return open_element(make_element(tag, {}, start), start, /*transparent=*/true);
  }

  if (tag == kStyleTag) {
    const SourceLocation text_start = here();
    Result<std::string_view> raw = read_raw_text(tag, start);
    if (!raw) {
      return std::unexpected(std::move(raw).error());
    }
    Node style = make_element(tag, std::move(tail->attrs), start);
    if (!raw->empty()) {
      style.children.push_back(make_text(std::string{*raw}, text_start));
    }
    open_.back().node.children.push_back(std::move(style));
    return {};
  }

  Node element = make_element(tag, std::move(tail->attrs), start);
  if (is_void_tag(tag)) {
    open_.back().node.children.push_back(std::move(element));
    return {};
  }
  return open_element(std::move(element), start, /*transparent=*/false);
}

Result<void> Parser::open_element(Node element, const SourceLocation& start, bool transparent) {
  // open_ は合成ルートを含むので、push 後の入れ子の深さは open_.size() になる。
  // 透過した要素も数える（木の深さより厳しくなるが、対応外のタグを並べただけの入力で
  // 解析器のメモリが伸びないようにするため。A49）。
  if (open_.size() > max_nesting_depth_) {
    return fail(ErrorKind::LimitExceeded,
                std::format("elements are nested too deeply (the maximum is {}): `<{}>`",
                            max_nesting_depth_, element.tag),
                start);
  }
  open_.push_back(OpenElement{.node = std::move(element), .transparent = transparent});
  return {};
}

// 開いている要素を 1 つ閉じる。透過した要素は木に残さず、その子を親の子として引き取る。
void Parser::close_top() {
  flush_text();
  OpenElement closed = std::move(open_.back());
  open_.pop_back();
  std::vector<Node>& siblings = open_.back().node.children;
  if (!closed.transparent) {
    siblings.push_back(std::move(closed.node));
    return;
  }
  for (Node& child : closed.node.children) {
    siblings.push_back(std::move(child));
  }
}

// 合成ルートを除いて、その名前の要素が開いているか（終了タグが「入れ子の誤り」か
// 「対応する開始タグが無い」かを分けるため）。
bool Parser::has_open_element(std::string_view tag) const {
  for (std::size_t i = 1; i < open_.size(); ++i) {
    if (open_[i].node.tag == tag) {
      return true;
    }
  }
  return false;
}

Result<StartTagTail> Parser::parse_attributes(std::string_view tag, const SourceLocation& start,
                                              bool keep_attributes) {
  StartTagTail tail;
  while (true) {
    const bool had_space = skip_whitespace() > 0;
    if (eof()) {
      return fail(ErrorKind::HtmlParse,
                  std::format("unterminated start tag `<{}`: expected `>` before the end of the "
                              "input",
                              tag),
                  start);
    }
    if (peek() == '>') {
      advance();
      return tail;
    }
    if (peek() == '/') {
      advance();
      if (peek() != '>') {
        return fail(ErrorKind::HtmlParse,
                    std::format("`/` in the start tag `<{}>` must be followed by `>`, got {}", tag,
                                describe_char_at(pos_)),
                    start);
      }
      advance();
      tail.self_closing = true;
      return tail;
    }
    if (!had_space) {
      return fail(ErrorKind::HtmlParse,
                  std::format("unexpected {} in the start tag `<{}>`: attributes must be separated "
                              "by whitespace",
                              describe_char_at(pos_), tag),
                  here());
    }
    Result<void> attribute = parse_attribute(tag, tail.attrs, keep_attributes);
    if (!attribute) {
      return std::unexpected(std::move(attribute).error());
    }
  }
}

// keep_attributes が false = 要素が透過。その要素の属性は名前も値も捨て、報告もしない
// （A49。重複の検査もしない: 捨てる属性が重なっても結果に影響しないので、
// そこで解析を止めると後ろの問題が 1 件も出なくなる）。
Result<void> Parser::parse_attribute(std::string_view tag, std::vector<Attribute>& attrs,
                                     bool keep_attributes) {
  const SourceLocation start = here();
  std::string name = read_name();
  if (name.empty()) {
    return fail(ErrorKind::HtmlParse,
                std::format("unexpected {} in the start tag `<{}>`: expected an attribute name, "
                            "`>` or `/>`",
                            describe_char_at(pos_), tag),
                start);
  }
  const bool keep = keep_attributes && contains(allowed_attributes(tag), name);
  if (!keep) {
    if (keep_attributes) {
      // A46: 集めて続行する。属性は捨てて要素は残す（値は読み切ってから捨てる。読まないと
      // `=` の右側を構文として読み違える）
      diagnostics_->add_error(
          make_error(ErrorKind::UnsupportedAttribute,
                     std::format("`{}` is not supported on `<{}>` (supported attributes: {})", name,
                                 tag, join(allowed_attributes(tag))),
                     start));
    }
  } else {
    for (const Attribute& existing : attrs) {
      if (existing.name == name) {
        return fail(ErrorKind::HtmlParse,
                    std::format("duplicate attribute `{}` on `<{}>` (first given at {}:{})", name,
                                tag, existing.location.line, existing.location.column),
                    start);
      }
    }
  }

  std::string value;
  // `=` の前後の空白は許す。`=` が無ければ値なし属性（空文字列）。
  const std::size_t equals = skip_whitespace_from(pos_);
  if (equals < src_.size() && src_[equals] == '=') {
    advance_to(equals + 1);
    skip_whitespace();
    Result<std::string> parsed = read_attribute_value(tag, name, keep);
    if (!parsed) {
      return std::unexpected(std::move(parsed).error());
    }
    value = std::move(*parsed);
  }
  if (!keep) {
    return {};
  }
  attrs.push_back(Attribute{.name = std::move(name), .value = std::move(value), .location = start});
  return {};
}

// keep が false の属性（透過した要素の全属性と、対応済みの要素の対応外の属性）は値を捨てるので、
// **中身は生のまま読み飛ばす**（文字参照を検証しない。A49）。`<link href="…&display=swap">` の
// ような URL で致命エラーにならないため。値の**終わり**の判定（引用符・空白・`>`・引用符なしの
// 値に書けない文字）は捨てる値でも同じ: そこは値の中身ではなく、タグをどこまで読むかの構文。
Result<std::string> Parser::read_attribute_value(std::string_view tag, std::string_view name,
                                                 bool keep) {
  if (!eof() && (peek() == '"' || peek() == '\'')) {
    return read_quoted_value(name, keep);
  }
  if (eof() || is_ascii_space(peek()) || peek() == '>') {
    return fail(ErrorKind::HtmlParse,
                std::format("missing value for attribute `{}` on `<{}>` after `=`", name, tag),
                here());
  }
  return read_unquoted_value(name, keep);
}

Result<std::string> Parser::read_quoted_value(std::string_view name, bool keep) {
  const SourceLocation start = here();
  const char quote = peek();
  advance();
  std::string value;
  while (!eof() && peek() != quote) {
    if (keep && peek() == '&') {
      Result<void> reference = consume_reference(value);
      if (!reference) {
        return std::unexpected(std::move(reference).error());
      }
      continue;
    }
    value.push_back(peek());
    advance();
  }
  if (eof()) {
    return fail(ErrorKind::HtmlParse,
                std::format("unterminated value for attribute `{}`: expected a closing `{}` before "
                            "the end of the input",
                            name, quote),
                start);
  }
  advance();  // 閉じ引用符
  return value;
}

Result<std::string> Parser::read_unquoted_value(std::string_view name, bool keep) {
  std::string value;
  while (!eof() && !is_ascii_space(peek()) && peek() != '>') {
    const char c = peek();
    // `/` を含めると `<img src=a/>` の解釈が曖昧になる。黙って決めずにエラーにする。
    if (c == '"' || c == '\'' || c == '=' || c == '<' || c == '`' || c == '/') {
      return fail(ErrorKind::HtmlParse,
                  std::format("`{}` is not allowed in an unquoted attribute value; quote the value "
                              "of `{}`",
                              c, name),
                  here());
    }
    if (keep && c == '&') {
      Result<void> reference = consume_reference(value);
      if (!reference) {
        return std::unexpected(std::move(reference).error());
      }
      continue;
    }
    value.push_back(c);
    advance();
  }
  return value;
}

Result<void> Parser::parse_end_tag() {
  const SourceLocation start = here();
  advance();  // `<`
  advance();  // `/`
  const std::string tag = read_name();
  if (tag.empty()) {
    return fail(ErrorKind::HtmlParse,
                std::format("`</` must be followed by a tag name, got {}", describe_char_at(pos_)),
                start);
  }
  const bool supported = is_supported_tag(tag);
  if (supported && is_void_tag(tag)) {
    return fail(
        ErrorKind::HtmlParse,
        std::format("`</{}>` is not allowed: `{}` is a void element and has no end tag", tag, tag),
        start);
  }
  skip_whitespace();
  if (eof()) {
    return fail(
        ErrorKind::HtmlParse,
        std::format("unterminated end tag `</{}`: expected `>` before the end of the input", tag),
        start);
  }
  if (peek() != '>') {
    return fail(ErrorKind::HtmlParse,
                std::format("unexpected {} in the end tag `</{}>`: expected `>`",
                            describe_char_at(pos_), tag),
                here());
  }
  advance();

  const auto mismatch = [&]() {
    const SourceLocation opened = open_.back().node.location;
    return fail(ErrorKind::HtmlParse,
                std::format("`</{}>` does not match the open element `<{}>` (opened at {}:{})", tag,
                            open_.back().node.tag, opened.line, opened.column),
                start);
  };

  if (!supported) {
    // 透過した要素の終了（A46）。開始タグで報告済みなので、ここでは何も足さずに消費する
    if (open_.size() > 1 && open_.back().transparent && open_.back().node.tag == tag) {
      close_top();
      return {};
    }
    // 対応する開始タグはあるのに合わない = 入れ子が壊れている。安全に読み続けられない
    if (has_open_element(tag)) {
      return mismatch();
    }
    // 対応する開始タグの無い終了タグ（`</table>` 単独）。対応外なので集めて読み飛ばす
    diagnostics_->add_error(make_error(
        ErrorKind::UnsupportedTag,
        std::format("`</{}>` is not supported (supported tags: {})", tag, join(kSupportedTags)),
        start));
    return {};
  }

  if (open_.size() <= 1) {
    return fail(ErrorKind::HtmlParse,
                std::format("unexpected end tag `</{}>`: no element is open", tag), start);
  }
  if (open_.back().node.tag != tag) {
    return mismatch();
  }
  close_top();
  return {};
}

bool Parser::matches_raw_text_end(std::size_t index, std::string_view tag) const {
  if (!starts_with_ci(index, "</") || !starts_with_ci(index + 2, tag)) {
    return false;
  }
  // `</styles>` のような別の名前を終了タグと取り違えない
  const std::size_t after = index + 2 + tag.size();
  return after < src_.size() && (is_ascii_space(src_[after]) || src_[after] == '>');
}

// 生テキスト要素の中身を、対応する終了タグまで読む（中の `<` も `&` も解釈しない）。
// 終了タグ（`</tag>`）まで消費して中身を返す。終了タグが無ければ HtmlParse（致命）。
// 対応済みの `<style>` と、対応外の `<script>` `<textarea>` などで共通（A49）。
Result<std::string_view> Parser::read_raw_text(std::string_view tag, const SourceLocation& start) {
  std::size_t end = std::string_view::npos;
  for (std::size_t i = pos_; i < src_.size(); ++i) {
    if (matches_raw_text_end(i, tag)) {
      end = i;
      break;
    }
  }
  if (end == std::string_view::npos) {
    return fail(ErrorKind::HtmlParse,
                std::format("unterminated `<{}>` element: expected `</{}>` before the end of the "
                            "input",
                            tag, tag),
                start);
  }

  const std::string_view raw = src_.substr(pos_, end - pos_);
  advance_to(end + 2 + tag.size());
  skip_whitespace();
  if (eof() || peek() != '>') {
    return fail(ErrorKind::HtmlParse,
                std::format("unexpected {} in the end tag `</{}>`: expected `>`",
                            describe_char_at(pos_), tag),
                here());
  }
  advance();
  return raw;
}

Result<Node> Parser::run() {
  if (Result<void> checked = check_input(); !checked) {
    return std::unexpected(std::move(checked).error());
  }
  open_.push_back(OpenElement{.node = make_element(std::string{kRootTag}, {}, SourceLocation{}),
                              .transparent = false});

  while (!eof()) {
    if (peek() == '<') {
      Result<void> markup = parse_markup();
      if (!markup) {
        return std::unexpected(std::move(markup).error());
      }
      continue;
    }
    if (peek() == '&') {
      begin_text();
      Result<void> reference = consume_reference(text_);
      if (!reference) {
        return std::unexpected(std::move(reference).error());
      }
      continue;
    }
    begin_text();
    text_.push_back(peek());
    advance();
  }

  if (open_.size() > 1) {
    const Node& unclosed = open_.back().node;
    return fail(ErrorKind::HtmlParse,
                std::format("unclosed element `<{}>`: expected `</{}>` before the end of the input",
                            unclosed.tag, unclosed.tag),
                unclosed.location);
  }
  flush_text();
  return std::move(open_.front().node);
}

void write_location(JsonWriter& writer, const SourceLocation& location) {
  writer.begin_object();
  writer.key("line").value(location.line);
  writer.key("column").value(location.column);
  writer.end_object();
}

// Text は 1 つの object を書き切る。Element は "children" の配列を開いたままにして、
// 子を書き終えた呼び出し側が閉じる。
void begin_node(JsonWriter& writer, const Node& node) {
  writer.begin_object();
  if (node.type == Node::Type::Text) {
    writer.key("type").value("text");
    writer.key("text").value(node.text);
    writer.key("location");
    write_location(writer, node.location);
    writer.end_object();
    return;
  }
  writer.key("type").value("element");
  writer.key("tag").value(node.tag);
  writer.key("attrs").begin_array();
  for (const Attribute& attr : node.attrs) {
    writer.begin_object();
    writer.key("name").value(attr.name);
    writer.key("value").value(attr.value);
    writer.end_object();
  }
  writer.end_array();
  writer.key("location");
  write_location(writer, node.location);
  writer.key("children").begin_array();
}

}  // namespace

Result<Node> parse(std::string_view source, Diagnostics& diagnostics,
                   std::size_t max_nesting_depth) {
  // SourceLocation::offset は 32 bit。黙って切り詰めて誤った位置を報告しない。
  // これは実装の都合による絶対上限で、RenderLimits では緩められない。
  if (source.size() > kMaxSourceBytes) {
    return fail(ErrorKind::LimitExceeded,
                std::format("the HTML input is {} bytes, which exceeds the absolute limit of "
                            "{} bytes (4 GiB; source positions are 32-bit)",
                            source.size(), kMaxSourceBytes));
  }
  Parser parser{source, diagnostics, max_nesting_depth};
  return parser.run();
}

std::string dump_json(const Node& root) {
  struct Frame {
    const Node* node = nullptr;
    std::size_t next_child = 0;
  };

  JsonWriter writer;
  std::vector<Frame> stack;
  begin_node(writer, root);
  if (root.type == Node::Type::Element) {
    stack.push_back(Frame{.node = &root, .next_child = 0});
  }
  // 明示スタックで走査する（深い木でも再帰しない）
  while (!stack.empty()) {
    const Node& node = *stack.back().node;
    const std::size_t index = stack.back().next_child;
    if (index < node.children.size()) {
      stack.back().next_child = index + 1;
      const Node& child = node.children[index];
      begin_node(writer, child);
      if (child.type == Node::Type::Element) {
        stack.push_back(Frame{.node = &child, .next_child = 0});
      }
      continue;
    }
    writer.end_array();   // children
    writer.end_object();  // element
    stack.pop_back();
  }
  return std::move(writer).str();
}

}  // namespace shashoku::html
