#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

namespace fo4 {

inline constexpr std::int32_t k_exterior_cell_min_coordinate = -110;
inline constexpr std::int32_t k_exterior_cell_max_coordinate = 110;
inline constexpr std::int32_t k_exterior_cell_block_span = 32;
inline constexpr std::int32_t k_exterior_cell_subblock_span = 8;

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

namespace exterior_cell_lookup {

bool exterior_cell_coordinate_valid(std::int32_t coordinate) noexcept;

ExteriorCellGridLayout exterior_cell_grid_layout(std::int32_t x, std::int32_t y);

std::uint32_t exterior_grid_group_label_u32(std::int32_t grid_x, std::int32_t grid_y);

const Record* find_exterior_cell(const Plugin& plugin,
                                 const FormKey& worldspace,
                                 std::int32_t x,
                                 std::int32_t y);

const Record* find_persistent_exterior_cell(const Plugin& plugin, const FormKey& worldspace);

std::optional<std::pair<std::int32_t, std::int32_t>> cell_grid(const Record& record);

}  // namespace exterior_cell_lookup
}  // namespace fo4
