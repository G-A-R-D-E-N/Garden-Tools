#include "bc_encode_blocks.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::texture {

// Invariant: BC3 stores the alpha block first and the colour block second, 16 bytes per 4x4 block.
std::vector<std::uint8_t> encode_bc3(std::span<const std::uint8_t> rgba, int width, int height) {
  return bc_encode::encode_blocks<16>(rgba, width, height, "BC3", [](const bc_encode::Block& block) {
    std::array<std::uint8_t, 16> out{};
    const auto alpha = bc_encode::alpha_block(block.alpha);
    const auto colour = bc_encode::colour_block(block);
    std::copy(alpha.begin(), alpha.end(), out.begin());
    std::copy(colour.begin(), colour.end(), out.begin() + 8);
    return out;
  });
}

}  // namespace fo4::texture
