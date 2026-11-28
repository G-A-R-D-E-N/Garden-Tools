#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {

// Constraint: these run the mesh ops of model_ops.hpp on one plain BSTriShape of a NIF and write the shape back in place.
enum class ModelOp : std::uint8_t {
  extrude_faces,
  bridge_loops,
  knife_plane,
  cut_loop,
  bevel_edges,
};

// Invariant: each op reads only its own fields and ignores the rest.
struct ModelOpRequest {
  ModelOp op{ModelOp::extrude_faces};
  std::uint32_t block{0};
  // Requires triangle indices and a positive distance; extrude_faces only.
  std::vector<std::uint32_t> tris;
  float distance{0.0f};
  // Requires two vertex index loops of equal length; bridge_loops only.
  std::vector<std::uint32_t> loop_a;
  std::vector<std::uint32_t> loop_b;
  // Requires a non-zero normal and a finite offset; knife_plane only.
  std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
  float offset{0.0f};
  // Invariant: the ring for cut_loop, the edges to chamfer for bevel_edges, each a pair of vertex indices.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
  // Requires a positive width; bevel_edges only.
  float width{0.0f};
};

struct ModelOpReport {
  std::size_t vertices_before{0};
  std::size_t vertices_after{0};
  std::size_t triangles_before{0};
  std::size_t triangles_after{0};
  // Invariant: knife_plane only, the pieces written for split triangles and the zero-area pieces dropped.
  std::size_t triangles_added{0};
  std::size_t degenerate_dropped{0};
};

std::optional<ModelOp> parse_model_op(std::string_view name);
const char* model_op_name(ModelOp op) noexcept;

// Constraint: the block must be a plain BSTriShape with one UV set, normal and tangent together or neither, and no BSDismemberSkinInstance.
// Constraint: a shape whose vertex bytes would change when rewritten unedited is refused with std::invalid_argument.
// Invariant: the block keeps its name, extra data, controller, collision, transform, skin, shader and alpha references and its vertex layout; only the vertices, triangles, counts and bounding sphere change.
// Invariant: a half-precision shape stays half precision, so only vertices the op creates are rounded.
std::vector<std::byte> apply_model_op(const NifFile& nif, const ModelOpRequest& request,
                                      ModelOpReport* report = nullptr);

}  // namespace fo4::nif
