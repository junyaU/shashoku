#include "style/css_color.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/color.hpp"
#include "style/css_chars.hpp"
#include "style/css_tokens.hpp"
#include "style/declaration.hpp"

namespace shashoku::style {
namespace {

struct NamedColor {
  std::string_view name;
  std::uint32_t rgb;  // 0xRRGGBB
};

// CSS Color 4 §6.1 の名前つき色 148 個 + transparent。名前の昇順（二分探索する）。
constexpr std::array<NamedColor, 148> kNamedColors = {{
    {"aliceblue", 0xF0F8FF},
    {"antiquewhite", 0xFAEBD7},
    {"aqua", 0x00FFFF},
    {"aquamarine", 0x7FFFD4},
    {"azure", 0xF0FFFF},
    {"beige", 0xF5F5DC},
    {"bisque", 0xFFE4C4},
    {"black", 0x000000},
    {"blanchedalmond", 0xFFEBCD},
    {"blue", 0x0000FF},
    {"blueviolet", 0x8A2BE2},
    {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887},
    {"cadetblue", 0x5F9EA0},
    {"chartreuse", 0x7FFF00},
    {"chocolate", 0xD2691E},
    {"coral", 0xFF7F50},
    {"cornflowerblue", 0x6495ED},
    {"cornsilk", 0xFFF8DC},
    {"crimson", 0xDC143C},
    {"cyan", 0x00FFFF},
    {"darkblue", 0x00008B},
    {"darkcyan", 0x008B8B},
    {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9},
    {"darkgreen", 0x006400},
    {"darkgrey", 0xA9A9A9},
    {"darkkhaki", 0xBDB76B},
    {"darkmagenta", 0x8B008B},
    {"darkolivegreen", 0x556B2F},
    {"darkorange", 0xFF8C00},
    {"darkorchid", 0x9932CC},
    {"darkred", 0x8B0000},
    {"darksalmon", 0xE9967A},
    {"darkseagreen", 0x8FBC8F},
    {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F},
    {"darkslategrey", 0x2F4F4F},
    {"darkturquoise", 0x00CED1},
    {"darkviolet", 0x9400D3},
    {"deeppink", 0xFF1493},
    {"deepskyblue", 0x00BFFF},
    {"dimgray", 0x696969},
    {"dimgrey", 0x696969},
    {"dodgerblue", 0x1E90FF},
    {"firebrick", 0xB22222},
    {"floralwhite", 0xFFFAF0},
    {"forestgreen", 0x228B22},
    {"fuchsia", 0xFF00FF},
    {"gainsboro", 0xDCDCDC},
    {"ghostwhite", 0xF8F8FF},
    {"gold", 0xFFD700},
    {"goldenrod", 0xDAA520},
    {"gray", 0x808080},
    {"green", 0x008000},
    {"greenyellow", 0xADFF2F},
    {"grey", 0x808080},
    {"honeydew", 0xF0FFF0},
    {"hotpink", 0xFF69B4},
    {"indianred", 0xCD5C5C},
    {"indigo", 0x4B0082},
    {"ivory", 0xFFFFF0},
    {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA},
    {"lavenderblush", 0xFFF0F5},
    {"lawngreen", 0x7CFC00},
    {"lemonchiffon", 0xFFFACD},
    {"lightblue", 0xADD8E6},
    {"lightcoral", 0xF08080},
    {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2},
    {"lightgray", 0xD3D3D3},
    {"lightgreen", 0x90EE90},
    {"lightgrey", 0xD3D3D3},
    {"lightpink", 0xFFB6C1},
    {"lightsalmon", 0xFFA07A},
    {"lightseagreen", 0x20B2AA},
    {"lightskyblue", 0x87CEFA},
    {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xB0C4DE},
    {"lightyellow", 0xFFFFE0},
    {"lime", 0x00FF00},
    {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6},
    {"magenta", 0xFF00FF},
    {"maroon", 0x800000},
    {"mediumaquamarine", 0x66CDAA},
    {"mediumblue", 0x0000CD},
    {"mediumorchid", 0xBA55D3},
    {"mediumpurple", 0x9370DB},
    {"mediumseagreen", 0x3CB371},
    {"mediumslateblue", 0x7B68EE},
    {"mediumspringgreen", 0x00FA9A},
    {"mediumturquoise", 0x48D1CC},
    {"mediumvioletred", 0xC71585},
    {"midnightblue", 0x191970},
    {"mintcream", 0xF5FFFA},
    {"mistyrose", 0xFFE4E1},
    {"moccasin", 0xFFE4B5},
    {"navajowhite", 0xFFDEAD},
    {"navy", 0x000080},
    {"oldlace", 0xFDF5E6},
    {"olive", 0x808000},
    {"olivedrab", 0x6B8E23},
    {"orange", 0xFFA500},
    {"orangered", 0xFF4500},
    {"orchid", 0xDA70D6},
    {"palegoldenrod", 0xEEE8AA},
    {"palegreen", 0x98FB98},
    {"paleturquoise", 0xAFEEEE},
    {"palevioletred", 0xDB7093},
    {"papayawhip", 0xFFEFD5},
    {"peachpuff", 0xFFDAB9},
    {"peru", 0xCD853F},
    {"pink", 0xFFC0CB},
    {"plum", 0xDDA0DD},
    {"powderblue", 0xB0E0E6},
    {"purple", 0x800080},
    {"rebeccapurple", 0x663399},
    {"red", 0xFF0000},
    {"rosybrown", 0xBC8F8F},
    {"royalblue", 0x4169E1},
    {"saddlebrown", 0x8B4513},
    {"salmon", 0xFA8072},
    {"sandybrown", 0xF4A460},
    {"seagreen", 0x2E8B57},
    {"seashell", 0xFFF5EE},
    {"sienna", 0xA0522D},
    {"silver", 0xC0C0C0},
    {"skyblue", 0x87CEEB},
    {"slateblue", 0x6A5ACD},
    {"slategray", 0x708090},
    {"slategrey", 0x708090},
    {"snow", 0xFFFAFA},
    {"springgreen", 0x00FF7F},
    {"steelblue", 0x4682B4},
    {"tan", 0xD2B48C},
    {"teal", 0x008080},
    {"thistle", 0xD8BFD8},
    {"tomato", 0xFF6347},
    {"turquoise", 0x40E0D0},
    {"violet", 0xEE82EE},
    {"wheat", 0xF5DEB3},
    {"white", 0xFFFFFF},
    {"whitesmoke", 0xF5F5F5},
    {"yellow", 0xFFFF00},
    {"yellowgreen", 0x9ACD32},
}};

Color from_rgb24(std::uint32_t rgb) {
  return Color{.r = static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU),
               .g = static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU),
               .b = static_cast<std::uint8_t>(rgb & 0xFFU),
               .a = 255};
}

