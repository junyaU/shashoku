# ⑥ PNG エンコード / デコード（ARCHITECTURE.md §3.2）。
# zlib は実装だけが使う（ヘッダに漏らさない）ので PRIVATE_DEPS。
shashoku_add_module(png
  SOURCES
    encoder.cpp
    decoder.cpp
  DEPS shashoku::core
  PRIVATE_DEPS ZLIB::ZLIBSTATIC)
