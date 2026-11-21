#include "fo4/nif/havok_names.hpp"

#include "fo4/archive/ba2_hash.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

std::uint32_t havok_material_crc(std::string_view name) {
  std::string lower(name);
  for (auto& ch : lower) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return fo4::archive::ba2_crc32(std::as_bytes(std::span(lower)));
}

std::vector<std::string> build_havok_layer_names(std::span<const HavokLayerRow> rows) {
  if (rows.size() > 256) {
    throw std::invalid_argument("a Havok layer table holds at most 256 layers");
  }
  std::vector<std::string> names(rows.size());
  std::vector<bool> seen(rows.size(), false);
  for (const auto& row : rows) {
    if (row.index >= rows.size() || seen[row.index]) {
      throw std::invalid_argument("Havok layer rows must have indices 0 through N-1, each exactly once");
    }
    seen[row.index] = true;
    names[row.index] = row.name;
  }
  return names;
}

}  // namespace fo4::nif
