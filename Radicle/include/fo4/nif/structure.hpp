#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct BlockRemoval {
  std::vector<std::byte> bytes;
  std::vector<std::uint32_t> removed;
  std::vector<std::uint32_t> kept;
};

// Constraint: payload is one node or shape block with earlier references; it becomes parent's last child.
std::vector<std::byte> add_block(const NifFile& nif, std::uint32_t parent, std::string_view type,
                                 std::span<const std::byte> payload);
// Constraint: the block must be a childless non-root whose only referrers are child lists; blocks only it referenced go with it, a header string only they used is emptied in place, and removed and kept hold original indices.
BlockRemoval remove_block(const NifFile& nif, std::uint32_t block);
// Constraint: the block keeps its index; its one child entry moves to the end of the new parent's list, and a move into the block's own subtree is refused.
std::vector<std::byte> reparent_block(const NifFile& nif, std::uint32_t block, std::uint32_t parent);
// Constraint: the shape's alpha slot must be empty, and only a BSTriShape or BSMeshLODTriShape can take the new NiAlphaProperty.
std::vector<std::byte> attach_alpha_property(const NifFile& nif, std::uint32_t shape, std::uint16_t flags,
                                             std::uint8_t threshold);
// Constraint: the shape's shader must be a BSLightingShaderProperty with no texture set; a shader shared with other shapes is copied first, so only this shape changes.
std::vector<std::byte> create_texture_set(const NifFile& nif, std::uint32_t shape,
                                          const std::vector<std::string>& textures);

}  // namespace fo4::nif
