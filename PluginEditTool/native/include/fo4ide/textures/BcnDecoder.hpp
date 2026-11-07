#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::textures {

// True when the given DXGI format can be decoded in process by
// decodeBlockFormat (BC1-5, BC7; BC6H is not decodable here).
bool canDecodeBlockFormat(std::uint8_t dxgi) noexcept;

// Decode a BCn-compressed surface (one or more 4x4 blocks, row-major) into
// RGBA8. `width`/`height` are the image dimensions; both are rounded up to
// the block grid, out-of-bounds texels are skipped. Errors: "InvalidData",
// "UnsupportedFormat".
Result<std::vector<std::uint8_t>> decodeBlockFormat(std::span<const std::uint8_t> surface,
                                                    std::uint8_t dxgi,
                                                    int width,
                                                    int height);

}  // namespace fo4ide::textures
