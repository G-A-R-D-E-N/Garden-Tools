#pragma once

#include <cstdint>

namespace fo4::texture {

// Constraint: DXGI has no 24-bit formats; these ids are private to the decoder and must never reach a BA2 entry or a DX10 header.
inline constexpr std::uint8_t k_dxgi_bgr8 = 0xF0;
inline constexpr std::uint8_t k_dxgi_rgb8 = 0xF1;

}
