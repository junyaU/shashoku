#pragma once

#include <cstddef>
#include <memory>

#include "core/ids.hpp"
#include "core/result.hpp"
#include "raster/glyph_source.hpp"

namespace shashoku::text {

class FontStore;

namespace detail {
struct GlyphRuntime;  // FreeType のハンドルとグリフキャッシュを隠す（このヘッダに漏らさない）
}  // namespace detail

// ⑤b ラスタライザに注入するグリフ供給元（ARCHITECTURE.md §3.3 / §3.5）。
//
// **1 回の render（= 1 スレッド）専用の実行コンテキスト**（A34）。FontStore に残るのは
// 何本の render() から同時に読んでもよい共有資源だけで、FreeType の可変な状態
// （`FT_Library` / `FT_Face` / グリフスロット）はこのオブジェクトが自分で持つ。
// FreeType は「`FT_Face` は同時に 1 スレッドからしか使えない。同じ `FT_Library` に対する
// face の生成・破棄も同時に行えない」と定めているので、共有せず実行ごとに作り直す
// （3 本で 0.16 ms ≒ render の 0.26%）。FontStore を参照するだけで所有しないので、
// FontStore はこれより長生きすること。
//
// ヒンティングなし・埋め込みビットマップなし（A8）。同じ引数には必ず同じ被覆率を返す。
// 同じ (FontId, glyph_id, pixel_size, sideways) の結果は**この実行の中だけ**メモする。
// キャッシュは純粋な写像のメモ化なので、あってもなくても出力は 1 ビットも変わらない。
class FreeTypeGlyphSource final : public raster::GlyphSource {
 public:
  explicit FreeTypeGlyphSource(const FontStore& fonts);
  // グリフキャッシュの容量（バイト）を指定する。**テスト用**（0 なら一切メモしない）。
  // 容量は出力に影響しないので RenderLimits には入れない（A25 とは別物。A34）。
  FreeTypeGlyphSource(const FontStore& fonts, std::size_t cache_capacity_bytes);
  ~FreeTypeGlyphSource() override;
  FreeTypeGlyphSource(const FreeTypeGlyphSource&) = delete;
  FreeTypeGlyphSource& operator=(const FreeTypeGlyphSource&) = delete;
  FreeTypeGlyphSource(FreeTypeGlyphSource&&) = delete;
  FreeTypeGlyphSource& operator=(FreeTypeGlyphSource&&) = delete;

  // 成功して空のビットマップを返すのは空白グリフ（描くものがない）だけ。
  // 不正な FontId・契約違反の pixel_size は Internal、FreeType の失敗・輪郭を持たない
  // グリフ・未対応の pixel_mode は FontLoad（glyph_source.hpp の契約。issue #3）。
  Result<raster::GlyphBitmap> rasterize(FontId font, GlyphId glyph_id, float pixel_size,
                                        bool sideways) override;

  // キャッシュが効いていることをテストで確かめるためだけの統計。出力には影響しない。
  struct CacheStats {
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::size_t entries = 0;
    std::size_t bytes = 0;
  };
  [[nodiscard]] CacheStats cache_stats() const noexcept;

 private:
  std::unique_ptr<detail::GlyphRuntime> impl_;
};

}  // namespace shashoku::text
