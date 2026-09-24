// shashoku の CLI（ARCHITECTURE.md §3.10）。
//
//   shashoku input.html -o out.png --width 800
//
// 公開 API だけを使う（`src/` のヘッダは include しない）。ライブラリの利用例も兼ねる。
// 終了コード: 0 成功 / 1 レンダリングエラー・入出力エラー / 2 引数の誤り。
//
// `--font` を省いたときは、バイナリに埋め込んだ既定フォントを使う（#20 / A38）。
// **これは CLI 層だけの機能**で、ライブラリはバイト列しか受け取らない（embedded.hpp）。
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "embedded.hpp"
#include "shashoku/shashoku.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

// `--font` の説明だけは埋め込みの有無で変わる（SHASHOKU_EMBED_DEFAULT_FONT=OFF の
// ビルドで「省略できる」と書かない。#20）。
std::string_view font_option_help() {
  if (shashoku::cli::has_default_font()) {
    return R"(  --font <file>           フォント（複数指定可。指定順がフォールバック順）。
                          省略すると埋め込みの既定フォントを使う（--version で版を表示）
)";
  }
  return R"(  --font <file>           フォント（複数指定可。指定順がフォールバック順）。
                          **必須**（このビルドには既定フォントが埋め込まれていない）
)";
}

void print_usage(std::ostream& out) {
  out << R"(使い方: shashoku <input.html> [options]

)" << font_option_help()
      << R"(  --image <name>=<file>   PNG 画像。<img src="name"> で参照する
  -o, --output <file>     出力先。PNG を書くときは必須（ダンプは省略で標準出力）
  --width <N>             ビューポートの幅（CSS px、既定 1200）
  --height <N>            ビューポートの高さ（CSS px、既定は内容の高さに追従）
  --scale <S>             出力倍率（既定 1.0。2.0 で Retina 向け）
  --compression <N>       PNG の圧縮レベル 0〜9（既定 6。0 が最速、9 が最小）
  --overflow <policy>     あふれ処理 oidashi | oikomi | burasage（既定 oidashi）
  --line-break <mode>     行分割の厳しさ strict | normal | loose（既定 strict）
  --dump-stage <stage>    中間表現を出す dom | style | box | display-list | svg
  --strict                警告（豆腐・紙面からのはみ出し）もエラーにする。PNG は作らない
  --diagnostics <format>  診断の出し方 human | json（既定 human）。
                          json は標準出力に 1 オブジェクト（-o が必須。--dump-stage と併用不可）
  --version               版を表示する（shashoku・依存ライブラリ・既定フォント）
  --license               ライセンスを表示する（shashoku の MIT と第三者ソフトウェア）
  -h, --help              この使い方を表示する

約物の空き（JLREQ 3.1）:
  --trim-line-start       行頭の始め括弧の前の空きを詰める（天付き。既定は詰めない）
  --no-trim-line-end      行末の終わり括弧・句読点の後ろの空きを詰めない（既定は詰める）
  --no-collapse-punctuation
                          連続する約物の間の空きを詰めない（既定は詰める）
)";
}

// 試用報告（.github/ISSUE_TEMPLATE/）にそのまま貼れる形にする。
// 「どの版で組んだ PNG か」は依存ライブラリと既定フォントまで含めて決まる（A32）。
void print_version(std::ostream& out) {
  out << "shashoku " << shashoku::version() << '\n'
      << "  zlib " << SHASHOKU_ZLIB_VERSION << '\n'
      << "  FreeType " << SHASHOKU_FREETYPE_VERSION << '\n'
      << "  HarfBuzz " << SHASHOKU_HARFBUZZ_VERSION << '\n'
      << "  default font: " << shashoku::cli::default_font_version() << '\n';
}

// SIL OFL 1.1 が求める「ライセンス文の同梱」を、バイナリ 1 つでも満たすための出口。
void print_license(std::ostream& out) {
  out << shashoku::cli::license_text() << '\n' << shashoku::cli::third_party_license_text();
}

