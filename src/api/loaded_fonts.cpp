#include "shashoku/loaded_fonts.hpp"

#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <utility>

#include "api/loaded_resources.hpp"
#include "api/out_of_memory.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "shashoku/error.hpp"
#include "shashoku/font_set.hpp"
#include "text/font_store.hpp"

// 解釈済みのフォント（ARCHITECTURE.md A34）。FontSet のバイト列を一度だけ FontStore に
// 読み込んで持つだけの薄い入れ物で、可変の状態は持たない。FreeType のハンドルを持たない
// ことが「何スレッドから同時に読んでもよい」の根拠（text/font_store.hpp）。
namespace shashoku {

struct LoadedFonts::Impl {
  text::FontStore fonts;
};

LoadedFonts::LoadedFonts(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
LoadedFonts::LoadedFonts(LoadedFonts&&) noexcept = default;
LoadedFonts& LoadedFonts::operator=(LoadedFonts&&) noexcept = default;
LoadedFonts::~LoadedFonts() = default;

std::size_t LoadedFonts::size() const noexcept {
  return impl_ != nullptr ? impl_->fonts.size() : 0;
}

std::expected<LoadedFonts, RenderError> LoadedFonts::prepare(const FontSet& fonts) {
  return detail::catch_out_of_memory<LoadedFonts>(
      [&fonts]() -> std::expected<LoadedFonts, RenderError> {
        if (fonts.empty()) {
          return fail(ErrorKind::NoFonts,
                      "no fonts were given: add at least one font to the FontSet");
        }
        auto impl = std::make_unique<Impl>();
        for (std::size_t i = 0; i < fonts.size(); ++i) {
          const Result<FontId> id = impl->fonts.load(fonts.at(i));
          if (!id) {
            return fail(ErrorKind::FontLoad, std::format("font #{}: {}", i, id.error().message));
          }
        }
        return LoadedFonts(std::move(impl));
      });
}

namespace detail {

const text::FontStore* LoadedFontsAccess::fonts(const LoadedFonts& loaded) noexcept {
  return loaded.impl_ != nullptr ? &loaded.impl_->fonts : nullptr;
}

}  // namespace detail
}  // namespace shashoku
