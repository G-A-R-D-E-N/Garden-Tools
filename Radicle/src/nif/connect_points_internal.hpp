#pragma once

#include "fo4/binary/cursor.hpp"
#include "fo4/nif/connect_points.hpp"
#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::nif::connect_detail {

std::span<const std::byte> block_bytes(const NifFile& nif, std::uint32_t block);
// Requires the block to be in the file and to have the given type; throws parse_error otherwise.
void require_block(const NifFile& nif, std::uint32_t block, std::string_view type);
ConnectParents parse_parents(binary::Cursor& cursor, const NifFile& nif);
ConnectChildren parse_children(binary::Cursor& cursor, const NifFile& nif);
// Invariant: the NiExtraData name index and any bytes after the list are copied from the original block.
std::vector<std::byte> encode_parents(const NifFile& nif, std::uint32_t block, const ConnectParents& value);
std::vector<std::byte> encode_children(const NifFile& nif, std::uint32_t block, const ConnectChildren& value);
// Requires a node named parent in nif, or an empty parent, or k_workshop_connect_parent.
void require_parent_node(const NifFile& nif, std::uint32_t block, std::string_view parent);

}  // namespace fo4::nif::connect_detail
