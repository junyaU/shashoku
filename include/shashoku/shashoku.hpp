#pragma once

// shashoku（写植）— 日本語組版特化の HTML→PNG レンダリングエンジン。
// これ 1 つを include すれば公開 API がすべて揃う（DESIGN.md §8）。
//
//   #include <shashoku/shashoku.hpp>
//
//   shashoku::FontSet fonts;
//   fonts.add(noto_sans_jp_bytes);              // 追加順がフォールバック順
//   const auto result = shashoku::render("<p>こんにちは、世界。</p>", fonts);
//   if (!result) { std::cerr << shashoku::to_string(result.error()) << '\n'; return 1; }
//   write_file("out.png", result->png);
//
// このディレクトリのヘッダは `include/` 配下と標準ライブラリしか include しない。
// FreeType / HarfBuzz / `src/` の内部型は公開 API に一切現れない（ARCHITECTURE.md §3.10）。

#include "shashoku/error.hpp"
#include "shashoku/font_set.hpp"
#include "shashoku/image_set.hpp"
#include "shashoku/options.hpp"
#include "shashoku/render.hpp"
#include "shashoku/version.hpp"
#include "shashoku/warning.hpp"
