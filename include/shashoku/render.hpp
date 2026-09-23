#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "shashoku/error.hpp"
#include "shashoku/font_set.hpp"
#include "shashoku/image_set.hpp"
#include "shashoku/loaded_fonts.hpp"
#include "shashoku/loaded_images.hpp"
#include "shashoku/options.hpp"
#include "shashoku/warning.hpp"

namespace shashoku {

struct RenderResult {
  std::vector<std::uint8_t> png;  // PNG バイト列（RGBA8 / 非インターレース）
  // 豆腐など、続行できた問題。並びは入力位置の昇順 → コードポイントの昇順（決定的）。
  // 同じ (コードポイント, テキストノード) の組は 1 件にまとまる（ARCHITECTURE.md A31）
  std::vector<Warning> warnings;
  // warnings が `RenderLimits::max_diagnostics` に達して記録を打ち切った（ARCHITECTURE.md A46）
  bool diagnostics_truncated = false;
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
// 失敗は RenderFailure（ARCHITECTURE.md A46）。①（HTML）と②（スタイル）の段は見つけた問題を
// 集めてから失敗し、errors に全部入れる（入力位置の昇順）。③ 以降は 1 件目で止まる:
//   InvalidUtf8 / HtmlParse / UnsupportedTag / UnsupportedAttribute … ① HTML
//   CssParse / UnsupportedProperty / UnsupportedValue / UnsupportedLayout … ② スタイル
//   NoFonts / FontLoad … フォント、ImageDecode / ImageNotFound … 画像
//   InvalidOption … RenderOptions の値が不正
//   LimitExceeded … 入力が opts.limits の上限を超えた（limits.hpp）
//   OutOfMemory … メモリを確保できなかった（最善努力。ARCHITECTURE.md A26）
//   WarningAsError … opts.warnings_as_errors のとき、警告（豆腐・はみ出し）を格上げしたもの
// errors と warnings の合計は opts.limits.max_diagnostics で打ち切られる（truncated）。
//
// 信頼できない HTML を受けるときは `opts.limits` で予算を決める（limits.hpp）。既定値でも
// 事故は止まるが、無制限ではない。OutOfMemory は保証ではなく最後の網であることに注意。
std::expected<RenderResult, RenderFailure> render(std::string_view html, const FontSet& fonts,
                                                const RenderOptions& opts = {});

// 画像つき（ARCHITECTURE.md A12）。`<img src="名前">` は images から引く。
std::expected<RenderResult, RenderFailure> render(std::string_view html, const FontSet& fonts,
                                                const ImageSet& images,
                                                const RenderOptions& opts = {});

// 用意済みの共有資源を使う（ARCHITECTURE.md A34）。連続生成では `FontSet` / `ImageSet` を
// 毎回解釈し直す代わりに、`LoadedFonts::prepare()` / `LoadedImages::prepare()` の結果を
// 使い回す。**出力は上のオーバーロードとバイト単位で同じ**で、同じ共有資源を複数の
// スレッドから同時に渡してもよい（loaded_fonts.hpp の「スレッドの約束」）。
//
// `opts.limits` は用意し直した資源にも効く: `prepare()` に渡した `RenderLimits` と
// ここの `opts.limits` が違っても、両方の検査を通ったものだけが描かれる。
// ムーブ済みの `LoadedFonts` / `LoadedImages` を渡すと `InvalidOption`。
std::expected<RenderResult, RenderFailure> render(std::string_view html, const LoadedFonts& fonts,
                                                const RenderOptions& opts = {});

std::expected<RenderResult, RenderFailure> render(std::string_view html, const LoadedFonts& fonts,
                                                const LoadedImages& images,
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
std::expected<std::string, RenderFailure> dump(std::string_view html, const FontSet& fonts,
                                             const ImageSet& images, const RenderOptions& opts,
                                             DumpStage stage);

// 用意済みの共有資源を使う dump（A34）。出力は上の dump() と文字単位で同じ。
std::expected<std::string, RenderFailure> dump(std::string_view html, const LoadedFonts& fonts,
                                             const LoadedImages& images, const RenderOptions& opts,
                                             DumpStage stage);

// "box" のようなケバブケースの識別子（CLI の --dump-stage の値と同じ綴り）
std::string_view to_string(DumpStage stage) noexcept;

}  // namespace shashoku
