#include "fo4/forms/cell.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

using fo4::binary::raw::write_i32;

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
  return encode_zstring(value);
}

struct MutableWorldspaceLocation {
  Group* top_group{nullptr};
  std::size_t record_index{0};
};

MutableWorldspaceLocation locate_worldspace(Plugin& plugin, const FormKey& key) {
  for (auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
        group.header.top_label() != sig_wrld) {
      continue;
    }
    for (std::size_t index = 0; index < group.children.size(); ++index) {
      auto* record = std::get_if<Record>(&group.children[index]);
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

Record detach_created_cell(Plugin& plugin, const FormKey& key) {
  for (std::size_t group_index = 0; group_index < plugin.groups.size(); ++group_index) {
    auto& group = plugin.groups[group_index];
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top) ||
        group.header.top_label() != sig_cell) {
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

}  // namespace

std::array<std::byte, 4> exterior_grid_group_label(std::int32_t grid_x,
                                                   std::int32_t grid_y) {
  return u32_label(exterior_grid_group_label_u32(grid_x, grid_y));
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
  if (worldspace == nullptr || worldspace->header.signature != sig_wrld) {
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

  const auto key = create_local_record(next, sig_cell);
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
  record.subrecords.push_back(Subrecord{sig_xclc, std::move(xclc), false});

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

FormKey create_worldspace_persistent_cell(Plugin& plugin, const FormKey& worldspace, std::uint16_t flags) {
  if ((flags & 0x0001u) != 0) {
    throw CellError("persistent exterior CELL DATA cannot set the interior flag");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  const auto* world_record = find_record(next, worldspace);
  if (world_record == nullptr || world_record->header.signature != sig_wrld) {
    throw CellError("persistent CELL creation requires an existing WRLD record in the target plugin");
  }
  if (locate_worldspace(next, worldspace).top_group == nullptr) {
    throw CellError("WRLD record must be directly contained by the top-level WRLD group");
  }
  if (find_persistent_exterior_cell(next, worldspace) != nullptr) {
    throw CellError("worldspace already has a persistent CELL");
  }

  const auto key = create_local_record(next, sig_cell);
  auto record = detach_created_cell(next, key);
  std::vector<std::byte> data(2, std::byte{0});
  data[0] = std::byte{static_cast<std::uint8_t>(flags)};
  data[1] = std::byte{static_cast<std::uint8_t>(flags >> 8)};
  record.subrecords.push_back(Subrecord{sig_data, std::move(data), false});

  auto* world_children = ensure_worldspace_children(next, worldspace);
  world_children->children.insert(world_children->children.begin(), std::move(record));

  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
