// 固定シードの乱数による性質テスト（DESIGN.md §10-4 / ARCHITECTURE.md §4）。
// (a) 正しい HTML をランダムに組み立てて、パース結果が組み立て元の木と一致すること
// (b) それを壊したもの・完全なランダムバイト列で、落ちずに成功かエラーを返すこと
//     （ASan プリセットで回すことに意味がある）
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/diagnostics.hpp"
#include "core/result.hpp"
#include "core/utf8.hpp"
#include "html/dom.hpp"
#include "html/parser.hpp"
#include "shashoku/error.hpp"
#include "shashoku/limits.hpp"

namespace shashoku::html {
namespace {

constexpr std::uint32_t kSeed = 20260919U;
constexpr std::size_t kMaxGeneratedDepth = 6;

constexpr std::array<std::string_view, 10> kContainerTags{"div", "span", "p",  "h1",   "h2",
                                                          "h3",  "rp",   "rt", "ruby", "h6"};
constexpr std::array<std::string_view, 2> kGeneratedVoidTags{"br", "img"};
constexpr std::array<std::string_view, 3> kCommonAttributeNames{"class", "id", "style"};
constexpr std::array<std::string_view, 7> kImgAttributeNames{"alt", "class", "height", "id",
                                                             "src", "style", "width"};

constexpr std::array<char32_t, 10> kTextAlphabet{U'a', U'b', U'z', U' ',  U'\n',
                                                 U'&', U'<', U'>', U'あ', U'\U0001F600'};
constexpr std::array<char32_t, 11> kValueAlphabet{U'a', U'b', U'1', U'-',  U'.', U' ',
                                                  U'&', U'<', U'"', U'\'', U'あ'};

std::string upper_ascii(std::string_view s) {
  std::string out;
  for (const char c : s) {
    out.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c);
  }
  return out;
}

bool is_simple_value(std::string_view value) {
  return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
  });
}

std::span<const std::string_view> attribute_names_for(std::string_view tag) {
  if (tag == "img") {
    return kImgAttributeNames;
  }
  return kCommonAttributeNames;
}

// 位置を見ない比較のために SourceLocation を既定値に揃える（再帰しない）。
void clear_locations(Node& root) {
  std::vector<Node*> stack{&root};
  while (!stack.empty()) {
    Node* node = stack.back();
    stack.pop_back();
    node->location = SourceLocation{};
    for (Attribute& attr : node->attrs) {
      attr.location = SourceLocation{};
    }
    for (Node& child : node->children) {
      stack.push_back(&child);
    }
  }
}

Node make_element(std::string tag) {
  Node node;
  node.type = Node::Type::Element;
  node.tag = std::move(tag);
  return node;
}

Node make_text(std::string value) {
  Node node;
  node.type = Node::Type::Text;
  node.text = std::move(value);
  return node;
}

struct Generated {
  Node root;
  std::string html;
};

// 正しい HTML と、そこから得られるはずの DOM を同時に組み立てる。
// 木の構築も直列化も明示スタックで行う（再帰しない）。
class HtmlGenerator {
 public:
  explicit HtmlGenerator(std::uint32_t seed) : rng_(seed) {}

  Generated generate();
  std::mt19937& rng() { return rng_; }

 private:
  std::size_t pick(std::size_t count) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng_);
  }
  bool chance(std::size_t percent) { return pick(100) < percent; }
  std::string spell(std::string_view tag) {
    return chance(20) ? upper_ascii(tag) : std::string{tag};
  }

  void append_text_char(std::string& model, std::string& html, char32_t cp);
  std::string random_value();
  bool append_attribute(std::string& html, std::string_view name, std::string_view value);
  bool append_attributes(Node& element, std::string& html);
  void open_container(std::vector<Node>& stack, std::string& html);
  void close_container(std::vector<Node>& stack, std::string& html);
  void emit_void(std::vector<Node>& stack, std::string& html);
  void emit_text(std::vector<Node>& stack, std::string& html);

  std::mt19937 rng_;
};

