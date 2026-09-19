#pragma once

// text モジュールの内部ヘッダ。FreeType / HarfBuzz のハンドルが見えるのはここだけで、
// 公開ヘッダ（font_store.hpp / shaper.hpp / freetype_glyph_source.hpp）には漏らさない。
// このヘッダを include してよいのは src/text/*.cpp だけ。

#include <ft2build.h>
#include <hb.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <freetype/freetype.h>

#include "core/ids.hpp"

namespace shashoku::text::detail {

// 1 face ぶんの実体。FontId は FontStoreImpl::fonts の添字。
struct FontEntry {
  // FT_New_Memory_Face / hb_blob はバイト列を参照したままなので、face より長生きさせる。
  // TTC は複数 face が同じバイト列を共有するため shared_ptr で持つ。
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;
  FT_Face ft_face = nullptr;
  hb_face_t* hb_face = nullptr;
  hb_font_t* hb_font = nullptr;  // font unit スケールのまま。cmap 引き専用（スケールを変えない）
  std::string family;
  std::string family_folded;  // ASCII を小文字に畳んだ照合用の名前
  int weight = 400;           // OS/2 usWeightClass
  bool italic = false;
  std::uint16_t upem = 1000;

  FontEntry() = default;
  FontEntry(const FontEntry&) = delete;
  FontEntry& operator=(const FontEntry&) = delete;
  FontEntry(FontEntry&&) = delete;
  FontEntry& operator=(FontEntry&&) = delete;
  ~FontEntry();
};

struct FontStoreImpl {
  FT_Library library = nullptr;
  std::vector<std::unique_ptr<FontEntry>> fonts;

  FontStoreImpl();
  FontStoreImpl(const FontStoreImpl&) = delete;
  FontStoreImpl& operator=(const FontStoreImpl&) = delete;
  FontStoreImpl(FontStoreImpl&&) = delete;
  FontStoreImpl& operator=(FontStoreImpl&&) = delete;
  ~FontStoreImpl();

  // 範囲外の FontId には nullptr を返す（落ちない）。
  [[nodiscard]] const FontEntry* at(FontId font) const noexcept {
    return font < fonts.size() ? fonts[font].get() : nullptr;
  }
};

}  // namespace shashoku::text::detail
