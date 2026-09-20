#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shashoku {

// --dump-stage 用の最小 JSON ライタ（DESIGN.md §3-3）。読み取りはしない。
//
// 出力は決定的であること: キーは呼び出し順のまま、インデントは 2 スペース、
// ロケールに依存しない。
//
// float は「元の値に戻せる最短表現」（std::to_chars）で書く。ダンプは人が座標を
// 読むためのものなので、絶対値が 1e-6 以上 1e15 未満なら指数表記にせず固定小数点で
// 書く（100000.0 → "100000"、1200.5 → "1200.5"）。整数値は "12" のように小数点なし。
// この範囲を外れる有限値だけ指数表記になりうる（1e-7 → "1e-07"）。
// -0.0 は "0"、NaN / Inf は null。
//
//   JsonWriter w;
//   w.begin_object();
//   w.key("tag").value("div");
//   w.key("rect").begin_array().value(0.0f).value(12.5f).end_array();
//   w.end_object();
//   std::string json = std::move(w).str();
class JsonWriter {
 public:
  JsonWriter& begin_object();
  JsonWriter& end_object();
  JsonWriter& begin_array();
  JsonWriter& end_array();
  JsonWriter& key(std::string_view name);

  // UTF-8。制御文字・" ・\ をエスケープする（非 ASCII はそのまま出す）
  JsonWriter& value(std::string_view s);
  JsonWriter& value(const char* s);
  JsonWriter& value(float f);
  JsonWriter& value(double f);
  JsonWriter& value(std::int64_t i);
  JsonWriter& value(std::uint64_t i);
  JsonWriter& value(int i);
  JsonWriter& value(unsigned i);
  JsonWriter& value(bool b);
  JsonWriter& null();

  [[nodiscard]] std::string str() &&;
  [[nodiscard]] const std::string& str() const&;

 private:
  enum class Scope : std::uint8_t { Object, Array };
  struct Frame {
    Scope scope = Scope::Object;
    bool has_items = false;
  };

  void before_value();
  void newline_indent();

  std::string out_;
  std::vector<Frame> stack_;
  bool after_key_ = false;
};

}  // namespace shashoku
