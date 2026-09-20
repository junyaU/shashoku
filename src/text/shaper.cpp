#include "text/shaper.hpp"

#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hb-ot.h>

#include "core/ids.hpp"
#include "text/char_properties.hpp"
#include "text/font_store.hpp"
#include "text/font_store_impl.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {
namespace {

// HarfBuzz / FreeType と同じ 26.6 固定小数。font unit ⇄ px の換算はこの 2 関数に集約する。
constexpr int kFixedOne = 64;
constexpr char32_t kTofu = 0x25A1;  // □

[[nodiscard]] float to_px(hb_position_t value) {
  return static_cast<float>(value) / static_cast<float>(kFixedOne);
}

[[nodiscard]] int to_fixed(float px) {
  if (!(px > 0.0F)) {
    return 0;
  }
  return static_cast<int>(std::lround(px * static_cast<float>(kFixedOne)));
}

// font-family の照合は大文字小文字を無視し、前後の空白を落とす（ARCHITECTURE.md §3.5）。
[[nodiscard]] std::string fold_family_name(std::string_view name) {
  std::size_t begin = 0;
  std::size_t end = name.size();
  while (begin < end && (name[begin] == ' ' || name[begin] == '\t')) {
    ++begin;
  }
  while (end > begin && (name[end - 1] == ' ' || name[end - 1] == '\t')) {
    --end;
  }
  std::string folded(name.substr(begin, end - begin));
  for (char& c : folded) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return folded;
}

// CSS Fonts 4 §5.2 の font-weight 照合順。tier が小さいほど、同じ tier なら
// distance が小さいほど望ましい。
struct WeightRank {
  int tier = 0;
  int distance = 0;

  [[nodiscard]] bool better_than(const WeightRank& other) const {
    return tier != other.tier ? tier < other.tier : distance < other.distance;
  }
};

[[nodiscard]] WeightRank weight_rank(int candidate, int desired) {
  if (desired >= 400 && desired <= 500) {
    if (candidate >= desired && candidate <= 500) {
      return {0, candidate - desired};
    }
    if (candidate < desired) {
      return {1, desired - candidate};
    }
    return {2, candidate - 500};
  }
  if (desired < 400) {
    return candidate <= desired ? WeightRank{0, desired - candidate}
                                : WeightRank{1, candidate - desired};
  }
  return candidate >= desired ? WeightRank{0, candidate - desired}
                              : WeightRank{1, desired - candidate};
}

}  // namespace

namespace detail {

// 縦書きでの 1 文字の置き方。
enum class Orientation : std::uint8_t {
  Horizontal,  // 横書き
  Upright,     // 縦書き・立てる（HB_DIRECTION_TTB でシェーピング）
  Sideways,  // 縦書き・横倒し（横組みでシェーピングして時計回りに 90° 回す）
};

// コードポイント 1 個ぶんの割り当て。同じ (font, script, orientation, missing) が続く
// 区間が 1 つの run になる。
struct CharPlan {
  FontId font = 0;
  hb_script_t script = HB_SCRIPT_COMMON;
  Orientation orientation = Orientation::Horizontal;
  bool missing = false;
  bool attached = false;  // 直前の文字にくっつく（結合文字・異体字セレクタ・ZWJ の後ろ）

  [[nodiscard]] bool same_run_as(const CharPlan& other) const {
    return font == other.font && script == other.script && orientation == other.orientation &&
           missing == other.missing;
  }
};

struct ShaperImpl {
  const FontStore* fonts = nullptr;
  hb_buffer_t* buffer = nullptr;
  hb_buffer_t* probe_buffer = nullptr;
  hb_language_t language = nullptr;
  std::vector<hb_font_t*> shaping_fonts;  // FontId → シェーピング用 hb_font（遅延生成）
  std::vector<MissingGlyph> missing;
  std::set<char32_t> missing_seen;
  std::map<std::pair<FontId, char32_t>, bool> vertical_form_cache;

