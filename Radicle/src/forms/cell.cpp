#include "fo4/forms/cell.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_wrld = Signature::from_string("WRLD");
constexpr auto k_xclc = Signature::from_string("XCLC");

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

std::array<std::byte, 4> u32_label(std::uint32_t value) {
  return {
      std::byte{static_cast<std::uint8_t>(value)},
      std::byte{static_cast<std::uint8_t>(value >> 8)},
      std::byte{static_cast<std::uint8_t>(value >> 16)},
      std::byte{static_cast<std::uint8_t>(value >> 24)},
  };
}

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw CellError("CELL editor ID cannot contain an embedded NUL");
  }
  std::vector<std::byte> bytes;
  bytes.reserve(value.size() + 1);
  for (const char ch : value) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void write_i32(std::vector<std::byte>& data, std::size_t offset, std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

struct MutableWorldspaceLocation {
  Group* top_group{nullptr};
  std::size_t record_index{0};
};

struct ConstWorldspaceLocation {
  const Group* top_group{nullptr};
  std::size_t record_index{0};
};

MutableWorldspaceLocation locate_worldspace(Plugin& plugin, const FormKey& key) {
  for (auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
        group.header.top_label() != k_wrld) {
      continue;
    }
    for (std::size_t index = 0; index < group.children.size(); ++index) {
      auto* record = std::get_if<Record>(&group.children[index]);
      if (record != nullptr && record->form_key == key &&
          record->header.signature == k_wrld) {
        return {&group, index};
      }
    }
  }
  return {};
}

ConstWorldspaceLocation locate_worldspace(const Plugin& plugin, const FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
        group.header.top_label() != k_wrld) {
      continue;
    }
    for (std::size_t index = 0; index < group.children.size(); ++index) {
      const auto* record = std::get_if<Record>(&group.children[index]);
      if (record != nullptr && record->form_key == key &&
          record->header.signature == k_wrld) {
        return {&group, index};
      }
    }
  }
  return {};
}

std::uint32_t worldspace_child_label(const Plugin& plugin, const FormKey& worldspace) {
  return encode_plugin_form_id(worldspace, plugin_form_id_context(plugin)).value();
}

