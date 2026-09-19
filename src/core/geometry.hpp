#pragma once

namespace shashoku {

// 物理座標系: 原点は左上、+x は右、+y は下。単位は CSS px（ラスタライズ時に scale を掛ける）。

struct Point {
  float x = 0;
  float y = 0;

  bool operator==(const Point&) const = default;
};

struct Size {
  float width = 0;
  float height = 0;

  bool operator==(const Size&) const = default;
};

struct Rect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  [[nodiscard]] float right() const { return x + width; }
  [[nodiscard]] float bottom() const { return y + height; }
  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }

  bool operator==(const Rect&) const = default;
};

// 上右下左の 4 辺（CSS の margin / padding の並び順）
template <class T>
struct Edges {
  T top{};
  T right{};
  T bottom{};
  T left{};

  bool operator==(const Edges&) const = default;
};

}  // namespace shashoku
