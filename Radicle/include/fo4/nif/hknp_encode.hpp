#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <cstdint>

namespace fo4::nif {

// Constraint: one hknp metre is this many NIF game units.
inline constexpr float k_hknp_game_units_per_metre = 69.99125f;

struct HknpStaticBody {
  HknpVec3 position{};
  HknpVec4 orientation{{0, 0, 0, 1}};
  std::uint32_t packed_filter{1};
};

// Constraint: half extents must be finite and positive, otherwise std::invalid_argument is raised.
HknpShape make_hknp_box(const HknpVec3& center, const HknpVec3& half_extents, std::uint32_t material_crc);

// Requires a sphere, capsule, or convex shape; other classes and non-finite geometry raise std::invalid_argument.
HknpPackfile author_hknp_static_body(const HknpShape& shape, const HknpStaticBody& body);

}  // namespace fo4::nif
