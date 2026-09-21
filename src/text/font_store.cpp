#include "text/font_store.hpp"

#include <ft2build.h>
#include <hb.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <freetype/freetype.h>
#include <freetype/tttables.h>
#include <hb-ot.h>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "text/font_store_impl.hpp"

namespace shashoku::text {
namespace {

using detail::FontEntry;
using detail::FontStoreImpl;
using detail::ft_error_text;

// FreeType のハンドルを **load() の中だけ**で使うための後始末つきの入れ物。
// 例外を投げない方針なので早期 return が多い。解放漏れを型で塞ぐ。
class LibraryHandle {
 public:
  LibraryHandle() = default;
  LibraryHandle(const LibraryHandle&) = delete;
  LibraryHandle& operator=(const LibraryHandle&) = delete;
  LibraryHandle(LibraryHandle&&) = delete;
  LibraryHandle& operator=(LibraryHandle&&) = delete;
  ~LibraryHandle() {
    if (library_ != nullptr) {
      FT_Done_FreeType(library_);
    }
  }

  [[nodiscard]] FT_Library get() const noexcept { return library_; }
  [[nodiscard]] FT_Library* out() noexcept { return &library_; }

 private:
  FT_Library library_ = nullptr;
};

class FaceHandle {
 public:
  FaceHandle() = default;
  FaceHandle(const FaceHandle&) = delete;
  FaceHandle& operator=(const FaceHandle&) = delete;
  FaceHandle(FaceHandle&&) = delete;
  FaceHandle& operator=(FaceHandle&&) = delete;
  ~FaceHandle() {
    if (face_ != nullptr) {
      FT_Done_Face(face_);
    }
  }

  [[nodiscard]] FT_Face get() const noexcept { return face_; }
  [[nodiscard]] FT_Face* out() noexcept { return &face_; }

 private:
  FT_Face face_ = nullptr;
};

// HarfBuzz のプロセス全体の遅延初期化（既定の Unicode 関数群・言語タグの表）を、
// 共有前に単一スレッドから 1 回済ませておく。HarfBuzz 自身のスレッドテストも同じ理由で
// 先に 1 回呼んでいる（test/threads/hb-shape-threads.cc）。A34。
void prime_harfbuzz_process_tables() {
  static_cast<void>(hb_unicode_funcs_get_default());
  static_cast<void>(hb_language_from_string("ja", -1));
}

std::string fold_ascii(std::string_view name) {
  std::string folded(name);
  for (char& c : folded) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return folded;
}

int read_weight(FT_Face face) {
  // OS/2 が無い（CFF の一部や壊れたフォント）場合は style_flags から推定する。
  const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
  if (os2 != nullptr && os2->version != 0xFFFFU && os2->usWeightClass != 0) {
    return static_cast<int>(os2->usWeightClass);
  }
  return (face->style_flags & FT_STYLE_FLAG_BOLD) != 0 ? 700 : 400;
}

// FT_Face を一時的に 1 つ作って family / weight / upem を読み、HarfBuzz の face / font を
// 用意して FontEntry に詰める。**FT_Face はこの関数を出るときに閉じる**（A34）:
// 解析結果と HarfBuzz の不変オブジェクトだけが共有資源として残る。
// face 0 では TTC の face 数を num_faces に返す。
Result<std::unique_ptr<FontEntry>> make_entry(
    FT_Library library, const std::shared_ptr<const std::vector<std::uint8_t>>& bytes,
    FT_Long face_index, FT_Long& num_faces) {
  FaceHandle face;
  const FT_Error error = FT_New_Memory_Face(
      library, bytes->data(), static_cast<FT_Long>(bytes->size()), face_index, face.out());
  if (error != 0 || face.get() == nullptr) {
    return fail(ErrorKind::FontLoad, "cannot parse the font (face " + std::to_string(face_index) +
                                         "): " + ft_error_text(error));
  }
  num_faces = face.get()->num_faces;

  // 埋め込みビットマップ専用のフォント（CBDT / CBLC・sbix のカラー絵文字など）は、
  // FreeType が FT_FACE_FLAG_SCALABLE を立てない。ラスタライザは輪郭しか扱えないので
  // （glyph_source.hpp）、字が全部消えた PNG を出す前にここで落とす（fail loudly）。
  if (FT_IS_SCALABLE(face.get()) == 0 || face.get()->num_glyphs <= 0 ||
      face.get()->units_per_EM == 0) {
    return fail(ErrorKind::FontLoad,
                "the font has no outlines (face " + std::to_string(face_index) +
                    "): bitmap-only fonts (CBDT / sbix color emoji) are not supported");
  }

  auto entry = std::make_unique<FontEntry>();
  entry->bytes = bytes;
  entry->face_index = face_index;
  entry->upem = face.get()->units_per_EM;
  // family / weight / italic は**今までどおり FreeType から読む**。HarfBuzz の name 表から
  // 読み直すと値が変わり、フォールバック順（§3.5）が静かに変わりうるため。
  entry->family = face.get()->family_name != nullptr ? face.get()->family_name : "";
  entry->family_folded = fold_ascii(entry->family);
  entry->weight = read_weight(face.get());
  entry->italic = (face.get()->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

  // HarfBuzz は自前の OpenType 実装でフォントを読む（A7: hb-ft は使わない）。
  // バイト列の寿命は FontEntry が持つので、blob には解放関数を渡さない。
  hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(bytes->data()),
                                   static_cast<unsigned int>(bytes->size()),
                                   HB_MEMORY_MODE_READONLY, nullptr, nullptr);
  hb_face_t* hb_face = hb_face_create(blob, static_cast<unsigned int>(face_index));
  hb_blob_destroy(blob);
  if (hb_face_get_glyph_count(hb_face) == 0) {
    hb_face_destroy(hb_face);
    return fail(ErrorKind::FontLoad,
                "HarfBuzz cannot parse the font (face " + std::to_string(face_index) + ")");
  }
  // 不変にしてから共有する。HarfBuzz は immutable なオブジェクトを複数スレッドから
  // 同時に使うことを想定していて、face が内部に持つ表とシェーププランの置き場は
  // アトミックに保護されている（A34）。
  hb_face_make_immutable(hb_face);

  hb_font_t* hb_font = hb_font_create(hb_face);
  if (hb_font == hb_font_get_empty()) {
    // hb_font_create は確保に失敗すると空のフォント（不変の共有オブジェクト）を返す。
    // 黙って通すと「どの文字にもグリフが無い」= 全部豆腐になるので、ここで落とす。
    hb_face_destroy(hb_face);
    return fail(ErrorKind::OutOfMemory,
                "cannot create the HarfBuzz font (face " + std::to_string(face_index) + ")");
  }
  hb_ot_font_set_funcs(hb_font);  // メトリクスは hb-ot から読む（A7）

  entry->hb_face = hb_face;
  entry->font = detail::ImmutableFont(hb_font);  // ここで不変になる
  return entry;
}

}  // namespace

namespace detail {

FontEntry::~FontEntry() {
  // ここで落とすのは face への参照 1 つぶん。hb_font はそのあとメンバ（ImmutableFont）の
  // デストラクタが解放するが、font 自身も face の参照を持っているので順序は問わない。
  if (hb_face != nullptr) {
    hb_face_destroy(hb_face);
  }
}

const FontStoreImpl& FontStoreAccess::impl(const FontStore& store) noexcept { return *store.impl_; }

}  // namespace detail

FontStore::FontStore() : impl_(std::make_unique<FontStoreImpl>()) {}
FontStore::~FontStore() = default;
FontStore::FontStore(FontStore&&) noexcept = default;
FontStore& FontStore::operator=(FontStore&&) noexcept = default;

Result<FontId> FontStore::load(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return fail(ErrorKind::FontLoad, "the font byte sequence is empty");
  }
  prime_harfbuzz_process_tables();

