#include "bc_encode_blocks.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::texture {

// Invariant: BC1 is the 8-byte colour block only, so any alpha in the input is dropped and the block is opaque.
std::vector<std::uint8_t> encode_bc1(std::span<const std::uint8_t> rgba, int width, int height) {
  return bc_encode::encode_blocks<8>(rgba, width, height, "BC1", [](const bc_encode::Block& block) {
    return bc_encode::colour_block(block);
  });
}

// Invariant: BC4 is one 8-byte alpha-style block over the red channel.
std::vector<std::uint8_t> encode_bc4(std::span<const std::uint8_t> rgba, int width, int height) {
  return bc_encode::encode_blocks<8>(rgba, width, height, "BC4", [](const bc_encode::Block& block) {
    return bc_encode::alpha_block(block.red);
  });
}

// Invariant: BC5 stores the red block in bytes 0-7 and the green block in bytes 8-15.
std::vector<std::uint8_t> encode_bc5(std::span<const std::uint8_t> rgba, int width, int height) {
  return bc_encode::encode_blocks<16>(rgba, width, height, "BC5", [](const bc_encode::Block& block) {
    std::array<std::uint8_t, 16> out{};
    const auto red = bc_encode::alpha_block(block.red);
    const auto green = bc_encode::alpha_block(block.green);
    std::copy(red.begin(), red.end(), out.begin());
    std::copy(green.begin(), green.end(), out.begin() + 8);
    return out;
  });
}

}  // namespace fo4::texture
