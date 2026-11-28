#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/land/heightmap.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace fo4::forms {

enum class LandEditStatus {
  edited,
  cell_missing,
  cell_deleted,
  not_a_cell,
  land_missing,
  malformed,
  invalid_value,
};

struct LandEditResult {
  LandEditStatus status{LandEditStatus::land_missing};
  std::string copied_from;
  std::size_t neighbours_updated{0};
  std::size_t neighbours_skipped{0};
};

// Constraint: the patch must gain the winning CELL and its children groups before the VHGT write.
// Constraint: only VHGT is rewritten, so VNML stays as shipped; this raw entry has no float heights to derive normals from.
LandEditResult edit_land_heightmap(Plugin& patch,
                                   const fo4::env::Environment& environment,
                                   const FormKey& cell,
                                   const land::LandHeightmap& heightmap);

// Constraint: heights are absolute world heights in the 33x33 row-major order of land::land_heights.
// Constraint: VNML is rewritten from these floats for vertices whose 3x3 block changed; an approximation of the CK normals, not bit-identical.
// Constraint: changed shared-edge heights and the normals that read them are also written into the up-to-8 neighbour LANDs as patch overrides; one that cannot be updated is only counted.
LandEditResult edit_land_heights(Plugin& patch,
                                 const fo4::env::Environment& environment,
                                 const FormKey& cell,
                                 std::span<const float> heights);

}  // namespace fo4::forms
