#include "text/shaper.hpp"

#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <hb-ot.h>

#include "core/ids.hpp"
#include "core/number_text.hpp"
#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "text/char_properties.hpp"
#include "text/font_store.hpp"
#include "text/font_store_impl.hpp"
#include "text/text_measurer.hpp"

namespace shashoku::text {
namespace {

// HarfBuzz / FreeType と同じ 26.6 固定小数。font unit ⇄ px の換算はこの 2 関数に集約する。
constexpr int kFixedOne = 64;
constexpr char32_t kTofu = 0x25A1;  // □

// エラーメッセージに必ず「どのフォントの何が原因か」を入れる（DESIGN.md §3-6 fail loudly）。
// 値は number_text() を通す: NaN の符号ビットは CPU によって違うので、そのまま出すと
// 同じ入力でも文面が環境で変わる（core/number_text.hpp）。
[[nodiscard]] std::string where(FontId font, float font_size) {
  return std::format("FontId {}, {} px", font, number_text(font_size));
}

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

// 同じ family 名を持つ face のまとまり。fonts は font_weight の近い順。
struct FamilyGroup {
  std::string folded_name;
  std::vector<FontId> fonts;
};

struct ShaperImpl {
  const FontStore* fonts = nullptr;
  hb_buffer_t* buffer = nullptr;
  hb_buffer_t* probe_buffer = nullptr;
  hb_language_t language = nullptr;
  std::vector<hb_font_t*> shaping_fonts;  // FontId → シェーピング用 hb_font（遅延生成）
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

  // スケールを設定したシェーピング用フォント。FontStore が持つ hb_font は共有資源で
  // 不変（setter が黙って失敗する。A34）なので、スケールを変える側は必ず自分で作る。
  // 失敗は 2 つ: 不正な FontId（呼び出し側のバグ = Internal）と HarfBuzz の確保失敗
  // （hb_font_create は失敗すると空のフォントを返す = OutOfMemory。A26 / A30）。
  Result<hb_font_t*> shaping_font(FontId font, int scale) {
    const FontEntry* font_entry = entry(font);
    if (font_entry == nullptr) {
      return fail(ErrorKind::Internal,
                  std::format("cannot shape (FontId {}): no such FontId in the FontStore", font));
    }
    if (shaping_fonts.size() <= font) {
      shaping_fonts.resize(std::size_t{font} + 1, nullptr);
    }
    if (shaping_fonts[font] == nullptr) {
      hb_font_t* created = hb_font_create(font_entry->hb_face);
      if (created == hb_font_get_empty()) {
        // hb_font_create は確保に失敗すると空のフォント（不変の共有オブジェクト）を返す。
        return fail(ErrorKind::OutOfMemory,
                    std::format("cannot shape (FontId {}): cannot create the HarfBuzz font", font));
      }
      hb_ot_font_set_funcs(created);  // メトリクスは hb-ot から読む（A7）
      shaping_fonts[font] = created;
    }
    hb_font_set_scale(shaping_fonts[font], scale, scale);
    return shaping_fonts[font];
  }

  [[nodiscard]] std::vector<FamilyGroup> build_family_groups(int font_weight) const;
  [[nodiscard]] std::vector<FontId> resolve_stack(const TextStyle& style) const;
  [[nodiscard]] Result<FontMetrics> font_metrics(FontId font, float font_size);
  [[nodiscard]] Result<std::vector<hb_codepoint_t>> probe_glyphs(const FontEntry& entry,
                                                                 char32_t cp,
                                                                 hb_direction_t direction) const;
  [[nodiscard]] Result<bool> has_vertical_form(FontId font, char32_t cp);
  [[nodiscard]] Result<CharPlan> resolve_char(const std::vector<FontId>& stack, char32_t cp,
                                              bool vertical);
  [[nodiscard]] Result<std::vector<CharPlan>> build_plan(std::u32string_view text,
                                                         const std::vector<FontId>& stack,
                                                         bool vertical);