  // FT_Library はここで作ってここで閉じる（共有資源には残さない。A34）。
  LibraryHandle library;
  if (FT_Init_FreeType(library.out()) != 0) {
    return fail(ErrorKind::Internal, "cannot initialize FreeType");
  }

  auto data = std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end());

  // TTC / OTC は全 face を読み込む。どれか 1 つでも読めなければ 1 つも追加しない
  // （半端に登録された FontId が残らないようにする）。
  std::vector<std::unique_ptr<FontEntry>> loaded;
  FT_Long face_count = 1;
  for (FT_Long index = 0; index < face_count; ++index) {
    FT_Long num_faces = 1;
    auto entry = make_entry(library.get(), data, index, num_faces);
    if (!entry) {
      return std::unexpected(entry.error());
    }
    if (index == 0) {
      face_count = num_faces >= 1 ? num_faces : 1;
    }
    loaded.push_back(std::move(*entry));
  }

  const auto first = static_cast<FontId>(impl_->fonts.size());
  for (auto& entry : loaded) {
    impl_->fonts.push_back(std::move(entry));
  }
  return first;
}

std::size_t FontStore::size() const noexcept { return impl_->fonts.size(); }

bool FontStore::contains(FontId font) const noexcept { return impl_->at(font) != nullptr; }

std::string_view FontStore::family(FontId font) const noexcept {
  const FontEntry* entry = impl_->at(font);
  return entry != nullptr ? std::string_view(entry->family) : std::string_view();
}

int FontStore::weight(FontId font) const noexcept {
  const FontEntry* entry = impl_->at(font);
  return entry != nullptr ? entry->weight : 400;
}

bool FontStore::is_italic(FontId font) const noexcept {
  const FontEntry* entry = impl_->at(font);
  return entry != nullptr && entry->italic;
}

std::uint16_t FontStore::units_per_em(FontId font) const noexcept {
  const FontEntry* entry = impl_->at(font);
  return entry != nullptr ? entry->upem : 0;
}

GlyphId FontStore::glyph_for(FontId font, char32_t cp) const noexcept {
  const FontEntry* entry = impl_->at(font);
  if (entry == nullptr) {
    return 0;
  }
  return static_cast<GlyphId>(entry->font.nominal_glyph(cp));
}

}  // namespace shashoku::text
