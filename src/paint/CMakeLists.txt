# ⑤a ボックスツリー → ディスプレイリスト（ARCHITECTURE.md §3.9）。
# 論理 → 物理の座標変換（A1）を持つのはこのモジュールだけ。
# raster は display_list.hpp（ヘッダだけで完結する契約）を読むのでリンクしない。
shashoku_add_module(paint
  SOURCES
    display_list_builder.cpp
    dump_json.cpp
    dump_svg.cpp
  DEPS shashoku::core shashoku::layout)
