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

// FT_Face を 1 つ作り、HarfBuzz の face / font を用意して FontEntry に詰める。
Result<std::unique_ptr<FontEntry>> make_entry(
    FT_Library library, const std::shared_ptr<const std::vector<std::uint8_t>>& bytes,
    FT_Long face_index) {
  FT_Face face = nullptr;
  const FT_Error error = FT_New_Memory_Face(library, bytes->data(),
                                            static_cast<FT_Long>(bytes->size()), face_index, &face);
  if (error != 0 || face == nullptr) {
    return fail(ErrorKind::FontLoad, "フォントを解釈できません (face " +
                                         std::to_string(face_index) + "): " + ft_error_text(error));
  }

  auto entry = std::make_unique<FontEntry>();
  entry->bytes = bytes;
  entry->ft_face = face;  // ここから先の失敗でも ~FontEntry が face を解放する

  // 埋め込みビットマップ専用のフォント（CBDT / CBLC・sbix のカラー絵文字など）は、
  // FreeType が FT_FACE_FLAG_SCALABLE を立てない。ラスタライザは輪郭しか扱えないので
  // （glyph_source.hpp）、字が全部消えた PNG を出す前にここで落とす（fail loudly）。
  if (FT_IS_SCALABLE(face) == 0 || face->num_glyphs <= 0 || face->units_per_EM == 0) {
    return fail(ErrorKind::FontLoad,
                "輪郭を持たないフォントは扱えません (face " + std::to_string(face_index) +
                    "): 埋め込みビットマップ専用のフォント（CBDT / sbix のカラー絵文字など）は"
                    "対応していません");
  }

  entry->upem = face->units_per_EM;
  entry->family = face->family_name != nullptr ? face->family_name : "";
  entry->family_folded = fold_ascii(entry->family);
  entry->weight = read_weight(face);
  entry->italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

  // HarfBuzz は自前の OpenType 実装でフォントを読む（A7: hb-ft は使わない）。
  // バイト列の寿命は FontEntry が持つので、blob には解放関数を渡さない。
  hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(bytes->data()),
                                   static_cast<unsigned int>(bytes->size()),
                                   HB_MEMORY_MODE_READONLY, nullptr, nullptr);
  entry->hb_face = hb_face_create(blob, static_cast<unsigned int>(face_index));
  hb_blob_destroy(blob);
  if (hb_face_get_glyph_count(entry->hb_face) == 0) {
    return fail(ErrorKind::FontLoad,
                "HarfBuzz がフォントを解釈できません (face " + std::to_string(face_index) + ")");
  }

  entry->hb_font = hb_font_create(entry->hb_face);
  hb_ot_font_set_funcs(entry->hb_font);  // メトリクスは hb-ot から読む（A7）

  return entry;
}

}  // namespace

namespace detail {

FontEntry::~FontEntry() {
  if (hb_font != nullptr) {
    hb_font_destroy(hb_font);
  }
  if (hb_face != nullptr) {
    hb_face_destroy(hb_face);
  }
  if (ft_face != nullptr) {
    FT_Done_Face(ft_face);
  }
}

FontStoreImpl::FontStoreImpl() {
  if (FT_Init_FreeType(&library) != 0) {
    library = nullptr;
  }
}

FontStoreImpl::~FontStoreImpl() {
  fonts.clear();  // face は library より先に解放する
  if (library != nullptr) {
    FT_Done_FreeType(library);
  }
}

const FontStoreImpl& FontStoreAccess::impl(const FontStore& store) noexcept { return *store.impl_; }

}  // namespace detail

FontStore::FontStore() : impl_(std::make_unique<FontStoreImpl>()) {}
FontStore::~FontStore() = default;
FontStore::FontStore(FontStore&&) noexcept = default;
FontStore& FontStore::operator=(FontStore&&) noexcept = default;

Result<FontId> FontStore::load(std::span<const std::uint8_t> bytes) {
  if (impl_->library == nullptr) {
    return fail(ErrorKind::Internal, "FreeType を初期化できませんでした");
  }
  if (bytes.empty()) {
    return fail(ErrorKind::FontLoad, "フォントのバイト列が空です");
  }

  auto data = std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end());

  // TTC / OTC は全 face を読み込む。どれか 1 つでも読めなければ 1 つも追加しない
  // （半端に登録された FontId が残らないようにする）。
  std::vector<std::unique_ptr<FontEntry>> loaded;
  FT_Long face_count = 1;
  for (FT_Long index = 0; index < face_count; ++index) {
    auto entry = make_entry(impl_->library, data, index);
    if (!entry) {
      return std::unexpected(entry.error());
    }
    if (index == 0) {
      face_count = (*entry)->ft_face->num_faces;
      if (face_count < 1) {
        face_count = 1;
      }
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
  hb_codepoint_t glyph = 0;
  if (hb_font_get_nominal_glyph(entry->hb_font, cp, &glyph) == 0) {
    return 0;
  }
  return static_cast<GlyphId>(glyph);
}

}  // namespace shashoku::text
