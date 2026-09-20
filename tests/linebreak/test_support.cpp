#include "linebreak/test_support.hpp"

#include <cstdint>

namespace shashoku::linebreak::test {

float default_advance(char32_t cp, float em) { return cp < 0x80 ? 0.5F * em : em; }

Config with_strictness(Strictness strictness) {
  Config config;
  config.strictness = strictness;
  return config;
}

Config with_overflow(OverflowPolicy overflow) {
  Config config;
  config.overflow = overflow;
  return config;
}

std::u32string to_utf32(std::string_view utf8) {
  std::u32string out;
  std::size_t i = 0;
  while (i < utf8.size()) {
    const auto lead = static_cast<unsigned char>(utf8[i]);
    std::size_t extra = 0;
    char32_t cp = 0;
    if (lead < 0x80) {
      cp = lead;
    } else if ((lead & 0xE0U) == 0xC0U) {
      cp = lead & 0x1FU;
      extra = 1;
    } else if ((lead & 0xF0U) == 0xE0U) {
      cp = lead & 0x0FU;
      extra = 2;
    } else {
      cp = lead & 0x07U;
      extra = 3;
    }
    for (std::size_t k = 0; k < extra && i + 1 + k < utf8.size(); ++k) {
      cp = (cp << 6U) | (static_cast<unsigned char>(utf8[i + 1 + k]) & 0x3FU);
    }
    out.push_back(cp);
    i += extra + 1;
  }
  return out;
}

std::string to_utf8(char32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  }
  return out;
}

std::vector<Item> items_of(std::string_view utf8, float em,
                           const std::function<float(char32_t)>& advance) {
  std::vector<Item> items;
  for (const char32_t cp : to_utf32(utf8)) {
    Item item;
    item.cp = cp;
    item.em = em;
    if (cp == U'\n') {
      item.kind = ItemKind::ForcedBreak;
      item.advance = 0.0F;
    } else {
      item.advance = advance(cp);
    }
    items.push_back(item);
  }
  return items;
}

std::vector<Item> items_of(std::string_view utf8, float em) {
  return items_of(utf8, em, [em](char32_t cp) { return default_advance(cp, em); });
}

namespace {

std::string render(std::span<const Item> items, std::size_t begin, std::size_t end) {
  std::string out;
  for (std::size_t i = begin; i < end; ++i) {
    out += to_utf8(items[i].cp);
  }
  return out;
}

}  // namespace

std::vector<std::string> line_texts(std::span<const Item> items, const Breaks& breaks) {
  std::vector<std::string> out;
  out.reserve(breaks.lines.size());
  for (const Line& line : breaks.lines) {
    out.push_back(render(items, line.begin, line.content_end));
  }
  return out;
}

std::vector<std::string> line_texts_full(std::span<const Item> items, const Breaks& breaks) {
  std::vector<std::string> out;
  out.reserve(breaks.lines.size());
  for (const Line& line : breaks.lines) {
    out.push_back(render(items, line.begin, line.end));
  }
  return out;
}

std::string mark_opportunities(std::string_view utf8, const Config& config) {
  const std::vector<Item> items = items_of(utf8);
  const LineBreaker breaker(config);
  const std::vector<bool> opportunities = breaker.break_opportunities(items);
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0 && opportunities[i]) {
      out.push_back('|');
    }
    out += to_utf8(items[i].cp);
  }
  return out;
}

}  // namespace shashoku::linebreak::test
