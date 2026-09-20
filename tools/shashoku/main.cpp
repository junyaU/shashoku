// shashoku の CLI（ARCHITECTURE.md §3.10）。
//
//   shashoku input.html --font NotoSansJP.otf -o out.png --width 800
//
// 公開 API だけを使う（`src/` のヘッダは include しない）。ライブラリの利用例も兼ねる。
// 終了コード: 0 成功 / 1 レンダリングエラー・入出力エラー / 2 引数の誤り。
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "shashoku/shashoku.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

void print_usage(std::ostream& out) {
  out << R"(使い方: shashoku <input.html> --font <file> [options]

  --font <file>           フォント（複数指定可。指定順がフォールバック順）
  --image <name>=<file>   PNG 画像。<img src="name"> で参照する
  -o, --output <file>     出力先。PNG を書くときは必須（ダンプは省略で標準出力）
  --width <N>             ビューポートの幅（CSS px、既定 1200）
  --height <N>            ビューポートの高さ（CSS px、既定は内容の高さに追従）
  --scale <S>             出力倍率（既定 1.0。2.0 で Retina 向け）
  --overflow <policy>     あふれ処理 oidashi | oikomi | burasage（既定 oidashi）
  --line-break <mode>     行分割の厳しさ strict | normal | loose（既定 strict）
  --dump-stage <stage>    中間表現を出す dom | style | box | display-list | svg
  -h, --help              この使い方を表示する
)";
}

struct Arguments {
  std::string input;
  std::vector<std::string> fonts;
  std::vector<std::pair<std::string, std::string>> images;  // 名前 → パス
  std::string output;
  shashoku::RenderOptions options;
  std::optional<shashoku::DumpStage> stage;
  bool help = false;
};

// 引数の誤りは message を持って返る（使い方を出して終了コード 2）。
struct ArgumentError {
  std::string message;
};

std::optional<int> parse_int(std::string_view text) {
  int value = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const std::from_chars_result result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

// 浮動小数点の from_chars は libc++ 18 にまだ無く、strtof はロケールを読む。
// --scale に要るのは "2" や "1.5" のような単純な 10 進数だけなので自前で読む。
std::optional<float> parse_float(std::string_view text) {
  std::size_t index = 0;
  bool negative = false;
  if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
    negative = text[index] == '-';
    ++index;
  }
  const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
  double value = 0;
  std::size_t digits = 0;
  for (; index < text.size() && is_digit(text[index]); ++index, ++digits) {
    value = (value * 10) + static_cast<double>(text[index] - '0');
  }
  if (index < text.size() && text[index] == '.') {
    ++index;
    double place = 0.1;
    for (; index < text.size() && is_digit(text[index]); ++index, ++digits) {
      value += static_cast<double>(text[index] - '0') * place;
      place /= 10;
    }
  }
  if (digits == 0 || index != text.size()) {
    return std::nullopt;
  }
  return static_cast<float>(negative ? -value : value);
}

std::optional<shashoku::OverflowPolicy> parse_overflow(std::string_view text) {
  if (text == "oidashi") {
    return shashoku::OverflowPolicy::Oidashi;
  }
  if (text == "oikomi") {
    return shashoku::OverflowPolicy::Oikomi;
  }
  if (text == "burasage") {
    return shashoku::OverflowPolicy::Burasage;
  }
  return std::nullopt;
}

std::optional<shashoku::LineBreakStrictness> parse_strictness(std::string_view text) {
  if (text == "strict") {
    return shashoku::LineBreakStrictness::Strict;
  }
  if (text == "normal") {
    return shashoku::LineBreakStrictness::Normal;
  }
  if (text == "loose") {
    return shashoku::LineBreakStrictness::Loose;
  }
  return std::nullopt;
}

std::optional<shashoku::DumpStage> parse_stage(std::string_view text) {
  using shashoku::DumpStage;
  for (const DumpStage stage :
       {DumpStage::Dom, DumpStage::Style, DumpStage::Box, DumpStage::DisplayList, DumpStage::Svg}) {
    if (to_string(stage) == text) {
      return stage;
    }
  }
  return std::nullopt;
}

class Parser {
 public:
  explicit Parser(std::span<const std::string_view> args) : args_(args) {}

  std::expected<Arguments, ArgumentError> parse() {
    Arguments parsed;
    while (index_ < args_.size()) {
      const std::string_view arg = args_[index_++];
      if (arg == "-h" || arg == "--help") {
        parsed.help = true;
        return parsed;
      }
      if (const auto step = parse_one(arg, parsed); !step) {
        return std::unexpected(step.error());
      }
    }
    return finish(std::move(parsed));
  }

