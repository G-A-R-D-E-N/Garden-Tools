#pragma once

#include "fo4/nif/shape_ops.hpp"
#include "mesh_tools.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::nif::skin_detail {

// Invariant: one Skin is exactly the four-slot record the shape stores, so a read and a write agree.
struct Skin {
  std::array<float, 4> weight{};
  std::array<std::uint8_t, 4> bone{};
};

using Skins = std::vector<Skin>;

// Requires a shape whose weight and bone arrays cover its vertices.
Skins read_skins(const Shape& shape);

// Requires a tri-shape block; returns each vertex in NIF units after the block's transform.
std::vector<shape_detail::Vec3> global_vertex_points(const NifFile& nif, std::uint32_t block,
                                                     const std::vector<float>& verts);

// Requires one skin per vertex; returns the refusal reason when nothing is written.
std::optional<std::string> write_skin_bytes(write_detail::Bytes& bytes, const shape_detail::Layout& layout,
                                            const Skins& before, const Skins& after, mesh_detail::Delta* delta);

// Requires a tri-shape block that read_shape accepted; returns the refusal reason when the block stays unchanged.
std::optional<std::string> apply_skin_op(const NifFile& nif, const ShapeOpRequest& request, std::uint32_t block,
                                         const Shape& shape, const shape_detail::Layout& layout,
                                         write_detail::Bytes& bytes, mesh_detail::Delta* delta);

}  // namespace fo4::nif::skin_detail
