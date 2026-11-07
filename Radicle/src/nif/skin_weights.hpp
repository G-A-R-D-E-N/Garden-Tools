#pragma once

#include "fo4/nif/shape_ops.hpp"
#include "mesh_tools.hpp"
#include "shape_ops_internal.hpp"
#include "write_model.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace fo4::nif::skin_detail {

// Requires a tri-shape block that read_shape accepted; returns the refusal reason when the block stays unchanged.
std::optional<std::string> apply_skin_op(const NifFile& nif, const ShapeOpRequest& request, std::uint32_t block,
                                         const Shape& shape, const shape_detail::Layout& layout,
                                         write_detail::Bytes& bytes, mesh_detail::Delta* delta);

}  // namespace fo4::nif::skin_detail