Group* find_unique_group(std::vector<GroupChild>& children,
                         GroupType type,
                         std::uint32_t label) {
  Group* found = nullptr;
  for (auto& child : children) {
    auto* group = std::get_if<Group>(&child);
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

Group make_group(GroupType type, std::array<std::byte, 4> label) {
  Group group;
  group.header.group_type = static_cast<std::int32_t>(type);
  group.header.label = label;
  return group;
}

Group* ensure_worldspace_children(Plugin& plugin, const FormKey& worldspace) {
  auto location = locate_worldspace(plugin, worldspace);
  if (location.top_group == nullptr) {
    throw CellError("WRLD record must be a direct child of the top-level WRLD group");
  }

  const auto label = worldspace_child_label(plugin, worldspace);
  if (auto* existing = find_unique_group(
          location.top_group->children, GroupType::world_children, label)) {
    return existing;
  }

  const auto insertion = location.record_index + 1;
  location.top_group->children.insert(
      location.top_group->children.begin() + static_cast<std::ptrdiff_t>(insertion),
      make_group(GroupType::world_children, u32_label(label)));
  return &std::get<Group>(location.top_group->children[insertion]);
}

Group* ensure_grid_group(Group& parent,
                         GroupType type,
                         std::uint32_t label) {
  if (auto* existing = find_unique_group(parent.children, type, label)) {
    return existing;
  }
  parent.children.emplace_back(make_group(type, u32_label(label)));
  return &std::get<Group>(parent.children.back());
}

std::optional<std::pair<std::int32_t, std::int32_t>> cell_grid(
    const Record& record) {
  const Subrecord* grid = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != k_xclc) continue;
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

Record detach_created_cell(Plugin& plugin, const FormKey& key) {
  for (std::size_t group_index = 0; group_index < plugin.groups.size(); ++group_index) {
    auto& group = plugin.groups[group_index];
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
        group.header.top_label() != k_cell) {
      continue;
    }
    for (auto it = group.children.begin(); it != group.children.end(); ++it) {
      auto* record = std::get_if<Record>(&*it);
      if (record == nullptr || record->form_key != key) continue;
      Record detached = std::move(*record);
      group.children.erase(it);
      if (group.children.empty()) {
        plugin.groups.erase(
            plugin.groups.begin() + static_cast<std::ptrdiff_t>(group_index));
      }
      return detached;
    }
  }
  throw CellError("newly allocated CELL could not be detached from its temporary top-level group");
}

const Group* worldspace_children(const Plugin& plugin, const FormKey& worldspace) {
  const auto location = locate_worldspace(plugin, worldspace);
  if (location.top_group == nullptr) return nullptr;
  const auto label = worldspace_child_label(plugin, worldspace);
  return find_unique_group(
      location.top_group->children, GroupType::world_children, label);
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
  return u32_label(exterior_grid_group_label_u32(grid_x, grid_y));
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
    if (record == nullptr || record->header.signature != k_cell ||
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
    if (record != nullptr && record->header.signature == k_cell) return record;
  }
  return nullptr;
}

FormKey create_exterior_cell(Plugin& plugin, const ExteriorCellCreation& creation) {
  const auto layout = exterior_cell_grid_layout(creation.x, creation.y);
  if (creation.editor_id.has_value() &&
      creation.editor_id->find('\0') != std::string_view::npos) {
    throw CellError("CELL editor ID cannot contain an embedded NUL");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  const auto* worldspace = find_record(next, creation.worldspace);
  if (worldspace == nullptr || worldspace->header.signature != k_wrld) {
    throw CellError("exterior CELL creation requires an existing WRLD record in the target plugin");
  }
  if (locate_worldspace(next, creation.worldspace).top_group == nullptr) {
    throw CellError("WRLD record must be directly contained by the top-level WRLD group");
  }
  if (find_exterior_cell(next, creation.worldspace, creation.x, creation.y) != nullptr) {
    throw CellError("exterior CELL grid coordinate is already occupied");
  }

  if ((creation.flags & 0x0001u) != 0) {
    throw CellError("exterior CELL DATA cannot set the interior flag");
  }

  const auto key = create_local_record(next, k_cell);
  auto record = detach_created_cell(next, key);
  if (creation.editor_id.has_value()) {
    record.subrecords.push_back(Subrecord{sig_edid, zstring(stored_editor_id(*creation.editor_id)), false});
  }

  // CK LoadRecord accepts CELL DATA as either char or short; the in-memory
  // CELL flag field is 16-bit, so new current-format cells use two bytes.
  //
  // The value is not zero. Bit 1 is set on 61,542 of the 61,543 exterior cells
  // across Fallout4.esm, DLCCoast and DLCNukaWorld, so all-clear flags produce
  // a record shape that occurs exactly once in the shipped corpus. See
  // k_exterior_cell_default_flags.
  std::vector<std::byte> data(2, std::byte{0});
  data[0] = std::byte{static_cast<std::uint8_t>(creation.flags)};
  data[1] = std::byte{static_cast<std::uint8_t>(creation.flags >> 8)};
  record.subrecords.push_back(Subrecord{sig_data, std::move(data), false});

  // FO4 XCLC is exterior-only and 12 bytes in all 38,970 shipped instances
  // that carry it. The trailing dword stays zero: the CK's own coordinate
  // setter (FUN_140d197c0) writes only x and y and leaves the rest of the
  // cell-data block as initialised, and 84 shipped cells carry zero there.
  std::vector<std::byte> xclc(12, std::byte{0});
  write_i32(xclc, 0, creation.x);
  write_i32(xclc, 4, creation.y);
  record.subrecords.push_back(Subrecord{k_xclc, std::move(xclc), false});

  auto* world_children = ensure_worldspace_children(next, creation.worldspace);
  const auto block_label =
      exterior_grid_group_label_u32(layout.block_x, layout.block_y);
  auto* block = ensure_grid_group(
      *world_children, GroupType::exterior_cell_block, block_label);
  const auto subblock_label =
      exterior_grid_group_label_u32(layout.subblock_x, layout.subblock_y);
  auto* subblock = ensure_grid_group(
      *block, GroupType::exterior_cell_subblock, subblock_label);
  subblock->children.emplace_back(std::move(record));

  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
