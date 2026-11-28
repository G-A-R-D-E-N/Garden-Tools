#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>

namespace fo4::forms {

enum class TeleportTranslateStatus {
  translated,
  cell_missing,
  malformed,
  copy_failed,
};

struct TeleportTranslateResult {
  TeleportTranslateStatus status{TeleportTranslateStatus::cell_missing};
  std::size_t updated{0};
};

// Call after relocate_exterior_cell on the same working copy; dx and dy are new grid minus old grid.
// A REFR whose XTEL destination door sits in the patch's copy of the cell children gets its XTEL x/y translated by (dx, dy) * 4096.
// REFRs found only in the load order are copied into the patch as overrides first; one pass over the winning REFRs, since Environment has no reverse-link index.
// Every refusal leaves the patch unchanged; a patch that gains no change is not touched at all.
TeleportTranslateResult translate_incoming_teleports(Plugin& patch,
                                                     const env::Environment& environment,
                                                     const FormKey& cell,
                                                     std::int32_t dx_cells,
                                                     std::int32_t dy_cells);

}  // namespace fo4::forms
