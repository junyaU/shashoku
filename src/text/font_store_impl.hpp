#pragma once

// text モジュールの内部ヘッダ。FreeType / HarfBuzz のハンドルが見えるのはここだけで、
// 公開ヘッダ（font_store.hpp / shaper.hpp / freetype_glyph_source.hpp）には漏らさない。
// このヘッダを include してよいのは src/text/*.cpp だけ。

#include <ft2build.h>
#include <hb.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <freetype/freetype.h>

#include "core/ids.hpp"

namespace shashoku::text::detail {

// FreeType のエラーコードを人が読める文字列にする（FT_CONFIG_OPTION_ERROR_STRINGS が
// 無効なビルドでは番号だけ）。font_store.cpp と freetype_glyph_source.cpp が使う。
inline std::string ft_error_text(FT_Error error) {
  const char* text = FT_Error_String(error);
  return text != nullptr ? std::string(text) : ("FreeType error " + std::to_string(error));
}

// 不変（immutable）にした hb_font を持ち、**読み取り専用の用途だけ**を関数で出す入れ物。
//
// 生ポインタを外に出さないのが肝（ARCHITECTURE.md A34）: HarfBuzz の setter は immutable な
// オブジェクトに対して「黙って失敗する」ので、`hb_font_set_scale()` を呼びたいコードが
// この共有フォントを掴むと、スケールが変わらないまま組まれる、という見つけにくい壊れ方を
// する。スケールを変えたい側（Shaper）は face から自分の hb_font を作ること。
class ImmutableFont {
 public:
  ImmutableFont() = default;
  // hb_font_create の戻り値の所有権を受け取り、不変にする。
  explicit ImmutableFont(hb_font_t* font) noexcept : font_(font) { hb_font_make_immutable(font_); }
  ImmutableFont(const ImmutableFont&) = delete;
  ImmutableFont& operator=(const ImmutableFont&) = delete;
  ImmutableFont(ImmutableFont&& other) noexcept : font_(std::exchange(other.font_, nullptr)) {}
  ImmutableFont& operator=(ImmutableFont&& other) noexcept {
    std::swap(font_, other.font_);
    return *this;
  }
  ~ImmutableFont() {
    if (font_ != nullptr) {
      hb_font_destroy(font_);
    }
  }

  // cmap 引き。グリフを持たなければ 0（.notdef）。
  [[nodiscard]] hb_codepoint_t nominal_glyph(char32_t cp) const noexcept {
    hb_codepoint_t glyph = 0;
    if (hb_font_get_nominal_glyph(font_, cp, &glyph) == 0) {
      return 0;
    }
    return glyph;
  }

  // 縦組み用グリフ（vert）の有無を調べるためだけのシェーピング。位置は見ない。
  // バッファは呼び出し側（Shaper = 実行ごと）のもの。
  void shape_for_probe(hb_buffer_t* buffer) const noexcept { hb_shape(font_, buffer, nullptr, 0); }

 private:
  hb_font_t* font_ = nullptr;  // font unit スケールのまま。cmap 引きと vert の調査専用
};

// 1 face ぶんの**共有資源**（ARCHITECTURE.md A34）。FontId は FontStoreImpl::fonts の添字。
//
// ここに置いてよいのは「構築が終わったあとは読むだけ」のものに限る。何本の render() から
// 同時に参照しても結果が変わらないことが FontStore の契約なので、**FreeType のハンドルは
// 持たない**: `FT_Face` は 1 スレッドからしか使えず、同じ `FT_Library` に対する生成・破棄も
// 直列化が要る（freetype.h の "An FT_Face object can only be safely used from one thread at
// a time" の注記）。`FT_Library` / `FT_Face` は実行ごとに FreeTypeGlyphSource が作るので、
// 作り直すのに要る face_index だけをここに覚えておく。
struct FontEntry {
  // FT_New_Memory_Face と hb_blob はバイト列を参照したままなので、face より長生きさせる。
  // TTC は複数 face が同じバイト列を共有するため shared_ptr で持つ。
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;
  FT_Long face_index = 0;  // TTC の何番目の face か（FT_Face を作り直すのに要る）
  // 不変にした face。Shaper はここから**自分の** hb_font を作る（スケールを変えたいので）。
  hb_face_t* hb_face = nullptr;
  ImmutableFont font;  // 読み取り専用の hb_font（上の注意を参照）
  std::string family;
  std::string family_folded;  // ASCII を小文字に畳んだ照合用の名前
  // 色データ（COLR）だけを持ち、単色の輪郭が空のグリフ（A43 / issue #27）。**昇順・重複なし**
  // で、二分探索で引く。load() のときに作ってからは読むだけ（A34）。色データを持たない
  // フォントでは必ず空なので、ふつうのフォントの引き当ては空かどうかの判定 1 回で終わる。
  std::vector<GlyphId> color_only_glyphs;
  std::size_t color_probe_count = 0;  // 上を作るのに調べたグリフ数（テスト用の統計）
  int weight = 400;                   // OS/2 usWeightClass
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
  std::vector<std::unique_ptr<FontEntry>> fonts;

  // 範囲外の FontId には nullptr を返す（落ちない）。
  [[nodiscard]] const FontEntry* at(FontId font) const noexcept {
    return font < fonts.size() ? fonts[font].get() : nullptr;
  }
};

}  // namespace shashoku::text::detail
