#pragma once

#include <string_view>

namespace shashoku {

// ライブラリのバージョン文字列（例: "0.1.0"）。CMake の project(VERSION) と同期している。
std::string_view version() noexcept;

}  // namespace shashoku
