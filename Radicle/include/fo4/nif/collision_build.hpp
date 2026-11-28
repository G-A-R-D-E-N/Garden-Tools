#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::nif {

// Constraint: the encoder stores the authored convex flags for every non-box shape, so a hull reuses the box family value.
inline constexpr std::uint32_t k_convex_shape_flags = 0x01000143U;

// Invariant: the hull vertices are the exact convex support vertices, so the encoder's stored radius must stay zero.
struct MeshHull {
  // Constraint: shape.vertices are metres, kind is convex, convex_radius is 0 and shape.faces/planes stay empty.
  HknpShape shape;
  // Invariant: every triple indexes shape.vertices, ordered by first appearance in ascending source-vertex order.
  std::vector<std::uint16_t> face_indices;
  // Constraint: distance from the hull centroid to the nearest accepted face plane, in metres; informational only.
  float nearest_face_distance_metres{0.0f};
  // Constraint: estimate valid only when each hull facet is covered once; coplanar duplicate faces double count.
  float volume_metres3{0.0f};
  // Invariant: counts only non-degenerate triangles inspected, so repeated-index or zero-area faces are not counted.
  std::size_t triangles_considered{0};
  std::size_t distinct_positions{0};
};

// Requires finite NIF-unit positions, in-range triangle indices, a non-negative tolerance and a closed convex mesh.
// Constraint: a triangle is a hull face only when every referenced vertex outside its own corners lies on one side of its plane.
// Constraint: every distinct referenced position must lie on an accepted hull plane, otherwise the mesh is not convex.
MeshHull build_convex_hull_from_mesh(std::span<const float> verts, std::span<const std::uint16_t> tris,
                                    std::uint32_t material_crc, float tolerance_nif_units = 1e-3f,
                                    float game_units_per_metre = k_hknp_game_units_per_metre);

// Constraint: tolerance_nif_units is in NIF game units and the payload is what a system block stores after its u32 prefix.
std::vector<std::byte> encode_collision_payload(std::span<const float> verts, std::span<const std::uint16_t> tris,
                                               std::uint32_t material_crc, const HknpStaticBody& body,
                                               float tolerance_nif_units = 1e-3f,
                                               float game_units_per_metre = k_hknp_game_units_per_metre);

// Requires a bhkPhysicsSystem/bhkRagdollSystem block and a payload that parses as one hknp packfile.
// Invariant: bytes after the stored blob stay in place, other blocks stay byte-identical and the file re-reads cleanly.
// Constraint: a block-level refusal throws parse_error like the edit writer; only a bad caller payload throws std::invalid_argument.
std::vector<std::byte> replace_collision_blob(const NifFile& nif, std::uint32_t block, std::span<const std::byte> payload);
}  // namespace fo4::nif