  Result<void> shape_run(std::u32string_view text, std::size_t begin, std::size_t end,
                         const CharPlan& plan, const TextStyle& style, int scale, ShapedText& out);
  Result<void> emit_missing_run(std::size_t begin, std::size_t end,
                                const std::vector<CharPlan>& plan, const std::vector<FontId>& stack,
                                const TextStyle& style, ShapedText& out);
  Result<ShapedText> shape(std::u32string_view text, const TextStyle& style);
};

// FontStore の全フォントを family 名でグループ化し、各グループの中を font_weight の
// 近い順（CSS Fonts 4 §5.2）に並べる。グループの順序は、その family の最初のフォントが
// 追加された順。太さの照合は font-family の指定と無関係に、常に全 family に効く。
std::vector<FamilyGroup> ShaperImpl::build_family_groups(int font_weight) const {
  const auto count = static_cast<FontId>(fonts->size());
  std::vector<FamilyGroup> groups;

  for (FontId font = 0; font < count; ++font) {
    std::string name = fold_family_name(fonts->family(font));
    const auto it = std::find_if(groups.begin(), groups.end(), [&name](const FamilyGroup& group) {
      return group.folded_name == name;
    });
    if (it != groups.end()) {
      it->fonts.push_back(font);
    } else {
      groups.push_back(FamilyGroup{std::move(name), {font}});
    }
  }

  for (FamilyGroup& group : groups) {
    // 安定ソートなので、同じ順位の face は追加順のまま残る。
    std::stable_sort(group.fonts.begin(), group.fonts.end(),
                     [this, font_weight](FontId a, FontId b) {
                       return weight_rank(fonts->weight(a), font_weight)
                           .better_than(weight_rank(fonts->weight(b), font_weight));
                     });
  }
  return groups;
}

std::vector<FontId> ShaperImpl::resolve_stack(const TextStyle& style) const {
  const std::vector<FamilyGroup> groups = build_family_groups(style.font_weight);

  // font-family は family（グループ）の優先順を変えるだけ。FontStore にない名前や
  // 総称ファミリは読み飛ばす（A15）。
  std::vector<std::size_t> order;
  order.reserve(groups.size());
  auto push_group = [&order](std::size_t index) {
    if (std::find(order.begin(), order.end(), index) == order.end()) {
      order.push_back(index);
    }
  };

  for (const std::string& requested : style.font_family) {
    const std::string wanted = fold_family_name(requested);
    if (wanted.empty()) {
      continue;
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
      if (groups[i].folded_name == wanted) {
        push_group(i);
        break;
      }
    }
  }
  for (std::size_t i = 0; i < groups.size(); ++i) {
    push_group(i);  // 指定を使い切ったら残りのグループを追加順で足す
  }

  std::vector<FontId> stack;
  stack.reserve(fonts->size());
  for (const std::size_t index : order) {
    for (const FontId font : groups[index].fonts) {
      stack.push_back(font);
    }
  }
  return stack;
}

Result<FontMetrics> ShaperImpl::font_metrics(FontId font, float font_size) {
  Result<hb_font_t*> hb_font = shaping_font(font, to_fixed(font_size));
  if (!hb_font) {
    return std::unexpected(hb_font.error());
  }
  hb_font_extents_t extents{};
  if (hb_font_get_h_extents(*hb_font, &extents) == 0) {
    return fail(ErrorKind::FontLoad,
                std::format("cannot read the metrics ({}): the font has no horizontal extents "
                            "(hhea / OS/2)",
                            where(font, font_size)));
  }
  FontMetrics result;
  result.ascent = std::max(0.0F, to_px(extents.ascender));
  result.descent = std::max(0.0F, -to_px(extents.descender));
  result.line_gap = std::max(0.0F, to_px(extents.line_gap));
  return result;
}

Result<std::vector<hb_codepoint_t>> ShaperImpl::probe_glyphs(const FontEntry& entry, char32_t cp,
                                                             hb_direction_t direction) const {
  const auto value = static_cast<std::uint32_t>(cp);
  hb_buffer_clear_contents(probe_buffer);
  hb_buffer_add_utf32(probe_buffer, &value, 1, 0, 1);
  hb_buffer_set_direction(probe_buffer, direction);
  hb_buffer_set_script(probe_buffer, hb_unicode_script(hb_unicode_funcs_get_default(), cp));
  hb_buffer_set_language(probe_buffer, language);
  // 見たいのはグリフ ID だけ（位置は見ない）ので、共有資源の不変フォントで shape する。
  // 不変なオブジェクトを複数スレッドから同時に shape してよいのは HarfBuzz の約束
  // （バッファの方は 1 スレッド専用なので、Shaper が自分で持っている）。A34。
  entry.font.shape_for_probe(probe_buffer);
  if (hb_buffer_allocation_successful(probe_buffer) == 0) {
    return fail(
        ErrorKind::OutOfMemory,
        std::format("cannot probe the vertical glyph (U+{:04X}): HarfBuzz could not allocate "
                    "its buffer",
                    static_cast<std::uint32_t>(cp)));
  }

  unsigned int count = 0;
  const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(probe_buffer, &count);
  if (count > 0 && infos == nullptr) {
    return fail(
        ErrorKind::Internal,
        std::format("cannot probe the vertical glyph (U+{:04X}): HarfBuzz returned no glyph info",
                    static_cast<std::uint32_t>(cp)));
  }
  std::vector<hb_codepoint_t> ids;
  ids.reserve(count);
  for (unsigned int i = 0; i < count; ++i) {
    ids.push_back(infos[i].codepoint);
  }
  return ids;
}

// UAX #50 の Tr（transformed, fallback rotated）用。フォントが縦書き用グリフ（vert）を
// 持つなら立てて差し替え、持たないなら横倒しにする、というのが UAX #50 の規定。
Result<bool> ShaperImpl::has_vertical_form(FontId font, char32_t cp) {
  const std::pair<FontId, char32_t> key{font, cp};
  if (const auto it = vertical_form_cache.find(key); it != vertical_form_cache.end()) {
    return it->second;
  }
  const FontEntry* font_entry = entry(font);
  if (font_entry == nullptr) {
    return fail(
        ErrorKind::Internal,
        std::format("cannot probe the vertical glyph (FontId {}): no such FontId in the FontStore",
                    font));
  }
  const Result<std::vector<hb_codepoint_t>> horizontal =
      probe_glyphs(*font_entry, cp, HB_DIRECTION_LTR);
  if (!horizontal) {
    return std::unexpected(horizontal.error());
  }
  const Result<std::vector<hb_codepoint_t>> vertical =
      probe_glyphs(*font_entry, cp, HB_DIRECTION_TTB);
  if (!vertical) {
    return std::unexpected(vertical.error());
  }
  const bool result = *horizontal != *vertical;
  vertical_form_cache.emplace(key, result);
  return result;
}

Result<CharPlan> ShaperImpl::resolve_char(const std::vector<FontId>& stack, char32_t cp,
                                          bool vertical) {
  CharPlan plan;
  plan.font = stack.front();
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
    case VerticalOrientation::TransformedRotated: {
      const Result<bool> upright = has_vertical_form(plan.font, cp);
      if (!upright) {
        return std::unexpected(upright.error());
      }
      plan.orientation = *upright ? Orientation::Upright : Orientation::Sideways;
      break;
    }
    case VerticalOrientation::Rotated:
      plan.orientation = Orientation::Sideways;
      break;
  }
  return plan;
}

Result<void> ShaperImpl::shape_run(std::u32string_view text, std::size_t begin, std::size_t end,
                                   const CharPlan& plan, const TextStyle& style, int scale,
                                   ShapedText& out) {
  const auto text_begin = static_cast<std::uint32_t>(begin);
  const auto text_end = static_cast<std::uint32_t>(end);
  const auto glyph_base = static_cast<std::uint32_t>(out.glyphs.size());

  // 横倒しの run は、回転後に欧文の字面が中心軸に対して中央へ来るようベースラインをずらす。
  // （ascent 側が軸の右、descent 側が軸の左に出るので、その差の半分だけ戻す）
  float baseline_shift = 0.0F;
  if (plan.orientation == Orientation::Sideways) {
    const Result<FontMetrics> run_metrics = font_metrics(plan.font, style.font_size);
    if (!run_metrics) {
      return std::unexpected(run_metrics.error());
    }
    baseline_shift = (run_metrics->descent - run_metrics->ascent) / 2.0F;
  }

  const Result<hb_font_t*> hb_font = shaping_font(plan.font, scale);
  if (!hb_font) {
    return std::unexpected(hb_font.error());
  }
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
  hb_shape(*hb_font, buffer, nullptr, 0);
  // hb_buffer_create 自体の失敗もここで捕まる（確保に失敗すると successful = false の
  // 空のバッファが返るため、コンストラクタが Result を返せなくても握りつぶさずに済む）。
  if (hb_buffer_allocation_successful(buffer) == 0) {
    return fail(
        ErrorKind::OutOfMemory,
        std::format("cannot shape ({}): HarfBuzz could not allocate its buffer ({} code points)",
                    where(plan.font, style.font_size), end - begin));
  }

  unsigned int count = 0;
  const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
  const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
  if (count > 0 && (infos == nullptr || positions == nullptr)) {
    return fail(ErrorKind::Internal,
                std::format("cannot shape ({}): HarfBuzz returned no glyph info ({} glyphs)",
                            where(plan.font, style.font_size), count));
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
    return {};
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
  return {};
}

// 豆腐の run。どの文字が豆腐だったかは ShapedCluster::missing で返すだけで、
// Shaper 自身は何も溜めない（A31。警告を組み立てるのは ③ レイアウトの仕事）。
Result<void> ShaperImpl::emit_missing_run(std::size_t begin, std::size_t end,
                                          const std::vector<CharPlan>& plan,
                                          const std::vector<FontId>& stack, const TextStyle& style,
                                          ShapedText& out) {
  // 豆腐は □（U+25A1）をフォールバック列の順に探し、最初に見つかったフォントのグリフで描く。
  // 第一フォントだけを見ると、欧文フォントが先頭のときに幅の狭い .notdef が 1em の枠の
  // 左端に出て不揃いになる。どのフォントにも □ が無いときだけ第一フォントの .notdef。
  FontId font = stack.front();
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
  float ascent = 0.0F;
  if (vertical) {
    const Result<FontMetrics> tofu_metrics = font_metrics(font, style.font_size);
    if (!tofu_metrics) {
      return std::unexpected(tofu_metrics.error());
    }
    ascent = tofu_metrics->ascent;
  }

  for (std::size_t i = begin; i < end; ++i) {
    if (i > begin && plan[i].attached) {
      out.clusters.back().text_end = static_cast<std::uint32_t>(i + 1);
      continue;
    }
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
  return {};
}

// コードポイントごとにフォント・向き・スクリプトを決める。
Result<std::vector<CharPlan>> ShaperImpl::build_plan(std::u32string_view text,
                                                     const std::vector<FontId>& stack,
                                                     bool vertical) {
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
    Result<CharPlan> resolved = resolve_char(stack, cp, vertical);
    if (!resolved) {
      return std::unexpected(resolved.error());
    }
    plan[i] = *resolved;
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

// 呼び出し側の契約（text_measurer.hpp）を確かめる。破られていたら黙って空を返さずに
// Internal で落とす（フォントが 1 つも無い FontStore は api が NoFonts で弾いている）。
Result<void> check_contract(const std::vector<FontId>& stack, const TextStyle& style) {
  if (stack.empty()) {
    return fail(ErrorKind::Internal, "cannot shape: no fonts are loaded in the FontStore");
  }
  if (!std::isfinite(style.font_size)) {
    // 値の表記は number_text() を通す（NaN の符号は CPU で変わる。core/number_text.hpp）。
    return fail(ErrorKind::Internal, std::format("cannot shape: font_size must be finite (got {})",
                                                 number_text(style.font_size)));
  }
  // 負も契約違反（issue #26）。style が `font-size: -16px` を止めているので入力からは
  // 到達しないが、注入点の契約（text_measurer.hpp）を実装でも守る。`0` は CSS 上
  // 有効なので通す（`-0` は `0` と等しいのでここも通る）。
  if (style.font_size < 0) {
    return fail(ErrorKind::Internal,
                std::format("cannot shape: font_size must be non-negative (got {})",
                            number_text(style.font_size)));
  }
  return {};
}

}  // namespace

Result<ShapedText> ShaperImpl::shape(std::u32string_view text, const TextStyle& style) {
  const std::vector<FontId> stack = resolve_stack(style);
  if (const Result<void> ok = check_contract(stack, style); !ok) {
    return std::unexpected(ok.error());
  }
  ShapedText out;
  if (text.empty()) {
    return out;  // 「正常に 0 グリフ」。失敗と区別する（text_measurer.hpp）
  }

  const int scale = to_fixed(style.font_size);
  const Result<std::vector<CharPlan>> plan =
      build_plan(text, stack, style.direction == Direction::Vertical);
  if (!plan) {
    return std::unexpected(plan.error());
  }

  // 同じ run が続く区間ごとにシェーピングする。
  std::size_t begin = 0;
  while (begin < text.size()) {
    std::size_t end = begin + 1;
    while (end < text.size() && (*plan)[end].same_run_as((*plan)[begin])) {
      ++end;
    }
    const Result<void> done = (*plan)[begin].missing
                                  ? emit_missing_run(begin, end, *plan, stack, style, out)
                                  : shape_run(text, begin, end, (*plan)[begin], style, scale, out);
    if (!done) {
      return std::unexpected(done.error());
    }
    begin = end;
  }

  out.clusters = join_attached_clusters(text, *plan, out.clusters);
  normalize_clusters(out, text.size());
  return out;
}

}  // namespace detail

Shaper::Shaper(const FontStore& fonts) : impl_(std::make_unique<detail::ShaperImpl>(fonts)) {}
Shaper::~Shaper() = default;

Result<ShapedText> Shaper::shape(std::u32string_view text, const TextStyle& style) {
  return impl_->shape(text, style);
}

Result<FontMetrics> Shaper::metrics(const TextStyle& style) {
  const std::vector<FontId> stack = impl_->resolve_stack(style);
  if (const Result<void> ok = detail::check_contract(stack, style); !ok) {
    return std::unexpected(ok.error());
  }
  return impl_->font_metrics(stack.front(), style.font_size);
}

}  // namespace shashoku::text
