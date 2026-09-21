#include "embedded.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// バイト列はアセンブラの `.incbin` でそのまま .rodata に置く（A-new-1）。
// C の配列初期化子に展開する方法だと、9 MB の OTF 2 本が 50 MB を超えるソースになり
// コンパイルが現実的でない。`.incbin` は GNU as と clang の統合アセンブラの両方が
// 持っている（対応環境は linux-x86_64 のみ。CI の 2 ツールチェーンで検査する）。
//
// `.pushsection` / `.popsection` で .rodata に出入りし、直前のセクションを壊さない。
// 局所ラベル `9:` は各ブロックの中で閉じているので、ブロック間で衝突しない。

extern "C" {
// NOLINTBEGIN(modernize-avoid-c-arrays): アセンブラが置いた記号を指すので、
// 大きさの無い配列で受けるしかない（std::array にはできない）。
#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
extern const std::uint8_t kShashokuDefaultFontRegularBegin[];
extern const std::uint8_t kShashokuDefaultFontBoldBegin[];
#endif
extern const char kShashokuLicenseBegin[];
extern const char kShashokuThirdPartyLicenseBegin[];
// NOLINTEND(modernize-avoid-c-arrays)

#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
extern const std::uint64_t kShashokuDefaultFontRegularSize;
extern const std::uint64_t kShashokuDefaultFontBoldSize;
#endif
extern const std::uint64_t kShashokuLicenseSize;
extern const std::uint64_t kShashokuThirdPartyLicenseSize;
}  // extern "C"

#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
asm(".pushsection .rodata\n"
    ".balign 8\n"
    ".globl kShashokuDefaultFontRegularBegin\n"
    "kShashokuDefaultFontRegularBegin:\n"
    ".incbin \"" SHASHOKU_DEFAULT_FONT_REGULAR
    "\"\n"
    "9:\n"
    ".balign 8\n"
    ".globl kShashokuDefaultFontRegularSize\n"
    "kShashokuDefaultFontRegularSize:\n"
    ".quad 9b - kShashokuDefaultFontRegularBegin\n"
    ".popsection\n");

asm(".pushsection .rodata\n"
    ".balign 8\n"
    ".globl kShashokuDefaultFontBoldBegin\n"
    "kShashokuDefaultFontBoldBegin:\n"
    ".incbin \"" SHASHOKU_DEFAULT_FONT_BOLD
    "\"\n"
    "9:\n"
    ".balign 8\n"
    ".globl kShashokuDefaultFontBoldSize\n"
    "kShashokuDefaultFontBoldSize:\n"
    ".quad 9b - kShashokuDefaultFontBoldBegin\n"
    ".popsection\n");
#endif

asm(".pushsection .rodata\n"
    ".balign 8\n"
    ".globl kShashokuLicenseBegin\n"
    "kShashokuLicenseBegin:\n"
    ".incbin \"" SHASHOKU_LICENSE_FILE
    "\"\n"
    "9:\n"
    ".balign 8\n"
    ".globl kShashokuLicenseSize\n"
    "kShashokuLicenseSize:\n"
    ".quad 9b - kShashokuLicenseBegin\n"
    ".popsection\n");

asm(".pushsection .rodata\n"
    ".balign 8\n"
    ".globl kShashokuThirdPartyLicenseBegin\n"
    "kShashokuThirdPartyLicenseBegin:\n"
    ".incbin \"" SHASHOKU_THIRD_PARTY_LICENSE_FILE
    "\"\n"
    "9:\n"
    ".balign 8\n"
    ".globl kShashokuThirdPartyLicenseSize\n"
    "kShashokuThirdPartyLicenseSize:\n"
    ".quad 9b - kShashokuThirdPartyLicenseBegin\n"
    ".popsection\n");

namespace shashoku::cli {

bool has_default_font() noexcept {
#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
  return true;
#else
  return false;
#endif
}

std::span<const std::uint8_t> default_font_regular() noexcept {
#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
  return {kShashokuDefaultFontRegularBegin,
          static_cast<std::size_t>(kShashokuDefaultFontRegularSize)};
#else
  return {};
#endif
}

std::span<const std::uint8_t> default_font_bold() noexcept {
#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
  return {kShashokuDefaultFontBoldBegin, static_cast<std::size_t>(kShashokuDefaultFontBoldSize)};
#else
  return {};
#endif
}

std::string_view default_font_version() noexcept {
#if defined(SHASHOKU_EMBED_DEFAULT_FONT)
  return SHASHOKU_DEFAULT_FONT_VERSION;
#else
  return "(not embedded)";
#endif
}

std::string_view license_text() noexcept {
  return {kShashokuLicenseBegin, static_cast<std::size_t>(kShashokuLicenseSize)};
}

std::string_view third_party_license_text() noexcept {
  return {kShashokuThirdPartyLicenseBegin, static_cast<std::size_t>(kShashokuThirdPartyLicenseSize)};
}

}  // namespace shashoku::cli
