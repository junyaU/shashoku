#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "core/result.hpp"

namespace shashoku::text {

// ③ レイアウト ⇄ ④ シェーピングの絶縁層（DESIGN.md §3-4）。
// レイアウトはこのインターフェースだけを知り、FreeType / HarfBuzz / フォントを知らない。
// レイアウトのテストは「全角 = 1em、半角 = 0.5em」を返す偽物を注入して、フォントなしで書く。

enum class Direction : std::uint8_t { Horizontal, Vertical };

// シェーピングに効くスタイルだけを抜き出したもの。
// letter-spacing はここに含めない（クラスタの送りに足すのはレイアウトの仕事）。
struct TextStyle {
  std::vector<std::string> font_family;  // 優先順。総称ファミリや FontStore にない名前は読み飛ばす
  int font_weight = 400;
  // px。**非負かつ有限**（`0` は CSS Fonts 4 §2.5 で有効なので通す）。破ると Internal
  float font_size = 16;
  Direction direction = Direction::Horizontal;

  bool operator==(const TextStyle&) const = default;
};

// 座標の約束:
//   「ペン位置」は、横書きでは欧文ベースライン上の点、縦書きでは行の中心軸上の点
//   （字送り方向の現在位置）。グリフ原点 = ペン位置 + (x_offset, y_offset)。
//   オフセットは物理座標（+x 右、+y 下）の px。HarfBuzz の y 上向きは ④ の中で反転しておく。
struct ShapedGlyph {
  FontId font = 0;
  GlyphId glyph_id = 0;
  float advance = 0;  // 字送り方向（横書き: 右、縦書き: 下）の送り。px
  float x_offset = 0;
  float y_offset = 0;
  bool sideways = false;  // 縦書き中の横倒し（欧文・数字）。時計回りに 90° 回して描く

  bool operator==(const ShapedGlyph&) const = default;
};

// 行分割の最小単位。1 クラスタ = 分割してはいけない文字のまとまり
// （基底文字 + 結合文字、異体字セレクタ付き漢字、合字、サロゲート相当の 1 文字など）。
struct ShapedCluster {
  std::uint32_t text_begin = 0;  // 入力 UTF-32 列の [text_begin, text_end)
  std::uint32_t text_end = 0;
  std::uint32_t glyph_begin = 0;  // ShapedText::glyphs の [glyph_begin, glyph_end)
  std::uint32_t glyph_end = 0;
  float advance = 0;     // クラスタ内グリフの advance の合計
  bool missing = false;  // どのフォントにもグリフがなかった（豆腐）

  bool operator==(const ShapedCluster&) const = default;
};

// clusters は入力の論理順で、入力全体を隙間なく覆う（text_begin は単調増加）。
// glyphs は描画順（LTR のみ対応なので論理順と一致する）。
struct ShapedText {
  std::vector<ShapedGlyph> glyphs;
  std::vector<ShapedCluster> clusters;
};

// font_size にスケール済みのメトリクス。px。どちらも正の値。
struct FontMetrics {
  float ascent = 0;   // ベースラインから上端まで
  float descent = 0;  // ベースラインから下端まで
  float line_gap = 0;  // フォント推奨の行間（line-height: normal = ascent + descent + line_gap）
};

class TextMeasurer {
 public:
  TextMeasurer() = default;
  TextMeasurer(const TextMeasurer&) = delete;
  TextMeasurer& operator=(const TextMeasurer&) = delete;
  // ムーブも禁止（コピー禁止と揃える）。注入点なので参照でしか受け渡さない。
  TextMeasurer(TextMeasurer&&) = delete;
  TextMeasurer& operator=(TextMeasurer&&) = delete;
  virtual ~TextMeasurer() = default;

  // text は改行を含まない 1 区間。フォールバックによるフォント切り替えは実装の中で処理する
  // （呼び出し側は「同一フォントの run」を意識しない）。
  //
  // **失敗は必ずエラーで返す**（issue #3 / A30。値返しの契約では「測れなかった」を
  // 空の結果としてしか表せず、文字の欠けた PNG が「成功」として出てしまう）。
  // 成功して空の ShapedText を返してよいのは **本当にグリフが 0 個のとき**だけ
  // （空文字列。入力が空でなければ clusters は必ず入力全体を覆う）。
  //   Internal     … 呼び出し側の契約違反（フォントを 1 つも持たない、負または非有限の
  //                   font_size。`font_size == 0` は有効。issue #26 / A36）
  //   FontLoad     … フォントからメトリクス / グリフを読めない
  //   OutOfMemory  … シェーピングエンジンが作業領域を確保できなかった
  // 豆腐（どのフォントにもグリフがない文字）はエラーではない。□ を返し、
  // `ShapedCluster::missing` を立てる（警告にするのは呼び出し側の仕事。DESIGN.md §6-6）。
  virtual Result<ShapedText> shape(std::u32string_view text, const TextStyle& style) = 0;

  // スタックの先頭で解決されるフォント（＝その要素の第一フォント）のメトリクス。
  // エラーの種類は shape() と同じ。
  virtual Result<FontMetrics> metrics(const TextStyle& style) = 0;
};

}  // namespace shashoku::text
