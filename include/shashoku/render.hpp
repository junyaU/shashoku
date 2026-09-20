#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "shashoku/error.hpp"
#include "shashoku/font_set.hpp"
#include "shashoku/image_set.hpp"
#include "shashoku/options.hpp"
#include "shashoku/warning.hpp"

namespace shashoku {

struct RenderResult {
  std::vector<std::uint8_t> png;  // PNG バイト列（RGBA8 / 非インターレース）
  std::vector<Warning> warnings;  // 豆腐など、続行できた問題。コードポイント昇順
  int width = 0;   // 出力画像の幅（デバイスピクセル = ceil(CSS px * scale)）
  int height = 0;  // 同・高さ
};

// 唯一のエントリポイント（DESIGN.md §8）。**純粋関数**: 同じ入力からは常にバイト単位で
// 同じ PNG が出る。グローバル状態・時刻・乱数・ネットワーク・ロケールに触れない。
//
// 出力の大きさ: 幅 = opts.viewport_width、高さ = opts.viewport_height。
// viewport_height を指定しなければ内容の高さに追従する（内容が空なら InvalidOption）。
// どちらも scale を掛けて切り上げたものがデバイスピクセル数になる。
//
// エラー（ErrorKind）は fail loudly の原則どおり、原因の入力位置つきで返る:
//   InvalidUtf8 / HtmlParse / UnsupportedTag / UnsupportedAttribute … ① HTML
//   CssParse / UnsupportedProperty / UnsupportedValue / UnsupportedLayout … ② スタイル
//   NoFonts / FontLoad … フォント、ImageDecode / ImageNotFound … 画像
//   InvalidOption … RenderOptions の値が不正
//   LimitExceeded … 入力が opts.limits の上限を超えた（limits.hpp）
//   OutOfMemory … メモリを確保できなかった（最善努力。ARCHITECTURE.md A26）
//
// 信頼できない HTML を受けるときは `opts.limits` で予算を決める（limits.hpp）。既定値でも
// 事故は止まるが、無制限ではない。OutOfMemory は保証ではなく最後の網であることに注意。
std::expected<RenderResult, RenderError> render(std::string_view html, const FontSet& fonts,
                                                const RenderOptions& opts = {});

// 画像つき（ARCHITECTURE.md A12）。`<img src="名前">` は images から引く。
std::expected<RenderResult, RenderError> render(std::string_view html, const FontSet& fonts,
                                                const ImageSet& images,
                                                const RenderOptions& opts = {});

// 中間表現のダンプ（DESIGN.md §3-3）。CLI の --dump-stage に対応する。
enum class DumpStage : std::uint8_t {
  Dom,          // ① DOM（JSON）
  Style,        // ② スタイル付きツリー（JSON）
  Box,          // ③ ボックスツリー（JSON）
  DisplayList,  // ⑤a ディスプレイリスト（JSON）
  Svg,          // ⑤a ディスプレイリスト（SVG。グリフは印で代用する）
};

// 指定した段までしか実行しない。Dom / Style はフォントを見ないので、
// 空の FontSet でも成功する（HTML と CSS だけを確かめたいとき用）。
std::expected<std::string, RenderError> dump(std::string_view html, const FontSet& fonts,
                                             const ImageSet& images, const RenderOptions& opts,
                                             DumpStage stage);

// "box" のようなケバブケースの識別子（CLI の --dump-stage の値と同じ綴り）
std::string_view to_string(DumpStage stage) noexcept;

}  // namespace shashoku