  explicit ShaperImpl(const FontStore& store)
      : fonts(&store),
        buffer(hb_buffer_create()),
        probe_buffer(hb_buffer_create()),
        language(hb_language_from_string("ja", -1)) {}
  ShaperImpl(const ShaperImpl&) = delete;
  ShaperImpl& operator=(const ShaperImpl&) = delete;
  ShaperImpl(ShaperImpl&&) = delete;
  ShaperImpl& operator=(ShaperImpl&&) = delete;

  ~ShaperImpl() {
    for (hb_font_t* font : shaping_fonts) {
      if (font != nullptr) {
        hb_font_destroy(font);
      }
    }
    hb_buffer_destroy(probe_buffer);
    hb_buffer_destroy(buffer);
  }

  [[nodiscard]] const FontEntry* entry(FontId font) const {
    return FontStoreAccess::impl(*fonts).at(font);
  }

  // スケールを設定したシェーピング用フォント。FontStore が持つ hb_font は
  // font unit スケールのまま使いたいので、Shaper 側で別に作る。
  hb_font_t* shaping_font(FontId font, int scale) {
    const FontEntry* font_entry = entry(font);
    if (font_entry == nullptr) {
      return nullptr;
    }
    if (shaping_fonts.size() <= font) {
      shaping_fonts.resize(std::size_t{font} + 1, nullptr);
    }
    if (shaping_fonts[font] == nullptr) {
      hb_font_t* created = hb_font_create(font_entry->hb_face);
      hb_ot_font_set_funcs(created);  // メトリクスは hb-ot から読む（A7）
      shaping_fonts[font] = created;
    }
    hb_font_set_scale(shaping_fonts[font], scale, scale);
    return shaping_fonts[font];
  }

  [[nodiscard]] std::vector<FontId> resolve_stack(const TextStyle& style) const;
  [[nodiscard]] FontMetrics font_metrics(FontId font, float font_size);
  [[nodiscard]] std::vector<hb_codepoint_t> probe_glyphs(hb_font_t* font, char32_t cp,
                                                         hb_direction_t direction) const;
  [[nodiscard]] bool has_vertical_form(FontId font, char32_t cp);
  [[nodiscard]] CharPlan resolve_char(const std::vector<FontId>& stack, char32_t cp, bool vertical);
  [[nodiscard]] std::vector<CharPlan> build_plan(std::u32string_view text,
                                                 const std::vector<FontId>& stack, bool vertical);

  void record_missing(char32_t cp) {
    if (missing_seen.insert(cp).second) {
      missing.push_back(MissingGlyph{cp});
    }
  }

