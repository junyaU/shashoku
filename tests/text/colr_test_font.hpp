#pragma once

// テスト用の最小の COLR / CPAL カラーフォント（704 バイト）を**バイト列で組み立てる**。
// リポジトリにバイナリを置かずに「色データだけを持つグリフ」を再現するためのもの
// （issue #27。ネットワークもダウンロードも要らない）。
//
// グリフ構成（4 個）:
//   gid 0 = .notdef（塗られた長方形。豆腐を描くのに使われる）
//   gid 1 = 'A'（U+0041）の COLR ベース。ColrBase::Empty なら**輪郭が空**、
//           ColrBase::Outlined ならベース自身が輪郭を持つ（Segoe UI Emoji の合成ベース相当）
//   gid 2, 3 = 色レイヤー用のグリフ（cmap からは引けない。輪郭を持つ）
// COLR v0 は gid 1 に「gid 2（パレット 0）→ gid 3（パレット 1）」の 2 枚のレイヤーを割り当てる。
// cmap にあるのは 'A' だけなので、'B' は従来どおりの豆腐（cmap に無い文字）の対照になる。
//
// sfnt のテーブルを素直に並べただけで、ヒンティングも name 文字列も持たない。
// FreeType も HarfBuzz もこれを輪郭フォントとして読める（FT_IS_SCALABLE が真）。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"

namespace shashoku::text::assets {

// COLR のベースグリフ（'A'）の作り方。
enum class ColrBase : std::uint8_t {
  Empty,  // 輪郭が空。色レイヤーだけで絵を作る作り（nanoemoji 系の COLR フォント）
  Outlined,  // ベース自身が輪郭を持つ（単色でも「色の落ちた絵」として描ける作り）
};

// このフォントの中身を知っているテストのための定数。
inline constexpr char32_t kColrCodepoint = U'A';         // cmap にある文字
inline constexpr char32_t kColrMissingCodepoint = U'B';  // cmap に無い（従来の豆腐の対照）
inline constexpr char32_t kColrTofuCodepoint = U'□';  // U+25A1。豆腐に使われるグリフ
inline constexpr GlyphId kColrNotdefGlyph = 0;
inline constexpr GlyphId kColrBaseGlyph = 1;  // 'A'。COLR のベース
inline constexpr GlyphId kColrFirstLayerGlyph = 2;  // 色レイヤー（輪郭あり・色データ無し）
inline constexpr GlyphId kColrSecondLayerGlyph = 3;
inline constexpr std::uint16_t kColrUnitsPerEm = 1000;
// ベースの輪郭が空のときのバイト数（issue #27 の再現に使った 704 バイトのフォント）。
inline constexpr std::size_t kColrFontSize = 704;

namespace detail {

inline void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

inline void put_i16(std::vector<std::uint8_t>& out, int value) {
  put_u16(out, static_cast<std::uint16_t>(value));
}

inline void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u16(out, value >> 16);
  put_u16(out, value & 0xFFFFU);
}

inline void put_tag(std::vector<std::uint8_t>& out, std::string_view tag) {
  for (const char c : tag) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
}

// 軸に平行な長方形 1 本ぶんの単純グリフ（glyf）。4 点ともオンカーブ・差分は int16。
inline std::vector<std::uint8_t> box_glyph(int x0, int y0, int x1, int y1) {
  const std::array<int, 4> xs{x0, x1, x1, x0};
  const std::array<int, 4> ys{y0, y0, y1, y1};
  std::vector<std::uint8_t> glyph;
  put_i16(glyph, 1);  // numberOfContours。1 本なら単純グリフ
  put_i16(glyph, x0);
  put_i16(glyph, y0);
  put_i16(glyph, x1);
  put_i16(glyph, y1);  // xMin yMin xMax yMax
  put_u16(glyph, 3);   // endPtsOfContours[0]: 輪郭の最後の点の番号
  put_u16(glyph, 0);   // instructionLength（ヒンティング命令は持たない）
  glyph.insert(glyph.end(), 4, 0x01);  // flags: 4 点とも ON_CURVE、x/y は int16 の差分
  int previous = 0;
  for (const int x : xs) {
    put_i16(glyph, x - previous);
    previous = x;
  }
  previous = 0;
  for (const int y : ys) {
    put_i16(glyph, y - previous);
    previous = y;
  }
  return glyph;
}

// sfnt のテーブルチェックサム（4 バイト単位の和。足りない分は 0 で埋める）。
inline std::uint32_t table_checksum(const std::vector<std::uint8_t>& data) {
  std::uint32_t sum = 0;
  for (std::size_t i = 0; i < data.size(); i += 4) {
    std::uint32_t word = 0;
    for (std::size_t j = 0; j < 4; ++j) {
      word = (word << 8) | (i + j < data.size() ? data[i + j] : 0U);
    }
    sum += word;  // 32 ビットで巻き戻るのが仕様
  }
  return sum;
}

struct SfntTable {
  std::string_view tag;
  std::vector<std::uint8_t> data;
};

}  // namespace detail