void HtmlGenerator::append_text_char(std::string& model, std::string& html, char32_t cp) {
  append_utf8(model, cp);
  const auto value = static_cast<std::uint32_t>(cp);
  const std::size_t style = pick(4);
  if (style == 0) {
    html += std::format("&#{};", value);
    return;
  }
  if (style == 1) {
    html += std::format("&#x{:X};", value);
    return;
  }
  // `&` と `<` は生のままでは書けない（厳格なサブセットなのでエラーになる）
  if (cp == U'&') {
    html += "&amp;";
    return;
  }
  if (cp == U'<') {
    html += "&lt;";
    return;
  }
  if (cp == U'>' && style == 2) {
    html += "&gt;";
    return;
  }
  append_utf8(html, cp);
}

std::string HtmlGenerator::random_value() {
  std::string value;
  const std::size_t length = pick(5);
  for (std::size_t i = 0; i < length; ++i) {
    append_utf8(value, kValueAlphabet[pick(kValueAlphabet.size())]);
  }
  return value;
}

// 引用符なしで書いたら true（直後に `/>` を書けなくなる）。
bool HtmlGenerator::append_attribute(std::string& html, std::string_view name,
                                     std::string_view value) {
  html += name;
  if (value.empty() && chance(50)) {
    return false;  // 値なし属性
  }
  html += '=';
  if (is_simple_value(value) && chance(40)) {
    html += value;
    return true;
  }
  const char quote = chance(50) ? '"' : '\'';
  html += quote;
  for (const char c : value) {
    if (c == '&') {
      html += "&amp;";
    } else if (c == '<') {
      html += "&lt;";
    } else if (c == quote) {
      html += (quote == '"') ? "&quot;" : "&apos;";
    } else {
      html += c;
    }
  }
  html += quote;
  return false;
}

bool HtmlGenerator::append_attributes(Node& element, std::string& html) {
  const std::span<const std::string_view> allowed = attribute_names_for(element.tag);
  bool ends_unquoted = false;
  const std::size_t count = pick(3);
  for (std::size_t i = 0; i < count; ++i) {
    const std::string_view name = allowed[pick(allowed.size())];
    if (element.find_attr(name) != nullptr) {
      continue;  // 属性の重複は正しい HTML ではないので作らない
    }
    std::string value = random_value();
    html += ' ';
    ends_unquoted = append_attribute(html, name, value);
    element.attrs.push_back(
        Attribute{.name = std::string{name}, .value = std::move(value), .location = {}});
  }
  return ends_unquoted;
}

void HtmlGenerator::open_container(std::vector<Node>& stack, std::string& html) {
  Node element = make_element(std::string{kContainerTags[pick(kContainerTags.size())]});
  html += '<';
  html += spell(element.tag);
  append_attributes(element, html);
  html += '>';
  stack.push_back(std::move(element));
}

void HtmlGenerator::close_container(std::vector<Node>& stack, std::string& html) {
  Node closed = std::move(stack.back());
  stack.pop_back();
  html += "</";
  html += spell(closed.tag);
  html += '>';
  stack.back().children.push_back(std::move(closed));
}

void HtmlGenerator::emit_void(std::vector<Node>& stack, std::string& html) {
  Node element = make_element(std::string{kGeneratedVoidTags[pick(kGeneratedVoidTags.size())]});
  html += '<';
  html += spell(element.tag);
  const bool ends_unquoted = append_attributes(element, html);
  // 引用符なしの値のあとに `/` は書けない（値の一部と紛れるのでエラーになる）
  html += (!ends_unquoted && chance(30)) ? "/>" : ">";
  stack.back().children.push_back(std::move(element));
}

void HtmlGenerator::emit_text(std::vector<Node>& stack, std::string& html) {
  Node& parent = stack.back();
  if (!parent.children.empty() && parent.children.back().type == Node::Type::Text) {
    return;  // 隣り合うテキストは 1 ノードにまとまるので、2 つ作らない
  }
  std::string model;
  const std::size_t length = 1 + pick(5);
  for (std::size_t i = 0; i < length; ++i) {
    append_text_char(model, html, kTextAlphabet[pick(kTextAlphabet.size())]);
  }
  parent.children.push_back(make_text(std::move(model)));
}