 private:
  static std::unexpected<ArgumentError> error(std::string message) {
    return std::unexpected(ArgumentError{std::move(message)});
  }

  // 値を取るオプションの一覧（shashoku の CLI に旗だけのオプションは無い）。
  static bool takes_value(std::string_view name) {
    using namespace std::string_view_literals;
    static constexpr std::array kWithValue{
        "--font"sv,   "--image"sv, "-o"sv,         "--output"sv,     "--width"sv,
        "--height"sv, "--scale"sv, "--overflow"sv, "--line-break"sv, "--dump-stage"sv};
    return std::ranges::find(kWithValue, name) != kWithValue.end();
  }

  // 引数 1 つぶん。`-` で始まらなければ入力ファイル。
  std::expected<void, ArgumentError> parse_one(std::string_view arg, Arguments& parsed) {
    if (!arg.starts_with('-')) {
      if (!parsed.input.empty()) {
        return error("入力ファイルが 2 つ以上あります: " + std::string(arg));
      }
      parsed.input = arg;
      return {};
    }
    const auto [name, inline_value] = split(arg);
    if (!takes_value(name)) {
      return error("知らないオプションです: " + std::string(arg));
    }
    const auto value = take(name, inline_value);
    if (!value) {
      return std::unexpected(value.error());
    }
    return apply(name, *value, parsed);
  }

  static std::expected<void, ArgumentError> apply(std::string_view name, std::string_view value,
                                                  Arguments& parsed) {
    if (name == "--font") {
      parsed.fonts.emplace_back(value);
      return {};
    }
    if (name == "-o" || name == "--output") {
      parsed.output = value;
      return {};
    }
    if (name == "--image") {
      return apply_image(value, parsed);
    }
    if (name == "--width" || name == "--height") {
      return apply_size(name, value, parsed);
    }
    if (name == "--scale") {
      return apply_scale(value, parsed);
    }
    if (name == "--overflow") {
      return apply_overflow(value, parsed);
    }
    if (name == "--line-break") {
      return apply_strictness(value, parsed);
    }
    return apply_stage(value, parsed);  // --dump-stage
  }

  static std::expected<void, ArgumentError> apply_image(std::string_view value, Arguments& parsed) {
    const std::size_t equals = value.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      return error("--image は <名前>=<パス> の形で指定します: " + std::string(value));
    }
    parsed.images.emplace_back(value.substr(0, equals), value.substr(equals + 1));
    return {};
  }

  static std::expected<void, ArgumentError> apply_size(std::string_view name,
                                                       std::string_view value, Arguments& parsed) {
    const std::optional<int> number = parse_int(value);
    if (!number) {
      return error(std::string(name) + " には整数を指定します: " + std::string(value));
    }
    if (name == "--width") {
      parsed.options.viewport_width = *number;
    } else {
      parsed.options.viewport_height = number;
    }
    return {};
  }

  static std::expected<void, ArgumentError> apply_scale(std::string_view value, Arguments& parsed) {
    const std::optional<float> number = parse_float(value);
    if (!number) {
      return error("--scale には数値を指定します: " + std::string(value));
    }
    parsed.options.scale = *number;
    return {};
  }

  static std::expected<void, ArgumentError> apply_overflow(std::string_view value,
                                                           Arguments& parsed) {
    const std::optional<shashoku::OverflowPolicy> policy = parse_overflow(value);
    if (!policy) {
      return error("--overflow は oidashi | oikomi | burasage のいずれかです: " +
                   std::string(value));
    }
    parsed.options.line_break.overflow = *policy;
    return {};
  }

  static std::expected<void, ArgumentError> apply_strictness(std::string_view value,
                                                             Arguments& parsed) {
    const std::optional<shashoku::LineBreakStrictness> strictness = parse_strictness(value);
    if (!strictness) {
      return error("--line-break は strict | normal | loose のいずれかです: " + std::string(value));
    }
    parsed.options.line_break.strictness = *strictness;
    return {};
  }

  static std::expected<void, ArgumentError> apply_stage(std::string_view value, Arguments& parsed) {
    const std::optional<shashoku::DumpStage> stage = parse_stage(value);
    if (!stage) {
      return error("--dump-stage は dom | style | box | display-list | svg のいずれかです: " +
                   std::string(value));
    }
    parsed.stage = stage;
    return {};
  }

  // 引数を読み切ったあとの整合性。
  static std::expected<Arguments, ArgumentError> finish(Arguments parsed) {
    if (parsed.input.empty()) {
      return error("入力の HTML ファイルを指定してください");
    }
    if (parsed.fonts.empty()) {
      return error("--font でフォントを 1 つ以上指定してください");
    }
    if (!parsed.stage && parsed.output.empty()) {
      return error("PNG の出力先を -o で指定してください（標準出力には書きません）");
    }
    return parsed;
  }

  // "--width=800" を ("--width", "800") に割る。`=` がなければ値は nullopt。
  static std::pair<std::string_view, std::optional<std::string_view>> split(std::string_view arg) {
    const std::size_t equals = arg.find('=');
    if (equals == std::string_view::npos) {
      return {arg, std::nullopt};
    }
    return {arg.substr(0, equals), arg.substr(equals + 1)};
  }

  std::expected<std::string_view, ArgumentError> take(
      std::string_view name, std::optional<std::string_view> inline_value) {
    if (inline_value) {
      return *inline_value;
    }
    if (index_ >= args_.size()) {
      return error(std::string(name) + " に値がありません");
    }
    return args_[index_++];
  }

  std::span<const std::string_view> args_;
  std::size_t index_ = 0;
};