std::optional<std::uint8_t> hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  const char lower = ascii_lower(c);
  if (lower >= 'a' && lower <= 'f') {
    return static_cast<std::uint8_t>(lower - 'a' + 10);
  }
  return std::nullopt;
}

// 0..255 に丸めて収める。丸めは四捨五入（raster の丸めと同じ流儀）。
std::uint8_t to_channel(double v) {
  if (!(v > 0)) {  // NaN もここで 0 に落とす
    return 0;
  }
  if (v >= 255) {
    return 255;
  }
  return static_cast<std::uint8_t>(std::lround(v));
}

std::optional<Color> parse_hex(std::string_view hex) {
  if (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8) {
    return std::nullopt;
  }
  std::array<std::uint8_t, 8> digits{};
  for (std::size_t i = 0; i < hex.size(); ++i) {
    const std::optional<std::uint8_t> d = hex_digit(hex[i]);
    if (!d) {
      return std::nullopt;
    }
    digits[i] = *d;
  }

  const bool shorthand = hex.size() <= 4;
  auto channel = [&](std::size_t index) {
    if (shorthand) {
      const auto d = digits[index];
      return static_cast<std::uint8_t>(d * 17);  // #abc → #aabbcc
    }
    return static_cast<std::uint8_t>(digits[index * 2] * 16 + digits[(index * 2) + 1]);
  };
  const bool has_alpha = hex.size() == 4 || hex.size() == 8;
  return Color{.r = channel(0),
               .g = channel(1),
               .b = channel(2),
               .a = has_alpha ? channel(3) : static_cast<std::uint8_t>(255)};
}

// rgb() / rgba() の中身を「成分値の並び」と「区切りがカンマかどうか」に分ける。
struct FunctionArgs {
  std::vector<const ValueToken*> components;  // 色成分（3 個）
  const ValueToken* alpha = nullptr;
  bool valid = false;
};