Generated HtmlGenerator::generate() {
  std::string html;
  std::vector<Node> stack;
  stack.push_back(make_element("#root"));

  const std::size_t steps = 8 + pick(24);
  for (std::size_t i = 0; i < steps; ++i) {
    const std::size_t action = pick(10);
    if (action < 4 && stack.size() < kMaxGeneratedDepth) {
      open_container(stack, html);
    } else if (action < 6) {
      emit_void(stack, html);
    } else if (action < 9) {
      emit_text(stack, html);
    } else if (stack.size() > 1) {
      close_container(stack, html);
    }
  }
  while (stack.size() > 1) {
    close_container(stack, html);
  }
  return Generated{.root = std::move(stack.front()), .html = std::move(html)};
}

// 落ちないこと・契約（エラーには必ず位置とメッセージが付く）を守ることだけを見る。
void expect_no_crash(std::string_view source) {
  Diagnostics diagnostics{RenderLimits{}.max_diagnostics};
  Result<Node> result = parse(source, diagnostics);
  if (result) {
    EXPECT_FALSE(dump_json(*result).empty());
    return;
  }
  const Error& error = result.error();
  EXPECT_FALSE(error.message.empty());
  EXPECT_TRUE(error.location.has_value()) << to_string(error);
}

std::string mutate(std::mt19937& rng, std::string source) {
  const std::size_t rounds = 1 + (rng() % 4);
  for (std::size_t round = 0; round < rounds && !source.empty(); ++round) {
    const std::size_t at = rng() % source.size();
    switch (rng() % 3) {
      case 0:
        source[at] = static_cast<char>(rng() % 256);
        break;
      case 1:
        source.insert(at, 1, static_cast<char>(rng() % 256));
        break;
      default:
        source.erase(at, 1);
        break;
    }
  }
  return source;
}

TEST(HtmlFuzz, GeneratedHtmlRoundTrips) {
  HtmlGenerator generator(kSeed);
  for (int i = 0; i < 300; ++i) {
    const Generated sample = generator.generate();
    SCOPED_TRACE(sample.html);
    Diagnostics diagnostics{RenderLimits{}.max_diagnostics};
    Result<Node> result = parse(sample.html, diagnostics);
    ASSERT_TRUE(result.has_value()) << to_string(result.error());
    Node actual = std::move(*result);
    clear_locations(actual);
    EXPECT_EQ(dump_json(actual), dump_json(sample.root));
  }
}

TEST(HtmlFuzz, MutatedHtmlNeverCrashes) {
  HtmlGenerator generator(kSeed + 1);
  for (int i = 0; i < 1500; ++i) {
    const Generated sample = generator.generate();
    expect_no_crash(mutate(generator.rng(), sample.html));
  }
}

TEST(HtmlFuzz, RandomBytesNeverCrash) {
  // 種の固定はこのテストの前提そのもの（ARCHITECTURE.md §4「乱数の種を固定した性質テスト」）。
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): 決定性のため意図的に固定する
  std::mt19937 rng(kSeed + 2);
  for (int i = 0; i < 1500; ++i) {
    const std::size_t length = rng() % 200;
    std::string source;
    source.reserve(length);
    for (std::size_t k = 0; k < length; ++k) {
      source.push_back(static_cast<char>(rng() % 256));
    }
    expect_no_crash(source);
  }
}

TEST(HtmlFuzz, RandomMarkupSoupNeverCrashes) {
  // パーサの奥まで届きやすいよう、HTML の区切り文字だけで作った入力も回す。
  constexpr std::string_view kSoup = "<>/=\"'&;#!- \n\tdivspanbrimgstyle123";
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp): 決定性のため意図的に固定する
  std::mt19937 rng(kSeed + 3);
  for (int i = 0; i < 3000; ++i) {
    const std::size_t length = rng() % 120;
    std::string source;
    source.reserve(length);
    for (std::size_t k = 0; k < length; ++k) {
      source.push_back(kSoup[rng() % kSoup.size()]);
    }
    expect_no_crash(source);
  }
}

}  // namespace
}  // namespace shashoku::html
