// 公開ヘッダだけで完結することの機械的な検査（ARCHITECTURE.md §3.10）。
//
// この翻訳単位は **include パスが `include/` だけ**のターゲットでコンパイルされる
// （tests/api/CMakeLists.txt）。公開ヘッダが `src/` の内部ヘッダや FreeType / HarfBuzz を
// 引いていたら、ここで「ファイルが見つからない」になって落ちる。
//
// 公開 API の型が実際に使えること（不完全型を返していないこと）もここで確かめる。
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "shashoku/shashoku.hpp"

namespace shashoku::header_check {

// api_test からリンクして呼ぶ。呼べる = このターゲットが確かにビルドされている。
int use_public_api() {
  FontSet fonts;
  const std::uint8_t byte = 0;
  fonts.add(std::span<const std::uint8_t>(&byte, 1));

  ImageSet images;
  images.add("icon", std::span<const std::uint8_t>(&byte, 1));

  RenderOptions options;
  options.viewport_width = 320;
  options.viewport_height = 200;
  options.scale = 2.0F;
  options.line_break.overflow = OverflowPolicy::Burasage;
  options.line_break.strictness = LineBreakStrictness::Loose;
  options.line_break.extra_line_start_prohibited = U"〆";

  // フォントが壊れている（1 バイト）ので必ず失敗する。ここで見たいのは
  // 「公開ヘッダだけで render() を呼んでエラーを読めること」。
  const std::expected<RenderResult, RenderError> result = render("<p>あ</p>", fonts, images, options);
  if (result) {
    return static_cast<int>(result->png.size() + result->warnings.size());
  }
  const std::string message = to_string(result.error());
  const std::string_view kind = to_string(result.error().kind);
  const std::string_view stage = to_string(DumpStage::DisplayList);
  const std::string_view warning = to_string(WarningKind::MissingGlyph);
  const std::string_view release = version();
  return static_cast<int>(message.size() + kind.size() + stage.size() + warning.size() +
                          release.size()) == 0
             ? 1
             : 0;
}

}  // namespace shashoku::header_check