  void shape_run(std::u32string_view text, std::size_t begin, std::size_t end, const CharPlan& plan,
                 const TextStyle& style, int scale, ShapedText& out);
  void emit_missing_run(std::u32string_view text, std::size_t begin, std::size_t end,
                        const std::vector<CharPlan>& plan, const std::vector<FontId>& stack,
                        const TextStyle& style, ShapedText& out);
  ShapedText shape(std::u32string_view text, const TextStyle& style);
};

std::vector<FontId> ShaperImpl::resolve_stack(const TextStyle& style) const {
  std::vector<FontId> stack;
  const auto count = static_cast<FontId>(fonts->size());

  auto push_unique = [&stack](FontId font) {
    if (std::find(stack.begin(), stack.end(), font) == stack.end()) {
      stack.push_back(font);
    }
  };

  for (const std::string& requested : style.font_family) {
    const std::string wanted = fold_family_name(requested);
    if (wanted.empty()) {
      continue;
    }
    // 同じ family に複数の weight があれば CSS の規則で 1 つ選ぶ。
    bool found = false;
    FontId best = 0;
    WeightRank best_rank;
    for (FontId font = 0; font < count; ++font) {
      if (fold_family_name(fonts->family(font)) != wanted) {
        continue;
      }
      const WeightRank rank = weight_rank(fonts->weight(font), style.font_weight);
      if (!found || rank.better_than(best_rank)) {
        found = true;
        best = font;
        best_rank = rank;
      }
    }
    if (found) {
      push_unique(best);  // 見つからない名前は読み飛ばす（A15）
    }
  }

  for (FontId font = 0; font < count; ++font) {
    push_unique(font);  // 指定を使い切ったら FontStore の追加順で探す
  }
  return stack;
}

FontMetrics ShaperImpl::font_metrics(FontId font, float font_size) {
  FontMetrics result;
  hb_font_t* hb_font = shaping_font(font, to_fixed(font_size));
  if (hb_font == nullptr) {
    return result;
  }
  hb_font_extents_t extents{};
  if (hb_font_get_h_extents(hb_font, &extents) == 0) {
    return result;
  }
  result.ascent = std::max(0.0F, to_px(extents.ascender));
  result.descent = std::max(0.0F, -to_px(extents.descender));
  result.line_gap = std::max(0.0F, to_px(extents.line_gap));
  return result;
}

std::vector<hb_codepoint_t> ShaperImpl::probe_glyphs(hb_font_t* font, char32_t cp,
                                                     hb_direction_t direction) const {
  const auto value = static_cast<std::uint32_t>(cp);
  hb_buffer_clear_contents(probe_buffer);
  hb_buffer_add_utf32(probe_buffer, &value, 1, 0, 1);
  hb_buffer_set_direction(probe_buffer, direction);
  hb_buffer_set_script(probe_buffer, hb_unicode_script(hb_unicode_funcs_get_default(), cp));
  hb_buffer_set_language(probe_buffer, language);
  hb_shape(font, probe_buffer, nullptr, 0);

  unsigned int count = 0;
  const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(probe_buffer, &count);
  std::vector<hb_codepoint_t> ids;
  ids.reserve(count);
  for (unsigned int i = 0; i < count; ++i) {
    ids.push_back(infos[i].codepoint);
  }
  return ids;
}

// UAX #50 の Tr（transformed, fallback rotated）用。フォントが縦書き用グリフ（vert）を
// 持つなら立てて差し替え、持たないなら横倒しにする、というのが UAX #50 の規定。
bool ShaperImpl::has_vertical_form(FontId font, char32_t cp) {
  const std::pair<FontId, char32_t> key{font, cp};
  if (const auto it = vertical_form_cache.find(key); it != vertical_form_cache.end()) {
    return it->second;
  }
  bool result = false;
  const FontEntry* font_entry = entry(font);
  if (font_entry != nullptr) {
    result = probe_glyphs(font_entry->hb_font, cp, HB_DIRECTION_LTR) !=
             probe_glyphs(font_entry->hb_font, cp, HB_DIRECTION_TTB);
  }
  vertical_form_cache.emplace(key, result);
  return result;
}

CharPlan ShaperImpl::resolve_char(const std::vector<FontId>& stack, char32_t cp, bool vertical) {
  CharPlan plan;
  plan.font = stack.empty() ? FontId{0} : stack.front();
  plan.missing = true;
  for (const FontId font : stack) {
    if (fonts->has_glyph(font, cp)) {
      plan.font = font;
      plan.missing = false;
      break;
    }
  }

  if (!vertical) {
    plan.orientation = Orientation::Horizontal;
    return plan;
  }
  if (plan.missing) {
    plan.orientation = Orientation::Upright;  // 豆腐（□）は立てる
    return plan;
  }
  switch (vertical_orientation(cp)) {
    case VerticalOrientation::Upright:
    case VerticalOrientation::TransformedUpright:
      plan.orientation = Orientation::Upright;
      break;
    case VerticalOrientation::TransformedRotated:
      plan.orientation =
          has_vertical_form(plan.font, cp) ? Orientation::Upright : Orientation::Sideways;
      break;
    case VerticalOrientation::Rotated:
      plan.orientation = Orientation::Sideways;
      break;
  }
  return plan;
}

void ShaperImpl::shape_run(std::u32string_view text, std::size_t begin, std::size_t end,
                           const CharPlan& plan, const TextStyle& style, int scale,
                           ShapedText& out) {
  const auto text_begin = static_cast<std::uint32_t>(begin);
  const auto text_end = static_cast<std::uint32_t>(end);
  const auto glyph_base = static_cast<std::uint32_t>(out.glyphs.size());

  // 横倒しの run は、回転後に欧文の字面が中心軸に対して中央へ来るようベースラインをずらす。
  // （ascent 側が軸の右、descent 側が軸の左に出るので、その差の半分だけ戻す）
  float baseline_shift = 0.0F;
  if (plan.orientation == Orientation::Sideways) {
    const FontMetrics run_metrics = font_metrics(plan.font, style.font_size);
    baseline_shift = (run_metrics.descent - run_metrics.ascent) / 2.0F;
  }

  hb_font_t* hb_font = shaping_font(plan.font, scale);
  if (hb_font != nullptr) {
    hb_buffer_clear_contents(buffer);
    // run の前後も文脈として渡す（hb はクラスタ値を text 全体の添字で返す）。
    hb_buffer_add_utf32(buffer, reinterpret_cast<const std::uint32_t*>(text.data()),
                        static_cast<int>(text.size()), static_cast<unsigned int>(begin),
                        static_cast<int>(end - begin));
    hb_buffer_set_direction(
        buffer, plan.orientation == Orientation::Upright ? HB_DIRECTION_TTB : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, plan.script);
    hb_buffer_set_language(buffer, language);  // ロケールを読ませない（決定性）
    hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_shape(hb_font, buffer, nullptr, 0);
  }

  unsigned int count = 0;
  const hb_glyph_info_t* infos =
      hb_font != nullptr ? hb_buffer_get_glyph_infos(buffer, &count) : nullptr;
  const hb_glyph_position_t* positions =
      hb_font != nullptr ? hb_buffer_get_glyph_positions(buffer, &count) : nullptr;
  if (infos == nullptr || positions == nullptr) {
    count = 0;
  }

  for (unsigned int i = 0; i < count; ++i) {
    ShapedGlyph glyph;
    glyph.font = plan.font;
    glyph.glyph_id = static_cast<GlyphId>(infos[i].codepoint);
    switch (plan.orientation) {
      case Orientation::Horizontal:
        glyph.advance = to_px(positions[i].x_advance);
        glyph.x_offset = to_px(positions[i].x_offset);
        glyph.y_offset = -to_px(positions[i].y_offset);  // HarfBuzz は y 上向き
        break;
      case Orientation::Upright:
        glyph.advance = -to_px(positions[i].y_advance);  // TTB の送りは負で返る
        glyph.x_offset = to_px(positions[i].x_offset);   // v_origin ぶん左にずれている
        glyph.y_offset = -to_px(positions[i].y_offset);
        break;
      case Orientation::Sideways:
        // 時計回りに 90° 回すと、横組みの +x が下、上（-y）が右になる。
        glyph.advance = to_px(positions[i].x_advance);
        glyph.y_offset = to_px(positions[i].x_offset);
        glyph.x_offset = to_px(positions[i].y_offset) + baseline_shift;
        glyph.sideways = true;
        break;
    }
    out.glyphs.push_back(glyph);
  }

  // クラスタを HarfBuzz の cluster 値から組み立てる。
  // 不変条件（text_measurer.hpp）: run の全体を隙間なく覆い、text_begin は単調増加。
  struct Group {
    std::uint32_t start = 0;
    std::uint32_t glyph_end = 0;
  };
  std::vector<Group> groups;
  for (unsigned int i = 0; i < count; ++i) {
    std::uint32_t start = infos[i].cluster;
    start = std::clamp(start, text_begin, text_end);
    if (groups.empty()) {
      start = text_begin;  // 先頭のクラスタは必ず run の先頭から始まる
    } else if (start <= groups.back().start) {
      groups.back().glyph_end = glyph_base + i + 1;
      continue;  // 同じクラスタ（または逆行）はまとめる
    }
    groups.push_back(Group{start, glyph_base + i + 1});
  }

  if (groups.empty()) {
    // シェーピングで全グリフが消えた（既定無視文字だけの run など）。
    out.clusters.push_back(
        ShapedCluster{text_begin, text_end, glyph_base, glyph_base, 0.0F, false});
    return;
  }

  for (std::size_t k = 0; k < groups.size(); ++k) {
    ShapedCluster cluster;
    cluster.text_begin = groups[k].start;
    cluster.text_end = k + 1 < groups.size() ? groups[k + 1].start : text_end;
    cluster.glyph_begin = k == 0 ? glyph_base : groups[k - 1].glyph_end;
    cluster.glyph_end = groups[k].glyph_end;
    for (std::uint32_t g = cluster.glyph_begin; g < cluster.glyph_end; ++g) {
      cluster.advance += out.glyphs[g].advance;
    }
    out.clusters.push_back(cluster);
  }
}

void ShaperImpl::emit_missing_run(std::u32string_view text, std::size_t begin, std::size_t end,
                                  const std::vector<CharPlan>& plan,
                                  const std::vector<FontId>& stack, const TextStyle& style,
                                  ShapedText& out) {
  // 豆腐は □（U+25A1）をフォールバック列の順に探し、最初に見つかったフォントのグリフで描く。
  // 第一フォントだけを見ると、欧文フォントが先頭のときに幅の狭い .notdef が 1em の枠の
  // 左端に出て不揃いになる。どのフォントにも □ が無いときだけ第一フォントの .notdef。
  FontId font = stack.empty() ? FontId{0} : stack.front();
  GlyphId tofu_glyph = 0;
  for (const FontId candidate : stack) {
    const GlyphId glyph = fonts->glyph_for(candidate, kTofu);
    if (glyph != 0) {
      font = candidate;
      tofu_glyph = glyph;
      break;
    }
  }
  const bool vertical = style.direction == Direction::Vertical;
  const float ascent = vertical ? font_metrics(font, style.font_size).ascent : 0.0F;

  for (std::size_t i = begin; i < end; ++i) {
    if (i > begin && plan[i].attached) {
      out.clusters.back().text_end = static_cast<std::uint32_t>(i + 1);
      continue;
    }
    record_missing(text[i]);

    ShapedGlyph glyph;
    glyph.font = font;
    glyph.glyph_id = tofu_glyph;
    glyph.advance = style.font_size;
    if (vertical) {
      // 縦書きのペン位置は中心軸上なので、1em の字面を軸の左右に半分ずつ振り分ける。
      glyph.x_offset = -style.font_size / 2.0F;
      glyph.y_offset = ascent;
    }

    ShapedCluster cluster;
    cluster.text_begin = static_cast<std::uint32_t>(i);
    cluster.text_end = static_cast<std::uint32_t>(i + 1);
    cluster.glyph_begin = static_cast<std::uint32_t>(out.glyphs.size());
    cluster.glyph_end = cluster.glyph_begin + 1;
    cluster.advance = glyph.advance;
    cluster.missing = true;

    out.glyphs.push_back(glyph);
    out.clusters.push_back(cluster);
  }
}

// コードポイントごとにフォント・向き・スクリプトを決める。
std::vector<CharPlan> ShaperImpl::build_plan(std::u32string_view text,
                                             const std::vector<FontId>& stack, bool vertical) {
  hb_unicode_funcs_t* unicode = hb_unicode_funcs_get_default();
  std::vector<CharPlan> plan(text.size());
  hb_script_t running_script = HB_SCRIPT_COMMON;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char32_t cp = text[i];
    // 結合文字・異体字セレクタ・ZWJ の後ろは直前と同じ run に入れる（別フォントに割らない）。
    if (i > 0 && (is_cluster_extender(cp) || is_zero_width_joiner(text[i - 1]))) {
      plan[i] = plan[i - 1];
      plan[i].attached = true;
      continue;
    }
    const hb_script_t script = hb_unicode_script(unicode, cp);
    const bool weak =
        script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_UNKNOWN;
    if (!weak) {
      running_script = script;
    }
    plan[i] = resolve_char(stack, cp, vertical);
    plan[i].script = running_script;  // 約物や数字は直前のスクリプトを継ぐ
  }
  return plan;
}

