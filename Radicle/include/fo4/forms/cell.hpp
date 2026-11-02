#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace fo4::forms {

inline constexpr std::int32_t k_exterior_cell_min_coordinate = -110;
inline constexpr std::int32_t k_exterior_cell_max_coordinate = 110;
inline constexpr std::int32_t k_exterior_cell_block_span = 32;
inline constexpr std::int32_t k_exterior_cell_subblock_span = 8;
inline constexpr std::int32_t k_worldspace_children_group_type = 1;
inline constexpr std::int32_t k_exterior_cell_block_group_type = 4;
inline constexpr std::int32_t k_exterior_cell_subblock_group_type = 5;

class CellError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ExteriorCellGridLayout {
  std::int32_t cell_x{0};
  std::int32_t cell_y{0};
  std::int32_t block_x{0};
  std::int32_t block_y{0};
  std::int32_t subblock_x{0};
  std::int32_t subblock_y{0};
};

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

bool exterior_cell_coordinate_valid(std::int32_t coordinate) noexcept;

// xEdit's worldspace grouping contract uses mathematical floor division:
// exterior blocks are 32x32 cells and sub-blocks are 8x8 cells. This matters
// for negative, non-multiple coordinates where C++ integer division truncates
// toward zero instead of toward negative infinity.
ExteriorCellGridLayout exterior_cell_grid_layout(std::int32_t x,
                                                 std::int32_t y);

// xEdit exposes wbGridCellToGroupLabel as 0xXXXXYYYY. In the little-endian
// GRUP header this means Y occupies label bytes 0..1 and X bytes 2..3.
std::uint32_t exterior_grid_group_label_u32(std::int32_t grid_x,
                                            std::int32_t grid_y);
std::array<std::byte, 4> exterior_grid_group_label(std::int32_t grid_x,
                                                   std::int32_t grid_y);

// Creates a new local exterior CELL beneath an existing WRLD record in this
// plugin. The mutation is transactional: invalid/malformed hierarchy or an
// occupied coordinate leaves the caller's plugin unchanged.
FormKey create_exterior_cell(Plugin& plugin, const ExteriorCellCreation& creation);

FormKey create_interior_cell(Plugin& plugin, std::string_view editor_id);

// Finds a non-persistent exterior CELL at the requested coordinate beneath the
// given WRLD hierarchy. Returns null when the hierarchy/coordinate is absent;
// malformed matching hierarchy throws CellError instead of guessing.
const Record* find_exterior_cell(const Plugin& plugin,
                                 const FormKey& worldspace,
                                 std::int32_t x,
                                 std::int32_t y);

const Record* find_persistent_exterior_cell(const Plugin& plugin, const FormKey& worldspace);

}  // namespace fo4::forms