struct Arguments {
  std::string input;
  std::vector<std::string> fonts;
  std::vector<std::pair<std::string, std::string>> images;  // 名前 → パス
  std::string output;
  shashoku::RenderOptions options;
  std::optional<shashoku::DumpStage> stage;
  bool json = false;  // --diagnostics json（診断を標準出力に JSON で出す）
  bool help = false;
  bool version = false;
  bool license = false;
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
      // 情報を出すだけのオプションは、入力ファイルが無くても動く（そこで読み終わる）。
      if (arg == "-h" || arg == "--help") {
        parsed.help = true;
        return parsed;
      }
      if (arg == "--version") {
        parsed.version = true;
        return parsed;
      }
      if (arg == "--license") {
        parsed.license = true;
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

  // 値を取るオプションの一覧（旗だけのものは kFlags）。
  static bool takes_value(std::string_view name) {
    using namespace std::string_view_literals;
    static constexpr std::array kWithValue{"--font"sv,       "--image"sv,       "-o"sv,
                                           "--output"sv,     "--width"sv,       "--height"sv,
                                           "--scale"sv,      "--compression"sv, "--overflow"sv,
                                           "--line-break"sv, "--dump-stage"sv,  "--diagnostics"sv};
    return std::ranges::find(kWithValue, name) != kWithValue.end();
  }

  // 値を取らないオプション（約物の空きの切り替え）。
  static std::expected<void, ArgumentError> apply_flag(std::string_view name, Arguments& parsed) {
    if (name == "--strict") {
      // 警告（豆腐・紙面からのはみ出し）を失敗にする（A46）。判定は render() の中。
      parsed.options.warnings_as_errors = true;
      return {};
    }
    shashoku::LineBreakConfig& config = parsed.options.line_break;
    if (name == "--trim-line-start") {
      config.trim_line_start = true;
    } else if (name == "--no-trim-line-end") {
      config.trim_line_end = false;
    } else {
      config.collapse_punctuation_spacing = false;  // --no-collapse-punctuation
    }
    return {};
  }

  static bool is_flag(std::string_view name) {
    using namespace std::string_view_literals;
    static constexpr std::array kFlags{"--trim-line-start"sv, "--no-trim-line-end"sv,
                                       "--no-collapse-punctuation"sv, "--strict"sv};
    return std::ranges::find(kFlags, name) != kFlags.end();
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
    if (is_flag(name)) {
      if (inline_value) {
        return error(std::string(name) + " は値を取りません");
      }
      return apply_flag(name, parsed);
    }
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
    if (name == "--compression") {
      return apply_compression(value, parsed);
    }
    if (name == "--overflow") {
      return apply_overflow(value, parsed);
    }
    if (name == "--line-break") {
      return apply_strictness(value, parsed);
    }
    if (name == "--diagnostics") {
      return apply_diagnostics(value, parsed);
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

  // 範囲の検査は render() に任せる（オプションの正は 1 か所）。ここは形の検査だけ。
  static std::expected<void, ArgumentError> apply_compression(std::string_view value,
                                                              Arguments& parsed) {
    const std::optional<int> number = parse_int(value);
    if (!number) {
      return error("--compression には 0〜9 の整数を指定します: " + std::string(value));
    }
    parsed.options.compression_level = *number;
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

  // 診断の出し方（A46）。human は今までどおりの stderr、json は §3.10 の 1 オブジェクト。
  static std::expected<void, ArgumentError> apply_diagnostics(std::string_view value,
                                                              Arguments& parsed) {
    if (value == "human") {
      parsed.json = false;
      return {};
    }
    if (value == "json") {
      parsed.json = true;
      return {};
    }
    return error("--diagnostics は human | json のいずれかです: " + std::string(value));
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
    if (parsed.fonts.empty() && !shashoku::cli::has_default_font()) {
      return error(
          "--font でフォントを 1 つ以上指定してください"
          "（このビルドには既定フォントが埋め込まれていません）");
    }
    if (!parsed.stage && parsed.output.empty()) {
      return error("PNG の出力先を -o で指定してください（標準出力には書きません）");
    }
    // --diagnostics json は標準出力を占有するので、ダンプとは併用できず、PNG の出力先が要る
    // （ARCHITECTURE.md §3.10）。
    if (parsed.json && parsed.stage) {
      return error("--diagnostics json は --dump-stage と併用できません");
    }
    if (parsed.json && parsed.output.empty()) {
      return error("--diagnostics json のときは PNG の出力先を -o で指定してください");
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

// ---------------------------------------------------------------------------
// 診断 JSON（`--diagnostics json`。ARCHITECTURE.md §3.10）
//
// 機械が読む出口。安定した契約は識別子（`kind` / `warning` / `edge`）と入力位置で、
// `message` / `detail` / `hint` の文面は人向けなので版で変わりうる（A46）。
// CLI は `src/` のヘッダを見ない約束なので、内部の JsonWriter は使わず、ここに
// 必要なだけのエスケープを置く（出すのは 1 オブジェクト・1 行だけ）。
// ---------------------------------------------------------------------------

// `"` `\` と制御文字（< 0x20）をエスケープする。非 ASCII は UTF-8 のバイト列のまま出す。
void write_json_string(std::ostream& out, std::string_view text) {
  static constexpr std::string_view kHexDigits = "0123456789abcdef";
  out << '"';
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"') {
      out << R"(\")";
    } else if (c == '\\') {
      out << R"(\\)";
    } else if (c == '\n') {
      out << R"(\n)";
    } else if (c == '\r') {
      out << R"(\r)";
    } else if (c == '\t') {
      out << R"(\t)";
    } else if (byte < 0x20U) {
      out << R"(\u00)" << kHexDigits[byte >> 4U] << kHexDigits[byte & 0x0FU];
    } else {
      out << c;
    }
  }
  out << '"';
}

// `"line"` / `"column"` / `"offset"`。位置が分からなければ 3 つとも null（§3.10）。
void write_json_location(std::ostream& out,
                         const std::optional<shashoku::SourceLocation>& location) {
  if (!location) {
    out << R"("line": null, "column": null, "offset": null)";
    return;
  }
  out << R"("line": )" << location->line << R"(, "column": )" << location->column
      << R"(, "offset": )" << location->offset;
}

// 成功でも失敗でも同じ形の 1 オブジェクトを出すための入れ物。
struct JsonReport {
  bool ok = false;
  std::optional<int> width;   // 成功時だけ。失敗時は null
  std::optional<int> height;  // 同上
  bool truncated = false;
  std::vector<shashoku::RenderError> errors;
  std::vector<shashoku::Warning> warnings;
};

void write_json_error(std::ostream& out, const shashoku::RenderError& error) {
  out << R"({"kind": )";
  write_json_string(out, shashoku::to_string(error.kind));
  out << R"(, "message": )";
  write_json_string(out, error.message);
  out << R"(, "hint": )";
  write_json_string(out, error.hint);  // 無ければ ""
  out << ", ";
  write_json_location(out, error.location);
  out << R"(, "warning": )";
  if (error.warning) {  // --strict で格上げしたものだけ、元の警告の種類を持つ
    write_json_string(out, shashoku::to_string(*error.warning));
  } else {
    out << "null";
  }
  out << '}';
}

void write_json_warning(std::ostream& out, const shashoku::Warning& warning) {
  out << R"({"kind": )";
  write_json_string(out, shashoku::to_string(warning.kind));
  out << R"(, "detail": )";
  write_json_string(out, warning.detail);
  out << R"(, "codepoint": )" << static_cast<std::uint32_t>(warning.codepoint) << ", ";
  write_json_location(out, warning.location);
  // overflow_px は CSS px（A50）。float は元の値に戻せる最短表現で書く。
  out << R"(, "overflow_px": )" << std::format("{}", warning.overflow_px) << R"(, "edge": )";
  if (warning.overflow_edge == shashoku::OverflowEdge::None) {
    out << "null";  // content-overflow 以外（豆腐）には辺が無い
  } else {
    write_json_string(out, shashoku::to_string(warning.overflow_edge));
  }
  out << '}';
}

void print_diagnostics_json(std::ostream& out, const JsonReport& report) {
  const auto write_int_or_null = [&out](const std::optional<int>& value) {
    if (value) {
      out << *value;
    } else {
      out << "null";
    }
  };
  out << R"({"ok": )" << (report.ok ? "true" : "false") << R"(, "width": )";
  write_int_or_null(report.width);
  out << R"(, "height": )";
  write_int_or_null(report.height);
  out << R"(, "truncated": )" << (report.truncated ? "true" : "false") << R"(, "errors": [)";
  for (std::size_t i = 0; i < report.errors.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    write_json_error(out, report.errors[i]);
  }
  out << R"(], "warnings": [)";
  for (std::size_t i = 0; i < report.warnings.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    write_json_warning(out, report.warnings[i]);
  }
  out << "]}\n";
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
  if (arguments.fonts.empty()) {
    // 既定フォント（#20）。**順序は Regular → Bold**。`--font <Regular> --font <Bold>` と
    // 書いたときとバイト単位で同じ PNG が出ることを cli_test.cmake が検査している。
    fonts.add(shashoku::cli::default_font_regular());
    fonts.add(shashoku::cli::default_font_bold());
  }
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
    // **失敗したときは出力ファイルを作らない・上書きしない**（A46）。-o を開くのは
    // render() が成功してからなので、既存のファイルはそのまま残る。
    if (arguments.json) {
      print_diagnostics_json(std::cout, JsonReport{.ok = false,
                                                   .width = std::nullopt,
                                                   .height = std::nullopt,
                                                   .truncated = result.error().truncated,
                                                   .errors = result.error().errors,
                                                   .warnings = result.error().warnings});
    } else {
      // 1 行 1 件（hint は "  hint: …" の行）。--strict で格上げした警告もここに並ぶ。
      std::cerr << shashoku::to_string(result.error()) << '\n';
    }
    return kExitError;
  }
  if (!arguments.json) {
    // 警告は stderr（PNG は stdout / ファイル）。detail には入力位置が入っている
    // （"… at L:C"。to_string(RenderError) と同じ書式。ARCHITECTURE.md A31 / A46）
    for (const shashoku::Warning& warning : result->warnings) {
      std::cerr << "warning[" << shashoku::to_string(warning.kind) << "]: " << warning.detail
                << '\n';
    }
  }
  if (!write_binary(arguments.output, result->png)) {
    // 入出力の失敗は入力の診断ではないので、JSON のときも stderr に出す（JSON は出さない）。
    return fail_with("出力を書けません: " + arguments.output);
  }
  if (arguments.json) {
    print_diagnostics_json(std::cout, JsonReport{.ok = true,
                                                 .width = result->width,
                                                 .height = result->height,
                                                 .truncated = result->diagnostics_truncated,
                                                 .errors = {},
                                                 .warnings = result->warnings});
    return kExitOk;
  }
  // 成功したことと実際の寸法を 1 行で（--height を省いたときの高さが分かる）。
  std::cerr << "wrote " << arguments.output << " (" << result->width << 'x' << result->height
            << ")\n";
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
  if (arguments->version) {
    print_version(std::cout);
    return kExitOk;
  }
  if (arguments->license) {
    print_license(std::cout);
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
