# ③ スタイル付きツリー → ボックスツリー（ARCHITECTURE.md §3.8）。製品の本丸。
# 契約ヘッダは box_tree.hpp（paint が読む）と layout.hpp。
# 依存は core（基本型・UTF-8・JSON）と linebreak（行分割器）だけ。
# style / text はヘッダだけで完結する契約（computed_style.hpp / text_measurer.hpp）なのでリンクしない。
shashoku_add_module(layout
  SOURCES
    east_asian_width.cpp
    inline_layout.cpp
    layout.cpp
    dump_json.cpp
  DEPS shashoku::core shashoku::linebreak)
