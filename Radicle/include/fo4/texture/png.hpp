#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace fo4::texture {

// Requires rgba.size() == width * height * 4; throws std::invalid_argument otherwise and std::runtime_error when zlib fails.
std::vector<std::uint8_t> encode_png_rgba(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

}  // namespace fo4::texture
