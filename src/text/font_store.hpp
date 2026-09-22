#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "core/ids.hpp"
#include "core/result.hpp"

namespace shashoku::text {

class FontStore;

namespace detail {
// FreeType / HarfBuzz のハンドルはここに閉じ込める。このヘッダを include する側
// （api モジュールなど）に FreeType のヘッダは見えない（ARCHITECTURE.md §3.10）。
struct FontStoreImpl;

// text モジュールの内部実装（shaper.cpp / freetype_glyph_source.cpp）だけが
// ハンドルに触れるための窓口。font_store_impl.hpp を include して初めて使える。
struct FontStoreAccess {
  [[nodiscard]] static const FontStoreImpl& impl(const FontStore& store) noexcept;
};
}  // namespace detail

// フォント実体の唯一の所有者（DESIGN.md §3-2）。ツリーやディスプレイリストには
// FontId しか載らず、実体の参照はシェーピングとラスタライズの瞬間だけ行う。
//
// FontId は 0 から順に払い出され、その順がフォールバック順になる（ARCHITECTURE.md §3.5）。
// ムーブ可・コピー不可（フォント実体を 2 箇所に持たせない）。
//
// **共有資源**（A34）: `load()` を呼び終わったあとは完全に読み取り専用で、何本の
// `render()` から同時に参照してもよい。中身はバイト列・HarfBuzz の不変オブジェクト
// （`hb_face_t` / cmap 引き用の `hb_font_t`）・解析済みの family / weight / upem だけで、
// FreeType のハンドルは持たない（`FT_Face` は 1 スレッド専用なので、実行ごとに
// `FreeTypeGlyphSource` が作る）。`load()` と破棄は、その `FontStore` を使っている
// `render()` と同時に行わないこと（利用者の責務）。
class FontStore {
 public:
  FontStore();
  ~FontStore();
  FontStore(FontStore&&) noexcept;
  FontStore& operator=(FontStore&&) noexcept;
  FontStore(const FontStore&) = delete;
  FontStore& operator=(const FontStore&) = delete;

  // バイト列をコピーして保持する（呼び出し側は span の寿命を気にしなくてよい）。
  // TTC / OTC はすべての face を読み込み、先頭 face の FontId を返す。
  // 解釈できないバイト列は ErrorKind::FontLoad。
  Result<FontId> load(std::span<const std::uint8_t> bytes);

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] bool contains(FontId font) const noexcept;

  // 以下の照会は不正な FontId でも落ちない（空文字列や既定値を返す）。
  [[nodiscard]] std::string_view family(FontId font) const noexcept;
  [[nodiscard]] int weight(FontId font) const noexcept;  // OS/2 usWeightClass。無ければ 400
  [[nodiscard]] bool is_italic(FontId font) const noexcept;
  [[nodiscard]] std::uint16_t units_per_em(FontId font) const noexcept;

  // cmap 引き。グリフを持たなければ 0（.notdef）。
  [[nodiscard]] GlyphId glyph_for(FontId font, char32_t cp) const noexcept;
  [[nodiscard]] bool has_glyph(FontId font, char32_t cp) const noexcept {
    return glyph_for(font, cp) != 0;
  }

  // 色データ（COLR）だけを持ち、単色の輪郭が空のグリフか（A43 / issue #27）。
  // shashoku は単色の輪郭しか描かないので、これは「そのフォントでは描けないグリフ」であり、
  // 豆腐に回す。判定は load() のときに済ませてある（FontStore は読み取り専用の共有資源。A34）。
  [[nodiscard]] bool is_color_only_glyph(FontId font, GlyphId glyph) const noexcept;

  // その文字を**単色で描ける**か（cmap にあり、かつ色データだけのグリフでない）。
  // フォールバック列を辿るときは has_glyph ではなくこちらで判定する。
  [[nodiscard]] bool has_drawable_glyph(FontId font, char32_t cp) const noexcept {
    const GlyphId glyph = glyph_for(font, cp);
    return glyph != 0 && !is_color_only_glyph(font, glyph);
  }

  // load() のときに「色データだけのグリフか」を調べたグリフの数。**テスト用の統計**で、
  // 出力には影響しない。色データを持たないフォント（COLR / COLRv1 が無い）では 0 になる
  // ＝ グリフごとの判定に一切入らないことを、テストがこの値で固定する。
  [[nodiscard]] std::size_t color_probe_count(FontId font) const noexcept;

 private:
  friend struct detail::FontStoreAccess;

  std::unique_ptr<detail::FontStoreImpl> impl_;
};

}  // namespace shashoku::text
