#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/exterior_cell.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fo4::forms {

using fo4::k_exterior_cell_min_coordinate;
using fo4::k_exterior_cell_max_coordinate;
using fo4::k_exterior_cell_block_span;
using fo4::k_exterior_cell_subblock_span;
inline constexpr std::int32_t k_worldspace_children_group_type = 1;
inline constexpr std::int32_t k_exterior_cell_block_group_type = 4;
inline constexpr std::int32_t k_exterior_cell_subblock_group_type = 5;

using fo4::CellError;
using fo4::ExteriorCellGridLayout;
using fo4::exterior_cell_lookup::cell_grid;
using fo4::exterior_cell_lookup::exterior_cell_coordinate_valid;
using fo4::exterior_cell_lookup::exterior_cell_grid_layout;
using fo4::exterior_cell_lookup::exterior_grid_group_label_u32;
using fo4::exterior_cell_lookup::find_exterior_cell;
using fo4::exterior_cell_lookup::find_persistent_exterior_cell;

// Serialized CELL DATA flags. Bit 0 marks an interior cell; bit 1 is set on
// effectively every shipped exterior cell (61,542 of 61,543 across
// Fallout4.esm, DLCCoast and DLCNukaWorld -- the single exception is one cell
// with DATA 0x0000). Emitting 0x0000 therefore produces a shape that occurs
// once in the entire shipped corpus, so new exterior cells default to 0x0002.
inline constexpr std::uint16_t k_exterior_cell_default_flags = 0x0002;

struct ExteriorCellCreation {
  FormKey worldspace;
  std::int32_t x{0};
  std::int32_t y{0};
  std::optional<std::string_view> editor_id;
  // Overridable, but bit 0 (interior) is refused: this path only builds
  // exterior cells and a mismatched flag would contradict the XCLC it emits.
  std::uint16_t flags{k_exterior_cell_default_flags};
};

std::array<std::byte, 4> exterior_grid_group_label(std::int32_t grid_x,
                                                   std::int32_t grid_y);

// Creates a new local exterior CELL beneath an existing WRLD record in this
// plugin. The mutation is transactional: invalid/malformed hierarchy or an
// occupied coordinate leaves the caller's plugin unchanged.
FormKey create_exterior_cell(Plugin& plugin, const ExteriorCellCreation& creation);

// Requires: an existing WRLD without a persistent CELL; the new CELL carries only a DATA of the given flags (bit 0 refused) and is the first child of the WRLD children GRUP.
FormKey create_worldspace_persistent_cell(Plugin& plugin, const FormKey& worldspace, std::uint16_t flags);

enum class CellRelocateStatus {
  relocated,
  unchanged,
  cell_missing,
  cell_deleted,
  not_a_cell,
  not_exterior,
  persistent_cell,
  invalid_coordinate,
  destination_occupied,
  malformed,
};

struct CellRelocateResult {
  CellRelocateStatus status{CellRelocateStatus::cell_missing};
};

// A coordinate change must move the CELL and its children group together; a lone XCLC edit leaves the group labels stale.
// Every refusal must leave the plugin unchanged, including interior, persistent and misfiled cells.
// The children group is translated by the grid delta times 4096: placed-reference DATA x/y, and NAVM NVNM vertex, waypoint and grid-bound x/y plus the parent grid. Anything that cannot be translated safely returns malformed and leaves the plugin unchanged.
CellRelocateResult relocate_exterior_cell(Plugin& plugin,
                                         const FormKey& cell,
                                         std::int32_t x,
                                         std::int32_t y);

FormKey create_interior_cell(Plugin& plugin, std::string_view editor_id);

}  // namespace fo4::forms