std::optional<std::vector<std::uint8_t>> read_binary(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
  if (input.bad()) {
    return std::nullopt;
  }
  return bytes;
}

bool write_binary(const std::string& path, std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  return output.good();
}

int fail_with(std::string_view message) {
  std::cerr << "error: " << message << '\n';
  return kExitError;
}

int run(const Arguments& arguments) {
  const std::optional<std::vector<std::uint8_t>> source = read_binary(arguments.input);
  if (!source) {
    return fail_with("入力を読めません: " + arguments.input);
  }
  const std::string html(source->begin(), source->end());

  shashoku::FontSet fonts;
  for (const std::string& path : arguments.fonts) {
    const std::optional<std::vector<std::uint8_t>> bytes = read_binary(path);
    if (!bytes) {
      return fail_with("フォントを読めません: " + path);
    }
    fonts.add(*bytes);
  }

  shashoku::ImageSet images;
  for (const auto& [name, path] : arguments.images) {
    const std::optional<std::vector<std::uint8_t>> bytes = read_binary(path);
    if (!bytes) {
      return fail_with("画像を読めません: " + path);
    }
    images.add(name, *bytes);
  }

  if (arguments.stage) {
    const auto dumped = shashoku::dump(html, fonts, images, arguments.options, *arguments.stage);
    if (!dumped) {
      std::cerr << shashoku::to_string(dumped.error()) << '\n';
      return kExitError;
    }
    if (arguments.output.empty()) {
      std::cout << *dumped << '\n';
      return kExitOk;
    }
    const std::span<const std::uint8_t> bytes{reinterpret_cast<const std::uint8_t*>(dumped->data()),
                                              dumped->size()};
    if (!write_binary(arguments.output, bytes)) {
      return fail_with("出力を書けません: " + arguments.output);
    }
    return kExitOk;
  }

  const auto result = shashoku::render(html, fonts, images, arguments.options);
  if (!result) {
    std::cerr << shashoku::to_string(result.error()) << '\n';
    return kExitError;
  }
  for (const shashoku::Warning& warning : result->warnings) {
    std::cerr << "warning[" << shashoku::to_string(warning.kind) << "]: " << warning.detail << '\n';
  }
  if (!write_binary(arguments.output, result->png)) {
    return fail_with("出力を書けません: " + arguments.output);
  }
  return kExitOk;
}

int cli_main(std::span<const std::string_view> args) {
  const auto arguments = Parser(args).parse();
  if (!arguments) {
    std::cerr << "error: " << arguments.error().message << "\n\n";
    print_usage(std::cerr);
    return kExitUsage;
  }
  if (arguments->help) {
    print_usage(std::cout);
    return kExitOk;
  }
  return run(*arguments);
}

}  // namespace

int main(int argc, char** argv) {
  // shashoku 自身は例外を投げない（CLAUDE.md のコード規約）が、確保失敗や
  // iostream までは保証できない。main から例外を出さないための最後の網。
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    return cli_main(args);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return kExitError;
  } catch (...) {
    std::cerr << "error: 不明な例外\n";
    return kExitError;
  }
}
