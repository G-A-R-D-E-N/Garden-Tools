#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::nif {

// Constraint: packfile is one bhkPhysicsSystem payload without its length prefix, refused unless it reads and body_id names one of its bodies.
std::vector<std::byte> set_node_collision(const NifFile& nif, std::string_view node_name,
                                          std::span<const std::byte> packfile, std::uint32_t body_id);
// Constraint: a node with no collision object is returned unchanged, and an object or system that another block still names is kept.
std::vector<std::byte> remove_node_collision(const NifFile& nif, std::string_view node_name);
// Constraint: the donor's first BSClothExtraData is copied as a new block and referenced from the named node's extra data.
std::vector<std::byte> copy_cloth_extra_data(const NifFile& nif, std::string_view node_name, const NifFile& donor);
// Constraint: the Havok bit (value 2) of the root's BSXFlags is set or cleared, and enabled adds a BSX block when the root has none.
std::vector<std::byte> set_havok_flag(const NifFile& nif, bool enabled);

}  // namespace fo4::nif
