#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct HavokLayerRow {
  std::uint32_t index{0};
  std::string name;
};

// Constraint: the material CRC is CRC-32 of the ASCII-lowercased name, with init 0 and no final XOR.
std::uint32_t havok_material_crc(std::string_view name);

// Requires one row per index 0 through rows.size() - 1 and at most 256 rows, since a packed filter keeps the layer in one byte; otherwise throws std::invalid_argument.
std::vector<std::string> build_havok_layer_names(std::span<const HavokLayerRow> rows);

}  // namespace fo4::nif
