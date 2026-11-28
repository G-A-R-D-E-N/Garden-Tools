#include "fo4/plugin/exterior_cell.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using fo4::binary::raw::read_i32;

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

struct ConstWorldspaceLocation {
  const Group* top_group{nullptr};
  std::size_t record_index{0};
};

ConstWorldspaceLocation locate_worldspace(const Plugin& plugin, const FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
        group.header.top_label() != sig_wrld) {
      continue;
    }
    for (std::size_t index = 0; index < group.children.size(); ++index) {
      const auto* record = std::get_if<Record>(&group.children[index]);
      if (record != nullptr && record->form_key == key &&
          record->header.signature == sig_wrld) {
        return {&group, index};
      }
    }
  }
  return {};
}

std::uint32_t worldspace_child_label(const Plugin& plugin, const FormKey& worldspace) {
  return encode_plugin_form_id(worldspace, plugin_form_id_context(plugin)).value();
}

const Group* find_unique_group(const std::vector<GroupChild>& children,
                               GroupType type,
                               std::uint32_t label) {
  const Group* found = nullptr;
  for (const auto& child : children) {
    const auto* group = std::get_if<Group>(&child);
    if (group == nullptr ||
        group->header.group_type != static_cast<std::int32_t>(type) ||
        group->header.label_as_u32() != label) {
      continue;
    }
    if (found != nullptr) {
      throw CellError("duplicate exterior CELL hierarchy group label");
    }
    found = group;
  }
  return found;
}

const Group* worldspace_children(const Plugin& plugin, const FormKey& worldspace) {
  const auto location = locate_worldspace(plugin, worldspace);
  if (location.top_group == nullptr) return nullptr;
  const auto label = worldspace_child_label(plugin, worldspace);
  return find_unique_group(
      location.top_group->children, GroupType::world_children, label);
}

}  // namespace

namespace exterior_cell_lookup {

std::optional<std::pair<std::int32_t, std::int32_t>> cell_grid(
    const Record& record) {
  const Subrecord* grid = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig_xclc) continue;
    if (grid != nullptr) {
      throw CellError("exterior CELL contains duplicate XCLC subrecords");
    }
    grid = &subrecord;
  }
  if (grid == nullptr) return std::nullopt;
  if (grid->data.size() != 8 && grid->data.size() != 12) {
    throw CellError("exterior CELL XCLC must be 8 or 12 bytes");
  }
  return std::pair{
      read_i32(grid->data, 0),
      read_i32(grid->data, 4),
  };
}

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

const Record* find_exterior_cell(const Plugin& plugin,
                                 const FormKey& worldspace,
                                 std::int32_t x,
                                 std::int32_t y) {
  const auto layout = exterior_cell_grid_layout(x, y);
  const auto* world_children = worldspace_children(plugin, worldspace);
  if (world_children == nullptr) return nullptr;

  const auto block_label =
      exterior_grid_group_label_u32(layout.block_x, layout.block_y);
  const auto* block = find_unique_group(
      world_children->children, GroupType::exterior_cell_block, block_label);
  if (block == nullptr) return nullptr;

  const auto subblock_label =
      exterior_grid_group_label_u32(layout.subblock_x, layout.subblock_y);
  const auto* subblock = find_unique_group(
      block->children, GroupType::exterior_cell_subblock, subblock_label);
  if (subblock == nullptr) return nullptr;

  const Record* found = nullptr;
  for (const auto& child : subblock->children) {
    const auto* record = std::get_if<Record>(&child);
    if (record == nullptr || record->header.signature != sig_cell ||
        record->header.flags.is_persistent()) {
      continue;
    }
    if (record->undecodable) {
      throw CellError("undecodable CELL inside an exterior sub-block");
    }
    const auto grid = cell_grid(*record);
    if (!grid.has_value()) {
      throw CellError("CELL inside an exterior sub-block is missing XCLC");
    }
    if (grid->first != x || grid->second != y) continue;
    if (found != nullptr) {
      throw CellError("duplicate exterior CELL grid coordinate in one sub-block");
    }
    found = record;
  }
  return found;
}

const Record* find_persistent_exterior_cell(const Plugin& plugin, const FormKey& worldspace) {
  const auto* world_children = worldspace_children(plugin, worldspace);
  if (world_children == nullptr) return nullptr;
  for (const auto& child : world_children->children) {
    const auto* record = std::get_if<Record>(&child);
    if (record != nullptr && record->header.signature == sig_cell) return record;
  }
  return nullptr;
}

}  // namespace exterior_cell_lookup
}  // namespace fo4
