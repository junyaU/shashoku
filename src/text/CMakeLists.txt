# ④ FontStore・フォールバック・シェーピング・グリフのラスタライズ（ARCHITECTURE.md §3.5）。
# FreeType と HarfBuzz は PRIVATE_DEPS: 公開ヘッダに両者の型を漏らさないので、
# text をリンクする側（layout / raster / api）にはヘッダが見えない（A7）。
shashoku_add_module(text
  SOURCES
    char_properties.cpp
    font_store.cpp
    shaper.cpp
    freetype_glyph_source.cpp
  DEPS shashoku::core
  PRIVATE_DEPS Freetype::Freetype harfbuzz::harfbuzz)
