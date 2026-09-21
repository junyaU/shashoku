// CLI のバイナリに焼き込んだデータ（既定フォントとライセンス文）。#20 / A-new-1。
//
// **これは CLI 層だけの機能**。ライブラリ（render() / dump()）は今までどおりバイト列しか
// 受け取らず、公開ヘッダ（include/shashoku/）も変わらない。フォントの探索も実行時の
// ダウンロードもしない（DESIGN.md §3-5 の純粋関数、§4 の「外部リソースを取りに行かない」）。
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace shashoku::cli {

// 既定フォントが埋め込まれているか（SHASHOKU_EMBED_DEFAULT_FONT=OFF なら false）。
[[nodiscard]] bool has_default_font() noexcept;

// 既定フォントのバイト列。埋め込みが無ければ空。
// **順序に意味がある**: FontSet には Regular → Bold の順で入れる。`--font` を明示した
// ときと同じ絵を出すための約束で、tools/shashoku/cli_test.cmake が検査する。
[[nodiscard]] std::span<const std::uint8_t> default_font_regular() noexcept;
[[nodiscard]] std::span<const std::uint8_t> default_font_bold() noexcept;

// `--version` が出す既定フォントの版（cmake/TestAssets.cmake のコミット SHA）。
[[nodiscard]] std::string_view default_font_version() noexcept;

// `--license` が出す文面（リポジトリ直下の LICENSE と THIRD_PARTY_LICENSES）。
[[nodiscard]] std::string_view license_text() noexcept;
[[nodiscard]] std::string_view third_party_license_text() noexcept;

}  // namespace shashoku::cli