// 704 バイトの COLR / CPAL フォントを組み立てる。
// map_tofu_to_base を立てると、□（U+25A1）も同じ（色データだけの）ベースに割り当てる。
inline std::vector<std::uint8_t> build_colr_font(ColrBase base, bool map_tofu_to_base = false) {
  using detail::box_glyph;
  using detail::put_i16;
  using detail::put_tag;
  using detail::put_u16;
  using detail::put_u32;
  using detail::SfntTable;

  // --- glyf / loca: グリフの輪郭とその位置 --------------------------------------
  std::vector<std::vector<std::uint8_t>> glyphs;
  glyphs.push_back(box_glyph(50, 0, 950, 700));  // gid 0 .notdef
  glyphs.push_back(base == ColrBase::Empty       // gid 1 'A'（COLR のベース）
                       ? std::vector<std::uint8_t>{}
                       : box_glyph(120, 20, 880, 680));
  glyphs.push_back(box_glyph(100, 0, 900, 700));    // gid 2 色レイヤー
  glyphs.push_back(box_glyph(250, 150, 750, 550));  // gid 3 色レイヤー
  const auto glyph_count = static_cast<std::uint16_t>(glyphs.size());

  std::vector<std::uint8_t> glyf;
  std::vector<std::uint8_t> loca;
  put_u32(loca, 0);  // loca は long 形式（head の indexToLocFormat = 1）
  for (const std::vector<std::uint8_t>& glyph : glyphs) {
    glyf.insert(glyf.end(), glyph.begin(), glyph.end());
    glyf.resize(glyf.size() + ((4 - (glyf.size() % 4)) % 4), 0);  // 4 バイト境界にそろえる
    put_u32(loca, static_cast<std::uint32_t>(glyf.size()));
  }

  // --- head: 単位・座標の範囲・loca の形式 ---------------------------------------
  std::vector<std::uint8_t> head;
  put_u16(head, 1);           // majorVersion
  put_u16(head, 0);           // minorVersion
  put_u32(head, 0x00010000);  // fontRevision
  put_u32(head, 0);           // checkSumAdjustment（FreeType も HarfBuzz も見ない）
  put_u32(head, 0x5F0F3CF5);  // magicNumber
  put_u16(head, 0x000B);      // flags
  put_u16(head, kColrUnitsPerEm);
  put_u32(head, 0);  // created（上位 32 ビット）
  put_u32(head, 0);  // created（下位 32 ビット）
  put_u32(head, 0);  // modified
  put_u32(head, 0);
  put_i16(head, 0);     // xMin
  put_i16(head, 0);     // yMin
  put_i16(head, 1000);  // xMax
  put_i16(head, 700);   // yMax
  put_u16(head, 0);     // macStyle
  put_u16(head, 8);     // lowestRecPPEM
  put_i16(head, 2);     // fontDirectionHint
  put_i16(head, 1);     // indexToLocFormat: 1 = long
  put_i16(head, 0);     // glyphDataFormat

  // --- hhea / hmtx: 横組みのメトリクス -------------------------------------------
  std::vector<std::uint8_t> hhea;
  put_u16(hhea, 1);     // majorVersion
  put_u16(hhea, 0);     // minorVersion
  put_i16(hhea, 800);   // ascender
  put_i16(hhea, -200);  // descender
  put_i16(hhea, 0);     // lineGap
  put_u16(hhea, 1000);  // advanceWidthMax
  put_i16(hhea, 0);     // minLeftSideBearing
  put_i16(hhea, 0);     // minRightSideBearing
  put_i16(hhea, 1000);  // xMaxExtent
  put_i16(hhea, 1);     // caretSlopeRise
  put_i16(hhea, 0);     // caretSlopeRun
  put_i16(hhea, 0);     // caretOffset
  for (int i = 0; i < 4; ++i) {
    put_i16(hhea, 0);  // reserved
  }
  put_i16(hhea, 0);            // metricDataFormat
  put_u16(hhea, glyph_count);  // numberOfHMetrics

  std::vector<std::uint8_t> hmtx;
  for (std::uint16_t i = 0; i < glyph_count; ++i) {
    put_u16(hmtx, 1000);  // advanceWidth: 全グリフ 1em
    put_i16(hmtx, 0);     // leftSideBearing
  }

  // --- maxp: グリフ数と輪郭の上限 ------------------------------------------------
  std::vector<std::uint8_t> maxp;
  put_u32(maxp, 0x00010000);  // version 1.0（glyf を持つフォント）
  put_u16(maxp, glyph_count);
  put_u16(maxp, 4);  // maxPoints
  put_u16(maxp, 1);  // maxContours
  put_u16(maxp, 0);  // maxCompositePoints
  put_u16(maxp, 0);  // maxCompositeContours
  put_u16(maxp, 2);  // maxZones
  for (int i = 0; i < 8; ++i) {
    put_u16(maxp, 0);  // maxTwilightPoints 以降はすべて 0
  }

  // --- post / name: 中身は持たない最小の表 ---------------------------------------
  std::vector<std::uint8_t> post;
  put_u32(post, 0x00030000);  // version 3.0（グリフ名を持たない）
  for (int i = 0; i < 7; ++i) {
    put_u32(post, 0);
  }
  std::vector<std::uint8_t> name;
  put_u16(name, 0);  // version
  put_u16(name, 0);  // count: 名前を 1 つも持たない
  put_u16(name, 6);  // storageOffset

  // --- OS/2: version 4（96 バイト）。weight / 幅の既定値だけ --------------------
  std::vector<std::uint8_t> os2;
  put_u16(os2, 4);               // version
  put_i16(os2, 1000);            // xAvgCharWidth
  put_u16(os2, 400);             // usWeightClass
  put_u16(os2, 5);               // usWidthClass
  put_u16(os2, 0);               // fsType
  put_i16(os2, 650);             // ySubscriptXSize
  put_i16(os2, 700);             // ySubscriptYSize
  put_i16(os2, 0);               // ySubscriptXOffset
  put_i16(os2, 140);             // ySubscriptYOffset
  put_i16(os2, 650);             // ySuperscriptXSize
  put_i16(os2, 700);             // ySuperscriptYSize
  put_i16(os2, 0);               // ySuperscriptXOffset
  put_i16(os2, 480);             // ySuperscriptYOffset
  put_i16(os2, 50);              // yStrikeoutSize
  put_i16(os2, 250);             // yStrikeoutPosition
  put_i16(os2, 0);               // sFamilyClass
  os2.insert(os2.end(), 10, 0);  // panose
  put_u32(os2, 1);               // ulUnicodeRange1（Basic Latin）
  put_u32(os2, 0);
  put_u32(os2, 0);
  put_u32(os2, 0);
  put_tag(os2, "TEST");  // achVendID
  put_u16(os2, 0x0040);  // fsSelection: REGULAR
  put_u16(os2, 0x0041);  // usFirstCharIndex
  put_u16(os2, 0x0041);  // usLastCharIndex
  put_i16(os2, 800);     // sTypoAscender
  put_i16(os2, -200);    // sTypoDescender
  put_i16(os2, 0);       // sTypoLineGap
  put_u16(os2, 800);     // usWinAscent
  put_u16(os2, 200);     // usWinDescent
  put_u32(os2, 1);       // ulCodePageRange1（Latin 1）
  put_u32(os2, 0);
  put_i16(os2, 500);   // sxHeight
  put_i16(os2, 700);   // sCapHeight
  put_u16(os2, 0);     // usDefaultChar
  put_u16(os2, 0x20);  // usBreakChar
  put_u16(os2, 0);     // usMaxContext

  // --- cmap: format 4 のサブテーブル 1 つ ----------------------------------------
  // 1 文字 = 1 区間（[cp, cp]）に、番兵の [0xFFFF, 0xFFFF] を足したもの。
  // gid は idDelta で作る（idRangeOffset は全区間 0）。
  std::vector<std::pair<char32_t, GlyphId>> mappings{{kColrCodepoint, kColrBaseGlyph}};
  if (map_tofu_to_base) {
    // □（U+25A1）も色データだけのベースに割り当てる。豆腐を描くグリフ自体が
    // 「色でしか描けない」ときの経路を作るための変種。
    mappings.emplace_back(kColrTofuCodepoint, kColrBaseGlyph);
  }
  const auto segment_count = static_cast<std::uint16_t>(mappings.size() + 1);  // + 番兵
  // searchRange / entrySelector / rangeShift は二分探索の補助（OpenType の定義どおり）。
  std::uint16_t power = 1;  // = 2^entry_selector ≦ segment_count
  std::uint16_t entry_selector = 0;
  while (power * 2 <= segment_count) {
    power = static_cast<std::uint16_t>(power * 2);
    ++entry_selector;
  }
  std::vector<std::uint8_t> sub;
  put_u16(sub, 4);                                    // format
  put_u16(sub, 16U + (8U * segment_count));           // length
  put_u16(sub, 0);                                    // language
  put_u16(sub, 2U * segment_count);                   // segCountX2
  put_u16(sub, 2U * power);                           // searchRange
  put_u16(sub, entry_selector);                       // entrySelector
  put_u16(sub, (2U * segment_count) - (2U * power));  // rangeShift
  for (const auto& [cp, glyph] : mappings) {
    put_u16(sub, static_cast<std::uint32_t>(cp));  // endCode[]
  }
  put_u16(sub, 0xFFFF);  // endCode[] 番兵
  put_u16(sub, 0);       // reservedPad
  for (const auto& [cp, glyph] : mappings) {
    put_u16(sub, static_cast<std::uint32_t>(cp));  // startCode[]
  }
  put_u16(sub, 0xFFFF);  // startCode[] 番兵
  for (const auto& [cp, glyph] : mappings) {
    put_u16(sub, static_cast<std::uint16_t>(glyph - static_cast<int>(cp)));  // idDelta[]
  }
  put_u16(sub, 1);  // idDelta[] 番兵（0xFFFF + 1 = 0 = .notdef）
  for (std::uint16_t i = 0; i < segment_count; ++i) {
    put_u16(sub, 0);  // idRangeOffset[]
  }

  std::vector<std::uint8_t> cmap;
  put_u16(cmap, 0);   // version
  put_u16(cmap, 1);   // numTables
  put_u16(cmap, 3);   // platformID: Windows
  put_u16(cmap, 1);   // encodingID: Unicode BMP
  put_u32(cmap, 12);  // subtableOffset
  cmap.insert(cmap.end(), sub.begin(), sub.end());

  // --- COLR v0: ベース gid 1 に色レイヤー 2 枚 -----------------------------------
  std::vector<std::uint8_t> colr;
  put_u16(colr, 0);                      // version 0
  put_u16(colr, 1);                      // numBaseGlyphRecords
  put_u32(colr, 14);                     // baseGlyphRecordsOffset
  put_u32(colr, 20);                     // layerRecordsOffset
  put_u16(colr, 2);                      // numLayerRecords
  put_u16(colr, kColrBaseGlyph);         // BaseGlyphRecord: glyphID
  put_u16(colr, 0);                      // firstLayerIndex
  put_u16(colr, 2);                      // numLayers
  put_u16(colr, kColrFirstLayerGlyph);   // LayerRecord[0]: glyphID
  put_u16(colr, 0);                      // paletteIndex
  put_u16(colr, kColrSecondLayerGlyph);  // LayerRecord[1]
  put_u16(colr, 1);

  // --- CPAL v0: 色 2 つのパレット 1 つ -------------------------------------------
  std::vector<std::uint8_t> cpal;
  put_u16(cpal, 0);                                   // version 0
  put_u16(cpal, 2);                                   // numPaletteEntries
  put_u16(cpal, 1);                                   // numPalettes
  put_u16(cpal, 2);                                   // numColorRecords
  put_u32(cpal, 14);                                  // colorRecordsArrayOffset
  put_u16(cpal, 0);                                   // colorRecordIndices[0]
  cpal.insert(cpal.end(), {0x20, 0x40, 0xE0, 0xFF});  // ColorRecord[0] BGRA: 青
  cpal.insert(cpal.end(), {0xE0, 0xC0, 0x20, 0xFF});  // ColorRecord[1] BGRA: 黄

  // --- sfnt の組み立て。テーブルディレクトリはタグの昇順 -------------------------
  const std::vector<SfntTable> tables{
      {"COLR", std::move(colr)}, {"CPAL", std::move(cpal)}, {"OS/2", std::move(os2)},
      {"cmap", std::move(cmap)}, {"glyf", std::move(glyf)}, {"head", std::move(head)},
      {"hhea", std::move(hhea)}, {"hmtx", std::move(hmtx)}, {"loca", std::move(loca)},
      {"maxp", std::move(maxp)}, {"name", std::move(name)}, {"post", std::move(post)}};
  const auto table_count = static_cast<std::uint16_t>(tables.size());

  std::vector<std::uint8_t> font;
  put_u32(font, 0x00010000);  // sfntVersion: TrueType の輪郭
  put_u16(font, table_count);
  put_u16(font, 128);  // searchRange = 16 * 2^floor(log2(12))
  put_u16(font, 3);    // entrySelector
  put_u16(font, 64);   // rangeShift = 16 * 12 - searchRange

  std::vector<std::uint8_t> body;
  auto offset = static_cast<std::uint32_t>(12 + (16 * table_count));
  for (const SfntTable& table : tables) {
    put_tag(font, table.tag);
    put_u32(font, detail::table_checksum(table.data));
    put_u32(font, offset);
    put_u32(font, static_cast<std::uint32_t>(table.data.size()));
    body.insert(body.end(), table.data.begin(), table.data.end());
    const std::size_t padded = (4 - (table.data.size() % 4)) % 4;
    body.resize(body.size() + padded, 0);
    offset += static_cast<std::uint32_t>(table.data.size() + padded);
  }
  font.insert(font.end(), body.begin(), body.end());
  return font;
}

// ベースの輪郭が空の COLR フォント（issue #27 の再現に使った 704 バイトのもの）。
inline const std::vector<std::uint8_t>& colr_font_empty_base() {
  static const std::vector<std::uint8_t> bytes = build_colr_font(ColrBase::Empty);
  return bytes;
}

// ベースが輪郭を持つ COLR フォント（Segoe UI Emoji のような作り）。単色で描ける。
inline const std::vector<std::uint8_t>& colr_font_outlined_base() {
  static const std::vector<std::uint8_t> bytes = build_colr_font(ColrBase::Outlined);
  return bytes;
}

// □（U+25A1）まで色データだけのベースに割り当てたフォント。豆腐を描くグリフ自体が
// 「色でしか描けない」場合に、空白の豆腐を選んでしまわないかを見るための変種。
inline const std::vector<std::uint8_t>& colr_font_with_color_only_tofu() {
  static const std::vector<std::uint8_t> bytes = build_colr_font(ColrBase::Empty, true);
  return bytes;
}

}  // namespace shashoku::text::assets