namespace {

// 結合文字・異体字セレクタ・ZWJ 連結が別クラスタに割れていたら繋ぐ。
// HarfBuzz は既定無視文字の扱いがフォント依存なので、ここで規則を確定させる。
std::vector<ShapedCluster> join_attached_clusters(std::u32string_view text,
                                                  const std::vector<CharPlan>& plan,
                                                  const std::vector<ShapedCluster>& clusters) {
  std::vector<ShapedCluster> merged;
  merged.reserve(clusters.size());
  for (const ShapedCluster& cluster : clusters) {
    const bool joins_previous =
        !merged.empty() && cluster.text_begin < text.size() &&
        (plan[cluster.text_begin].attached ||
         (cluster.text_begin > 0 && is_zero_width_joiner(text[cluster.text_begin - 1])));
    if (!joins_previous) {
      merged.push_back(cluster);
      continue;
    }
    merged.back().text_end = std::max(merged.back().text_end, cluster.text_end);
    merged.back().glyph_end = std::max(merged.back().glyph_end, cluster.glyph_end);
    merged.back().advance += cluster.advance;
    merged.back().missing = merged.back().missing || cluster.missing;
  }
  return merged;
}

// text_measurer.hpp の不変条件を最後に整える: 入力全体を隙間なく覆う /
// text_begin は単調増加 / glyph の範囲も隙間なく連続して glyphs をちょうど覆う。
void normalize_clusters(ShapedText& out, std::size_t text_length) {
  const auto total = static_cast<std::uint32_t>(text_length);
  std::vector<ShapedCluster> fixed;
  fixed.reserve(out.clusters.size());
  std::uint32_t next_text = 0;
  std::uint32_t next_glyph = 0;
  for (const ShapedCluster& cluster : out.clusters) {
    if (next_text >= total && !fixed.empty()) {
      // 覆う範囲が尽きた: 余ったグリフは直前のクラスタに吸収させる。
      fixed.back().glyph_end = std::max(fixed.back().glyph_end, cluster.glyph_end);
      fixed.back().advance += cluster.advance;
      fixed.back().missing = fixed.back().missing || cluster.missing;
      continue;
    }
    ShapedCluster adjusted = cluster;
    adjusted.text_begin = next_text;
    adjusted.text_end = std::clamp(cluster.text_end, next_text + 1, total);
    adjusted.glyph_begin = next_glyph;
    adjusted.glyph_end = std::max(cluster.glyph_end, next_glyph);
    fixed.push_back(adjusted);
    next_text = adjusted.text_end;
    next_glyph = adjusted.glyph_end;
  }
  if (!fixed.empty()) {
    fixed.back().text_end = total;
    fixed.back().glyph_end = static_cast<std::uint32_t>(out.glyphs.size());
  }
  out.clusters = std::move(fixed);
}

}  // namespace

