#include "fo4/forms/cell.hpp"

#include <limits>

namespace fo4::forms {
namespace {

std::int32_t floor_div(std::int32_t value, std::int32_t divisor) {
  const auto quotient = value / divisor;
  const auto remainder = value % divisor;
  return remainder != 0 && value < 0 ? quotient - 1 : quotient;
}

void require_group_coordinate(std::int32_t value) {
  if (value < std::numeric_limits<std::int16_t>::min() ||
      value > std::numeric_limits<std::int16_t>::max()) {
    throw CellError("exterior CELL group coordinate does not fit signed 16-bit label storage");
  }
}

}  // namespace

bool exterior_cell_coordinate_valid(std::int32_t coordinate) noexcept {
  return coordinate >= k_exterior_cell_min_coordinate &&
         coordinate <= k_exterior_cell_max_coordinate;
}

ExteriorCellGridLayout exterior_cell_grid_layout(std::int32_t x,
                                                 std::int32_t y) {
  if (!exterior_cell_coordinate_valid(x) || !exterior_cell_coordinate_valid(y)) {
    throw CellError("exterior CELL coordinate must be in the CK-proven -110..110 range");
  }

  return ExteriorCellGridLayout{
      .cell_x = x,
      .cell_y = y,
      .block_x = floor_div(x, k_exterior_cell_block_span),
      .block_y = floor_div(y, k_exterior_cell_block_span),
      .subblock_x = floor_div(x, k_exterior_cell_subblock_span),
      .subblock_y = floor_div(y, k_exterior_cell_subblock_span),
  };
}

std::uint32_t exterior_grid_group_label_u32(std::int32_t grid_x,
                                            std::int32_t grid_y) {
  require_group_coordinate(grid_x);
  require_group_coordinate(grid_y);
  const auto x = static_cast<std::uint16_t>(grid_x);
  const auto y = static_cast<std::uint16_t>(grid_y);
  return (static_cast<std::uint32_t>(x) << 16) |
         static_cast<std::uint32_t>(y);
}

std::array<std::byte, 4> exterior_grid_group_label(std::int32_t grid_x,
                                                   std::int32_t grid_y) {
  const auto packed = exterior_grid_group_label_u32(grid_x, grid_y);
  return {
      std::byte{static_cast<std::uint8_t>(packed)},
      std::byte{static_cast<std::uint8_t>(packed >> 8)},
      std::byte{static_cast<std::uint8_t>(packed >> 16)},
      std::byte{static_cast<std::uint8_t>(packed >> 24)},
  };
}

}  // namespace fo4::forms
