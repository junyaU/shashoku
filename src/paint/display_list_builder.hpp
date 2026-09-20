#pragma once

#include <string>

#include "layout/box_tree.hpp"
#include "raster/display_list.hpp"

// ⑤a ペイント（ARCHITECTURE.md §3.9）: ボックスツリー → ディスプレイリスト。
//
// 木を前順に辿って平らな命令列に潰すだけの段。ここでしかやらない仕事が 1 つある:
// **論理座標 → 物理座標の変換（A1）**。レイアウトは inline / block の論理座標で組み、
// 縦書きかどうかを知っているのはこの段だけ。変換は display_list_builder.cpp の
// `Axes` クラスに閉じていて、shashoku の中でここ以外に論理 → 物理の式は存在しない。
namespace shashoku::paint {

// 描画順: ブロックは「背景 → 枠線 → 子」、行は fragments の順。
// 何も描かない命令は出さない（完全に透明な塗り、幅 0 / 透明な枠線、グリフのない断片）。
// 同じ font / size / color / sideways が 1 行の中で連続する TextFragment は
// 1 つの DrawGlyphs にまとめる。
raster::DisplayList build_display_list(const layout::BoxTree& tree);

// --dump-stage=display-list の出力。キー順は固定（display_list.hpp の型の並び順）。
// 既定値のキーは省く（sideways = false）。
std::string dump_json(const raster::DisplayList& list);

// --dump-stage=svg の出力（DESIGN.md §2: SVG はデバッグダンプに格下げ）。
// グリフは輪郭を持たないので四角い印で代用する。壊れた入力（PushClip / PopClip の
// 対応が取れていない列）でも整形式の SVG を返す。
std::string dump_svg(const raster::DisplayList& list, float width, float height);

}  // namespace shashoku::paint
