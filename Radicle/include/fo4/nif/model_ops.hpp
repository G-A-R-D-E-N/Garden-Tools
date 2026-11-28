#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif::import_detail {
struct Mesh;
}  // namespace fo4::nif::import_detail

namespace fo4::nif::model_detail {

using import_detail::Mesh;
using Vec3 = std::array<float, 3>;

// Invariant: every op refuses with a reason instead of writing a partial mesh.
// Invariant: new vertices copy every attribute channel the mesh carries.
// Invariant: skin bone indices are inherited from the nearer endpoint and never blended.

// Constraint: the selection names triangle indices, not corners.
// Constraint: an empty selection is a refusal.
// Constraint: an out-of-range face is a refusal.
// Constraint: a non-positive or non-finite distance is a refusal.
// Constraint: missing or non-finite normals are a refusal.
std::optional<std::string> extrude_faces(Mesh& mesh, const std::vector<std::uint32_t>& tris, float distance);

// Constraint: the loops hold vertex indices of equal length (at least two).
// Constraint: every index must be in range and no vertex may appear twice.
// Invariant: the band follows the loop order and wraps around.
std::optional<std::string> bridge_loops(Mesh& mesh, const std::vector<std::uint32_t>& loop_a,
                                        const std::vector<std::uint32_t>& loop_b);

// Constraint: the normal must be non-zero and finite.
// Constraint: the offset must be finite.
// Invariant: triangles fully on either side (within the epsilon) are kept.
// Invariant: crossing triangles are split and zero-area pieces are dropped and counted.
// Invariant: triangles_added counts the pieces written for split triangles.
// Constraint: a cut that needs past 65535 vertices is a refusal.
struct KnifeResult {
  std::size_t triangles_added{0};
  std::size_t degenerate_dropped{0};
};
std::optional<std::string> knife_plane(Mesh& mesh, const Vec3& normal, float offset, KnifeResult* result = nullptr,
                                       float epsilon = 1e-6f);

// Constraint: consecutive ring edges share a vertex; first and last share a vertex.
// Constraint: no vertex appears in more than two ring edges.
// Invariant: every ring edge is split at its midpoint.
// Invariant: every triangle the cut passes through is re-triangulated.
// Constraint: a loop no triangle crosses cleanly is refused.
std::optional<std::string> cut_loop(Mesh& mesh, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& ring);

// Constraint: every selected edge must exist in the mesh and be manifold.
// Constraint: selected edges share no endpoint, so each corner takes one inset.
// Constraint: every endpoint may touch only the edge's adjacent triangles, so no crack is left.
// Invariant: each endpoint is chamfered at width into its adjacent face.
// Invariant: a two-face edge gains the chamfer quad and a boundary edge gains a strip.
std::optional<std::string> bevel_edges(Mesh& mesh, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges,
                                       float width);

// Constraint: the quaternion must be finite with a non-zero length.
// Constraint: the scale must be finite and non-zero.
// Invariant: rotation about the pivot applies first, then axial scale about the pivot, then translation.
// Invariant: modal (interactive) transforms call this per pointer event.
struct ModalTransform {
  Vec3 translation{0.0f, 0.0f, 0.0f};
  std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
  Vec3 scale{1.0f, 1.0f, 1.0f};
  Vec3 pivot{0.0f, 0.0f, 0.0f};
};
std::optional<std::string> transform_about_pivot(Mesh& mesh, const ModalTransform& xf);

}  // namespace fo4::nif::model_detail
