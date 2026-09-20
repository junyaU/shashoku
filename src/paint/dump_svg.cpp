#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "paint/display_list_builder.hpp"
#include "raster/display_list.hpp"

// --dump-stage=svg（DESIGN.md §2「SVG 主出力はデバッグダンプに格下げ」）。
//
// ディスプレイリストを人の目で確認するための出力で、PNG の代わりにはならない:
// グリフは輪郭を持たない（ここからフォントは見えない）ので、em 四方の印で代用する。
// 壊れた命令列（PushClip / PopClip の対応が取れていない、NaN 混じり）でも
// 必ず整形式の XML を返す。
namespace shashoku::paint {
namespace {

// SVG の数値として常に妥当な形（指数表記なし・ロケール非依存）で書く。
// 小数 3 桁で丸め、末尾の 0 と小数点を落とす。非有限値は 0 にする。
std::string number(float value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  std::string out = std::format("{:.3f}", value);
  const std::size_t dot = out.find('.');
  if (dot != std::string::npos) {
    out.erase(out.find_last_not_of('0') + 1);
    if (!out.empty() && out.back() == '.') {
      out.pop_back();
    }
  }
  if (out.empty() || out == "-0") {
    out = "0";
  }
  return out;
}

std::string rgb_hex(const Color& color) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out = "#";
  for (const std::uint8_t component : {color.r, color.g, color.b}) {
    out.push_back(kDigits[component >> 4U]);
    out.push_back(kDigits[component & 0x0FU]);
  }
  return out;
}

std::string opacity(const Color& color) { return number(static_cast<float>(color.a) / 255.0F); }

// XML のテキスト / 属性値に使えない 5 文字を実体参照にする。
std::string escaped(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::string rect_attrs(const Rect& rect) {
  return std::format(R"(x="{}" y="{}" width="{}" height="{}")", number(rect.x), number(rect.y),
                     number(std::max(0.0F, rect.width)), number(std::max(0.0F, rect.height)));
}

std::string radius_attrs(float radius) {
  if (!(radius > 0)) {
    return {};
  }
  return std::format(R"( rx="{}" ry="{}")", number(radius), number(radius));
}

class SvgWriter {
 public:
  SvgWriter(float width, float height) {
    out_ += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    const std::string w = number(std::max(0.0F, width));
    const std::string h = number(std::max(0.0F, height));
    out_ += std::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{}\" height=\"{}\" "
        "viewBox=\"0 0 {} {}\">\n",
        w, h, w, h);
    out_ += "  <!-- shashoku display list dump: グリフは em 四方の印で代用している -->\n";
  }

  void write(const raster::FillRect& cmd) {
    line(std::format(R"(<rect {} fill="{}" fill-opacity="{}"/>)", rect_attrs(cmd.rect),
                     rgb_hex(cmd.color), opacity(cmd.color)));
  }

  void write(const raster::FillRoundedRect& cmd) {
    line(std::format(R"(<rect {}{} fill="{}" fill-opacity="{}"/>)", rect_attrs(cmd.rect),
                     radius_attrs(cmd.radius), rgb_hex(cmd.color), opacity(cmd.color)));
  }

  // SVG のストロークはパスの両側に半分ずつ乗るので、内側に width/2 だけ縮めた矩形を描く。
  void write(const raster::StrokeRoundedRect& cmd) {
    const float inset = cmd.width / 2;
    const Rect rect{cmd.rect.x + inset, cmd.rect.y + inset, cmd.rect.width - cmd.width,
                    cmd.rect.height - cmd.width};
    line(
        std::format(R"(<rect {}{} fill="none" stroke="{}" stroke-opacity="{}" stroke-width="{}"/>)",
                    rect_attrs(rect), radius_attrs(cmd.radius - inset), rgb_hex(cmd.color),
                    opacity(cmd.color), number(cmd.width)));
  }

  void write(const raster::DrawGlyphs& cmd) {
    line(std::format(R"(<g fill="{}" fill-opacity="{}">)", rgb_hex(cmd.color), opacity(cmd.color)));
    ++indent_;
    line(std::format("<title>{}</title>",
                     escaped(std::format("font {} / {} px{}", cmd.font, number(cmd.size),
                                         cmd.sideways ? " / sideways" : ""))));
    for (const raster::GlyphInstance& glyph : cmd.glyphs) {
      // 横書きの em box は原点から見て x ∈ [0, size]、y ∈ [-0.8 size, 0.2 size]。
      // sideways はそれを原点まわりに時計回りへ 90° 回したもの。
      const Rect box =
          cmd.sideways
              ? Rect{glyph.origin.x - (0.2F * cmd.size), glyph.origin.y, cmd.size, cmd.size}
              : Rect{glyph.origin.x, glyph.origin.y - (0.8F * cmd.size), cmd.size, cmd.size};
      line(std::format("<rect {}/>", rect_attrs(box)));
    }
    --indent_;
    line("</g>");
  }

  void write(const raster::DrawImage& cmd) {
    line(R"(<g stroke="#808080" stroke-width="1" fill="#c0c0c0" fill-opacity="0.5">)");
    ++indent_;
    line(std::format("<title>{}</title>", escaped(std::format("image {}", cmd.image))));
    line(std::format("<rect {}/>", rect_attrs(cmd.dest)));
    line(std::format(R"(<line x1="{}" y1="{}" x2="{}" y2="{}"/>)", number(cmd.dest.x),
                     number(cmd.dest.y), number(cmd.dest.right()), number(cmd.dest.bottom())));
    line(std::format(R"(<line x1="{}" y1="{}" x2="{}" y2="{}"/>)", number(cmd.dest.right()),
                     number(cmd.dest.y), number(cmd.dest.x), number(cmd.dest.bottom())));
    --indent_;
    line("</g>");
  }

  void write(const raster::PushClip& cmd) {
    ++clip_serial_;
    const std::string id = std::format("clip{}", clip_serial_);
    line(std::format(R"(<clipPath id="{}">)", id));
    ++indent_;
    line(std::format("<rect {}{}/>", rect_attrs(cmd.rect), radius_attrs(cmd.radius)));
    --indent_;
    line("</clipPath>");
    line(std::format(R"SVG(<g clip-path="url(#{})">)SVG", id));
    ++indent_;
    ++open_clips_;
  }

  void write(const raster::PopClip& /*cmd*/) {
    // 対応する PushClip がない PopClip は捨てる（整形式を保つ）。
    if (open_clips_ == 0) {
      return;
    }
    --open_clips_;
    --indent_;
    line("</g>");
  }

  std::string finish() && {
    while (open_clips_ > 0) {  // 閉じ忘れの PushClip もここで閉じる
      write(raster::PopClip{});
    }
    out_ += "</svg>\n";
    return std::move(out_);
  }

 private:
  void line(std::string_view text) {
    out_.append(indent_ * 2, ' ');
    out_ += text;
    out_ += '\n';
  }

  std::string out_;
  std::size_t indent_ = 1;
  std::size_t open_clips_ = 0;
  std::size_t clip_serial_ = 0;
};

}  // namespace

std::string dump_svg(const raster::DisplayList& list, float width, float height) {
  SvgWriter writer(width, height);
  for (const raster::DrawCmd& command : list) {
    std::visit([&writer](const auto& cmd) { writer.write(cmd); }, command);
  }
  return std::move(writer).finish();
}

}  // namespace shashoku::paint
