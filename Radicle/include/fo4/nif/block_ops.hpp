#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fo4/nif/nif.hpp"

namespace fo4::nif {

// Constraint: sorting keeps the on-disk child count, drops null slots and fills the tail with -1.
std::vector<std::byte> sort_children_by_name(const NifFile& nif, std::uint32_t node);

// Constraint: the child needs no parent and must not be an ancestor; the node's child list must end its block.
std::vector<std::byte> append_child(const NifFile& nif, std::uint32_t node, std::uint32_t child);

// Constraint: only childless nodes and shape blocks may be copied; a shape copy shares skin, shader and texture refs.
std::vector<std::byte> duplicate_block(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif
