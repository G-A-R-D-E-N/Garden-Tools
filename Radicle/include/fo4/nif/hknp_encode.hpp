#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

#include <cstdint>
#include <vector>

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

// Constraint: a compound's children are placed by instance, so each child's scale is its own.
struct HknpCompoundChild {
  HknpShape shape;
  HknpInstance placement;
};

// Requires at least one child, at most 4096, and a supported child shape; anything else raises std::invalid_argument.
HknpPackfile author_hknp_compound_body(const std::vector<HknpCompoundChild>& children, const HknpStaticBody& body);

struct HknpSystemBody {
  HknpShape shape;
  // Constraint: non-empty children author a dynamic compound shape, and shape is then unused.
  std::vector<HknpCompoundChild> children;
  HknpVec3 position{};
  HknpVec4 orientation{{0, 0, 0, 1}};
  std::uint32_t packed_filter{1};
  // Constraint: mass is in kilograms; zero authors a static body.
  float mass{0.0f};
  // Constraint: a compound body's mass properties live here; a single shape falls back to shape.mass_properties.
  HknpMassProperties mass_properties{};
  // Invariant: inertia_orientation rotates the stored inertia axes onto the body; identity leaves them aligned.
  HknpVec4 inertia_orientation{{0, 0, 0, 1}};
};

// Requires 1 to 255 bodies, each finite with an authorable shape.
// Constraint: a dynamic body needs positive mass and mass properties; anything else raises std::invalid_argument.
// Invariant: dynamic bodies get one motion and one inertia record each, in body order, and that ordinal is the motion index.
HknpPackfile author_hknp_system(const std::vector<HknpSystemBody>& bodies);

}  // namespace fo4::nif
