#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Constraint: payload is one block of a mapped type whose references name earlier blocks, and it is appended as the last child of parent, which must have a child list.
std::vector<std::byte> add_block(const NifFile& nif, std::uint32_t parent, std::string_view type,
                                 std::span<const std::byte> payload);
// Constraint: the block must be a childless non-root whose only referrers are child lists; those child entries are dropped and later block indices move down by one.
std::vector<std::byte> remove_block(const NifFile& nif, std::uint32_t block);
// Constraint: the block keeps its index; its one child entry moves to the end of the new parent's list, and a move into the block's own subtree is refused.
std::vector<std::byte> reparent_block(const NifFile& nif, std::uint32_t block, std::uint32_t parent);
// Constraint: the shape's alpha slot must be empty, and only a BSTriShape or BSMeshLODTriShape can take the new NiAlphaProperty.
std::vector<std::byte> attach_alpha_property(const NifFile& nif, std::uint32_t shape, std::uint16_t flags,
                                             std::uint8_t threshold);
// Constraint: the shape's shader must be a BSLightingShaderProperty with no texture set; a shader shared with other shapes is copied first, so only this shape changes.
std::vector<std::byte> create_texture_set(const NifFile& nif, std::uint32_t shape,
                                          const std::vector<std::string>& textures);

}  // namespace fo4::nif
