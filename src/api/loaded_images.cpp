#include "shashoku/loaded_images.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/loaded_resources.hpp"
#include "api/out_of_memory.hpp"
#include "core/bitmap.hpp"
#include "core/result.hpp"
#include "png/png.hpp"
#include "shashoku/error.hpp"
#include "shashoku/image_set.hpp"
#include "shashoku/limits.hpp"

// デコード済みの画像（ARCHITECTURE.md A33）。デコードして得た画素はただの値なので、
// 構築が終われば読み取り専用の共有資源になる。
namespace shashoku {

namespace {

// 名前の重複はバイト列を見る前に弾く（ImageSet の形の問題で、中身の問題ではない）。
Result<void> check_unique_names(const ImageSet& images) {
  for (std::size_t i = 0; i < images.size(); ++i) {
    for (std::size_t seen = 0; seen < i; ++seen) {
      if (images.name(seen) == images.name(i)) {
        return fail(ErrorKind::InvalidOption,
                    std::format("image \"{}\" was added to the ImageSet twice", images.name(i)));
      }
    }
  }
  return {};
}

// (c) 大きな確保の直前（A25）。png::decode は IHDR を読んだ時点（画素を確保する前）に
// 判定するので、「巨大な寸法を名乗るだけの小さな PNG」でもメモリは 1 バイトも増えない。
// 1 枚あたりの上限と合計の上限のうち、いま効いている方をそのまま渡す。
Result<std::vector<Bitmap>> decode_all(const ImageSet& images, const RenderLimits& limits) {
  std::vector<Bitmap> decoded;
  decoded.reserve(images.size());
  std::uint64_t total_pixels = 0;
  for (std::size_t i = 0; i < images.size(); ++i) {
    const std::string_view name = images.name(i);
    const std::uint64_t remaining =
        limits.total_image_pixels > total_pixels ? limits.total_image_pixels - total_pixels : 0;
    const bool per_image_binds = limits.image_pixels <= remaining;
    const std::uint64_t cap = per_image_binds ? limits.image_pixels : remaining;

    Result<Bitmap> bitmap = png::decode(images.bytes(i), cap);
    if (!bitmap) {
      if (bitmap.error().kind == ErrorKind::LimitExceeded) {
        return fail(ErrorKind::LimitExceeded,
                    std::format("image \"{}\": {} (raise RenderLimits::{} to allow it)", name,
                                bitmap.error().message,
                                per_image_binds ? "image_pixels" : "total_image_pixels"));
      }
      return fail(ErrorKind::ImageDecode,
                  std::format("image \"{}\": {}", name, bitmap.error().message));
    }
    total_pixels += std::uint64_t{bitmap->width} * bitmap->height;
    decoded.push_back(std::move(*bitmap));
  }
  return decoded;
}

}  // namespace

struct LoadedImages::Impl {
  std::vector<Bitmap> images;      // 添字がそのまま ImageId
  std::vector<std::string> names;  // images と同じ長さ・同じ順
};

LoadedImages::LoadedImages(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
LoadedImages::LoadedImages(LoadedImages&&) noexcept = default;
LoadedImages& LoadedImages::operator=(LoadedImages&&) noexcept = default;
LoadedImages::~LoadedImages() = default;

std::size_t LoadedImages::size() const noexcept {
  return impl_ != nullptr ? impl_->images.size() : 0;
}

std::expected<LoadedImages, RenderError> LoadedImages::prepare(const ImageSet& images,
                                                               const RenderLimits& limits) {
  return detail::catch_out_of_memory<LoadedImages>(
      [&images, &limits]() -> std::expected<LoadedImages, RenderError> {
        if (const Result<void> ok = check_unique_names(images); !ok) {
          return std::unexpected(ok.error());
        }
        Result<std::vector<Bitmap>> decoded = decode_all(images, limits);
        if (!decoded) {
          return std::unexpected(decoded.error());
        }
        auto impl = std::make_unique<Impl>();
        impl->images = std::move(*decoded);
        impl->names.reserve(images.size());
        for (std::size_t i = 0; i < images.size(); ++i) {
          impl->names.emplace_back(images.name(i));
        }
        return LoadedImages(std::move(impl));
      });
}

namespace detail {

ImageTable LoadedImagesAccess::table(const LoadedImages& loaded) noexcept {
  if (loaded.impl_ == nullptr) {
    return ImageTable{};
  }
  return ImageTable{.images = &loaded.impl_->images, .names = &loaded.impl_->names};
}

}  // namespace detail
}  // namespace shashoku
