#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::textures {

// Encode RGBA8 into a PNG (8-bit, color type 6, filter 0 rows, zlib level 1 —
// matches the C# PngWriter's Fastest level). Errors: "InvalidArgument",
// "PngEncodeFailed".
Result<std::vector<std::uint8_t>> writePng(std::span<const std::uint8_t> rgba,
                                           int width,
                                           int height);

}  // namespace fo4ide::textures
