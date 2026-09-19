shashoku_add_test(raster_test
  SOURCES
    raster_test_util.cpp
    blend_test.cpp
    shape_test.cpp
    glyph_test.cpp
    image_test.cpp
    determinism_test.cpp
  LIBS shashoku::raster)