ShapedText ShaperImpl::shape(std::u32string_view text, const TextStyle& style) {
  ShapedText out;
  if (text.empty()) {
    return out;
  }

  const std::vector<FontId> stack = resolve_stack(style);
  const int scale = to_fixed(style.font_size);
  const std::vector<CharPlan> plan =
      build_plan(text, stack, style.direction == Direction::Vertical);

  // 同じ run が続く区間ごとにシェーピングする。
  std::size_t begin = 0;
  while (begin < text.size()) {
    std::size_t end = begin + 1;
    while (end < text.size() && plan[end].same_run_as(plan[begin])) {
      ++end;
    }
    if (plan[begin].missing) {
      emit_missing_run(text, begin, end, plan, stack, style, out);
    } else {
      shape_run(text, begin, end, plan[begin], style, scale, out);
    }
    begin = end;
  }

  out.clusters = join_attached_clusters(text, plan, out.clusters);
  normalize_clusters(out, text.size());
  return out;
}

}  // namespace detail

Shaper::Shaper(const FontStore& fonts) : impl_(std::make_unique<detail::ShaperImpl>(fonts)) {}
Shaper::~Shaper() = default;

ShapedText Shaper::shape(std::u32string_view text, const TextStyle& style) {
  return impl_->shape(text, style);
}

FontMetrics Shaper::metrics(const TextStyle& style) {
  const std::vector<FontId> stack = impl_->resolve_stack(style);
  if (stack.empty()) {
    return {};
  }
  return impl_->font_metrics(stack.front(), style.font_size);
}

std::vector<MissingGlyph> Shaper::take_missing_glyphs() {
  std::vector<MissingGlyph> taken = std::move(impl_->missing);
  impl_->missing.clear();
  impl_->missing_seen.clear();
  return taken;
}

}  // namespace shashoku::text