FunctionArgs split_rgb_args(const std::vector<ValueToken>& args) {
  FunctionArgs out;
  std::vector<const ValueToken*> values;
  std::size_t commas = 0;
  std::size_t slashes = 0;
  std::size_t slash_index = 0;
  for (const ValueToken& token : args) {
    if (token.kind == ValueToken::Kind::Comma) {
      ++commas;
    } else if (token.kind == ValueToken::Kind::Slash) {
      ++slashes;
      slash_index = values.size();
    } else {
      values.push_back(&token);
    }
  }

  // 旧構文 `rgb(r, g, b[, a])`: カンマで区切られ、スラッシュはない
  if (commas > 0) {
    if (slashes != 0 || commas != values.size() - 1 || (values.size() != 3 && values.size() != 4)) {
      return out;
    }
    out.components.assign(values.begin(), values.begin() + 3);
    out.alpha = values.size() == 4 ? values[3] : nullptr;
    out.valid = true;
    return out;
  }

  // 新構文 `rgb(r g b[ / a])`
  if (slashes > 1) {
    return out;
  }
  if (slashes == 1) {
    if (slash_index != 3 || values.size() != 4) {
      return out;
    }
    out.alpha = values[3];
  } else if (values.size() != 3) {
    return out;
  }
  out.components.assign(values.begin(), values.begin() + 3);
  out.valid = true;
  return out;
}

std::optional<double> component_value(const ValueToken& token) {
  if (token.kind == ValueToken::Kind::Number) {
    return token.number;
  }
  if (token.kind == ValueToken::Kind::Percentage) {
    return token.number * 255.0 / 100.0;
  }
  return std::nullopt;
}

std::optional<double> alpha_value(const ValueToken& token) {
  if (token.kind == ValueToken::Kind::Number) {
    return token.number * 255.0;
  }
  if (token.kind == ValueToken::Kind::Percentage) {
    return token.number * 255.0 / 100.0;
  }
  return std::nullopt;
}

std::optional<Color> parse_rgb_function(const ValueToken& token) {
  const FunctionArgs args = split_rgb_args(token.args);
  if (!args.valid) {
    return std::nullopt;
  }
  Color color;
  std::array<std::uint8_t*, 3> targets = {&color.r, &color.g, &color.b};
  for (std::size_t i = 0; i < 3; ++i) {
    const std::optional<double> v = component_value(*args.components[i]);
    if (!v) {
      return std::nullopt;
    }
    *targets[i] = to_channel(*v);
  }
  if (args.alpha != nullptr) {
    const std::optional<double> a = alpha_value(*args.alpha);
    if (!a) {
      return std::nullopt;
    }
    color.a = to_channel(*a);
  }
  return color;
}

}  // namespace

std::optional<Color> lookup_color_name(std::string_view lower_name) {
  if (lower_name == "transparent") {
    return kTransparent;
  }
  const auto* it = std::lower_bound(
      kNamedColors.begin(), kNamedColors.end(), lower_name,
      [](const NamedColor& entry, std::string_view name) { return entry.name < name; });
  if (it == kNamedColors.end() || it->name != lower_name) {
    return std::nullopt;
  }
  return from_rgb24(it->rgb);
}

std::optional<SpecColor> parse_color_token(const ValueToken& token) {
  switch (token.kind) {
    case ValueToken::Kind::Hash: {
      const std::optional<Color> color = parse_hex(token.text);
      if (!color) {
        return std::nullopt;
      }
      return SpecColor{.current_color = false, .color = *color};
    }
    case ValueToken::Kind::Ident: {
      const std::string lower = ascii_lower(token.text);
      if (lower == "currentcolor") {
        return SpecColor{.current_color = true, .color = kBlack};
      }
      const std::optional<Color> color = lookup_color_name(lower);
      if (!color) {
        return std::nullopt;
      }
      return SpecColor{.current_color = false, .color = *color};
    }
    case ValueToken::Kind::Function: {
      if (token.text != "rgb" && token.text != "rgba") {
        return std::nullopt;
      }
      const std::optional<Color> color = parse_rgb_function(token);
      if (!color) {
        return std::nullopt;
      }
      return SpecColor{.current_color = false, .color = *color};
    }
    default:
      return std::nullopt;
  }
}

}  // namespace shashoku::style
