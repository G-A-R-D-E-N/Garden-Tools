#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

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

}  // namespace fo4::forms
