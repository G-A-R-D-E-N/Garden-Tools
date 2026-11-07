#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::textures {

// Decode a DDS container into RGBA8. `width`/`height` receive the image
// dimensions. `reconstructZ` rebuilds the blue channel from R/G (BC5 normal
// maps). Errors: "InvalidDds", "UnsupportedFormat", "TooLarge".
Result<std::vector<std::uint8_t>> decodeDds(std::span<const std::uint8_t> ddsBytes,
                                            int& width,
                                            int& height,
                                            bool reconstructZ = false);

// Decode a DDS container to a PNG (RGBA, 8-bit). Errors: same as decodeDds.
Result<std::vector<std::uint8_t>> ddsToPng(std::span<const std::uint8_t> ddsBytes,
                                           bool reconstructZ = false);

// True when the container is a BC5 normal map (DXGI 82/83/84).
bool isBc5(std::span<const std::uint8_t> ddsBytes) noexcept;

}  // namespace fo4ide::textures
