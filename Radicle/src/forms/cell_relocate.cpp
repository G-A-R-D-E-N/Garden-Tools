#include "fo4/forms/cell.hpp"

#include "../plugin/child_groups.hpp"
#include "fo4/constants.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace fo4::forms {
namespace {

constexpr std::size_t k_xclc_minimum_size = 8;
constexpr std::array k_placed_kinds{
    Signature::from_string("REFR"), Signature::from_string("ACHR"),
    Signature::from_string("PGRE"), Signature::from_string("PMIS"),
    Signature::from_string("PARW"), Signature::from_string("PBEA"),
    Signature::from_string("PFLA"), Signature::from_string("PCON"),
    Signature::from_string("PBAR"), Signature::from_string("PHZD"),
};
constexpr std::size_t k_placed_data_size = 24;
constexpr std::uint32_t k_nvnm_version = 15;
constexpr std::uint16_t k_nvnm_form_version_minimum = 128;
constexpr std::size_t k_nvnm_parent_offset = 12;
constexpr std::size_t k_nvnm_vertices_offset = 16;
constexpr std::size_t k_nvnm_grid_size = 36;
constexpr std::uint32_t k_nvnm_max_divisor = 12;
constexpr std::size_t k_nvnm_grid_min_xy = 12;
constexpr std::size_t k_nvnm_grid_max_xy = 24;

struct Shift {
  float x{0};
  float y{0};
};

// Adding zero would still rewrite -0.0 to +0.0, so an untouched axis must not be written.
bool add_to_float(std::vector<std::byte>& data, std::size_t offset, float delta) {
  if (delta == 0.0f) return true;
  if (offset > data.size() || data.size() - offset < sizeof(float)) return false;
  float value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  value += delta;
  if (!std::isfinite(value)) return false;
  std::memcpy(data.data() + offset, &value, sizeof(value));
  return true;
}

bool shift_xy(std::vector<std::byte>& data, std::size_t offset, Shift shift) {
  return add_to_float(data, offset, shift.x) && add_to_float(data, offset + 4, shift.y);
}

Subrecord* unique_subrecord(Record& record, Signature signature, bool& duplicate) {
  Subrecord* found = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) duplicate = true;
    found = &subrecord;
  }
  return found;
}

bool shift_placed_reference(Record& record, Shift shift) {
  bool duplicate = false;
  auto* data = unique_subrecord(record, sig_data, duplicate);
  if (duplicate || data == nullptr || data->data.size() != k_placed_data_size) return false;
  return shift_xy(data->data, 0, shift);
}

struct Counted {
  std::size_t first{0};
  std::size_t count{0};
};

std::optional<Counted> take_counted(const std::vector<std::byte>& data,
                                    std::size_t& cursor,
                                    std::size_t element_size) {
  if (data.size() - cursor < 4) return std::nullopt;
  std::uint32_t count = 0;
  std::memcpy(&count, data.data() + cursor, sizeof(count));
  cursor += 4;
  if (count > (data.size() - cursor) / element_size) return std::nullopt;
  const Counted counted{cursor, count};
  cursor += static_cast<std::size_t>(count) * element_size;
  return counted;
}

// NVNM x/y floats of the vertices, waypoints and grid bounds, and the parent grid, are world-absolute (Fallout4.esm survey in the code-notes).
bool shift_navmesh(Record& record,
                   std::int32_t old_x,
                   std::int32_t old_y,
                   std::int32_t new_x,
                   std::int32_t new_y,
                   Shift shift) {
  bool duplicate = false;
  auto* nvnm = unique_subrecord(record, sig_nvnm, duplicate);
  if (duplicate) return false;
  if (nvnm == nullptr) return true;
  auto& data = nvnm->data;
  if (record.header.form_version < k_nvnm_form_version_minimum ||
      data.size() < k_nvnm_vertices_offset) {
    return false;
  }
  std::uint32_t version = 0;
  std::uint32_t world = 0;
  std::int16_t grid_y = 0;
  std::int16_t grid_x = 0;
  std::memcpy(&version, data.data(), sizeof(version));
  std::memcpy(&world, data.data() + 8, sizeof(world));
  std::memcpy(&grid_y, data.data() + k_nvnm_parent_offset, sizeof(grid_y));
  std::memcpy(&grid_x, data.data() + k_nvnm_parent_offset + 2, sizeof(grid_x));
  if (version != k_nvnm_version || world == 0 || grid_x != old_x || grid_y != old_y) return false;

  std::size_t cursor = k_nvnm_vertices_offset;
  const auto vertices = take_counted(data, cursor, 12);
  if (!vertices || !take_counted(data, cursor, 21) || !take_counted(data, cursor, 11) ||
      !take_counted(data, cursor, 10) || !take_counted(data, cursor, 8) ||
      !take_counted(data, cursor, 4)) {
    return false;
  }
  const auto waypoints = take_counted(data, cursor, 18);
  if (!waypoints || data.size() - cursor < k_nvnm_grid_size) return false;
  const auto grid = cursor;
  std::uint32_t divisor = 0;
  std::memcpy(&divisor, data.data() + grid, sizeof(divisor));
  if (divisor > k_nvnm_max_divisor) return false;
  cursor += k_nvnm_grid_size;
  for (std::uint32_t cell = 0; cell < divisor * divisor; ++cell) {
    if (!take_counted(data, cursor, 2)) return false;
  }
  if (cursor != data.size()) return false;

  const auto new_grid_y = static_cast<std::int16_t>(new_y);
  const auto new_grid_x = static_cast<std::int16_t>(new_x);
  std::memcpy(data.data() + k_nvnm_parent_offset, &new_grid_y, sizeof(new_grid_y));
  std::memcpy(data.data() + k_nvnm_parent_offset + 2, &new_grid_x, sizeof(new_grid_x));
  for (std::size_t index = 0; index < vertices->count; ++index) {
    if (!shift_xy(data, vertices->first + index * 12, shift)) return false;
  }
  for (std::size_t index = 0; index < waypoints->count; ++index) {
    if (!shift_xy(data, waypoints->first + index * 18, shift)) return false;
  }
  return shift_xy(data, grid + k_nvnm_grid_min_xy, shift) &&
         shift_xy(data, grid + k_nvnm_grid_max_xy, shift);
}

struct ChildShift {
  Plugin& plugin;
  std::int32_t old_x;
  std::int32_t old_y;
  std::int32_t new_x;
  std::int32_t new_y;
  Shift shift;
};

bool shift_children(const ChildShift& context, Group& group) {
  for (auto& child : group.children) {
    if (auto* nested = std::get_if<Group>(&child)) {
      if (!shift_children(context, *nested)) return false;
      continue;
    }
    auto& record = std::get<Record>(child);
    if (record.header.flags.is_deleted()) continue;
    const auto signature = record.header.signature;
    const bool placed =
        std::find(k_placed_kinds.begin(), k_placed_kinds.end(), signature) != k_placed_kinds.end();
    if (!placed && signature != sig_navm) continue;
    if (record.undecodable) return false;
    const bool shifted =
        placed ? shift_placed_reference(record, context.shift)
               : shift_navmesh(record, context.old_x, context.old_y, context.new_x, context.new_y,
                               context.shift);
    if (!shifted) return false;
    fo4::detail::mark_record_modified(context.plugin, record);
  }
  return true;
}

std::int32_t floor_div(std::int32_t value, std::int32_t divisor) {
  const auto quotient = value / divisor;
  const auto remainder = value % divisor;
  return remainder != 0 && value < 0 ? quotient - 1 : quotient;
}

std::array<std::byte, 4> label_bytes(std::uint32_t value) {
  return {
      std::byte{static_cast<std::uint8_t>(value)},
      std::byte{static_cast<std::uint8_t>(value >> 8)},
      std::byte{static_cast<std::uint8_t>(value >> 16)},
      std::byte{static_cast<std::uint8_t>(value >> 24)},
  };
}

bool same_group(const Group& group, GroupType type, std::uint32_t label) {
  return group.header.group_type == static_cast<std::int32_t>(type) &&
         group.header.label_as_u32() == label;
}

// A duplicated grid group label must fail rather than pick one of the groups.
Group* find_group(Group& parent, GroupType type, std::uint32_t label) {
  Group* found = nullptr;
  for (auto& child : parent.children) {
    auto* group = std::get_if<Group>(&child);
    if (group == nullptr || !same_group(*group, type, label)) continue;
    if (found != nullptr) throw CellError("duplicate exterior CELL hierarchy group label");
    found = group;
  }
  return found;
}

// Sibling block and sub-block groups must stay ordered by label.
Group* ensure_group(Group& parent, GroupType type, std::uint32_t label) {
  if (auto* existing = find_group(parent, type, label)) return existing;
  auto position = parent.children.end();
  for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
    const auto* group = std::get_if<Group>(&*it);
    if (group == nullptr || group->header.group_type != static_cast<std::int32_t>(type)) continue;
    if (group->header.label_as_u32() > label) {
      position = it;
      break;
    }
  }
  Group created;
  created.header.group_type = static_cast<std::int32_t>(type);
  created.header.label = label_bytes(label);
  return &std::get<Group>(*parent.children.insert(position, std::move(created)));
}

void erase_group_if_empty(Group& parent, GroupType type, std::uint32_t label) {
  for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
    auto* group = std::get_if<Group>(&*it);
    if (group == nullptr || !same_group(*group, type, label)) continue;
    if (group->children.empty()) parent.children.erase(it);
    return;
  }
}

// Records must stay sorted by FormID, and a CELL must be followed by its own child groups.
void insert_cell(Group& subblock, Record record, std::optional<Group> children) {
  const auto form_id = record.header.form_id.value();
  auto position = subblock.children.end();
  for (auto it = subblock.children.begin(); it != subblock.children.end(); ++it) {
    const auto* existing = std::get_if<Record>(&*it);
    if (existing != nullptr && existing->header.signature == sig_cell &&
        existing->header.form_id.value() > form_id) {
      position = it;
      break;
    }
  }
  const auto inserted = subblock.children.insert(position, std::move(record));
  if (children.has_value()) {
    subblock.children.insert(inserted + 1, GroupChild{std::move(*children)});
  }
}

}  // namespace

CellRelocateResult relocate_exterior_cell(Plugin& plugin,
                                          const FormKey& cell,
                                          std::int32_t x,
                                          std::int32_t y) {
  auto next = plugin;
  assign_record_form_keys(next);

  auto located = fo4::detail::find_mutable_location(next, cell);
  if (!located.has_value()) return {CellRelocateStatus::cell_missing};

  Record& record = located->record();
  if (record.header.signature != sig_cell) return {CellRelocateStatus::not_a_cell};
  if (record.header.flags.is_deleted()) return {CellRelocateStatus::cell_deleted};
  if (record.undecodable) return {CellRelocateStatus::malformed};

  const auto& path = located->path;
  if (path.size() < 2) return {CellRelocateStatus::not_exterior};
  const auto group_type_of = [&](std::size_t index) {
    return static_cast<GroupType>(path[index]->header.group_type);
  };
  if (group_type_of(1) != GroupType::world_children) {
    return {CellRelocateStatus::not_exterior};
  }
  if (path.size() == 2) {
    // The persistent exterior CELL must stay directly under the worldspace children group.
    return {CellRelocateStatus::persistent_cell};
  }
  if (path.size() != 4 || group_type_of(0) != GroupType::top ||
      group_type_of(2) != GroupType::exterior_cell_block ||
      group_type_of(3) != GroupType::exterior_cell_subblock) {
    return {CellRelocateStatus::malformed};
  }

  std::optional<std::pair<std::int32_t, std::int32_t>> current;
  try {
    current = cell_grid(record);
  } catch (const CellError&) {
    return {CellRelocateStatus::malformed};
  }
  if (!current.has_value()) return {CellRelocateStatus::malformed};
  if (current->first == x && current->second == y) {
    return {CellRelocateStatus::unchanged};
  }
  if (!exterior_cell_coordinate_valid(x) || !exterior_cell_coordinate_valid(y)) {
    return {CellRelocateStatus::invalid_coordinate};
  }

  ExteriorCellGridLayout layout{};
  std::uint32_t expected_block = 0;
  std::uint32_t expected_subblock = 0;
  try {
    layout = exterior_cell_grid_layout(x, y);
    expected_block = exterior_grid_group_label_u32(
        floor_div(current->first, k_exterior_cell_block_span),
        floor_div(current->second, k_exterior_cell_block_span));
    expected_subblock = exterior_grid_group_label_u32(
        floor_div(current->first, k_exterior_cell_subblock_span),
        floor_div(current->second, k_exterior_cell_subblock_span));
  } catch (const CellError&) {
    return {CellRelocateStatus::malformed};
  }
  // A CELL whose XCLC already contradicts its labels must not spread to a second coordinate.
  if (path[2]->header.label_as_u32() != expected_block ||
      path[3]->header.label_as_u32() != expected_subblock) {
    return {CellRelocateStatus::malformed};
  }

  Group* world_children = path[1];

  const std::uint32_t destination_block =
      exterior_grid_group_label_u32(layout.block_x, layout.block_y);
  const std::uint32_t destination_subblock =
      exterior_grid_group_label_u32(layout.subblock_x, layout.subblock_y);

  try {
    auto* block =
        find_group(*world_children, GroupType::exterior_cell_block, destination_block);
    auto* subblock = block == nullptr
                         ? nullptr
                         : find_group(*block, GroupType::exterior_cell_subblock,
                                      destination_subblock);
    if (subblock != nullptr) {
      for (const auto& child : subblock->children) {
        const auto* other = std::get_if<Record>(&child);
        if (other == nullptr || other->header.signature != sig_cell) continue;
        if (other->header.flags.is_persistent()) continue;
        if (other->undecodable) return {CellRelocateStatus::malformed};
        const auto other_grid = cell_grid(*other);
        if (!other_grid.has_value()) return {CellRelocateStatus::malformed};
        if (other_grid->first == x && other_grid->second == y) {
          return {CellRelocateStatus::destination_occupied};
        }
      }
    }
  } catch (const CellError&) {
    return {CellRelocateStatus::malformed};
  }

  // Ancestor inserts move the groups on this path, so the record must be detached first.
  Group& source_subblock = *path[3];
  const auto source_block_label = path[2]->header.label_as_u32();
  const auto source_subblock_label = path[3]->header.label_as_u32();
  const auto index = located->index;
  std::optional<Group> cell_children;
  if (index + 1 < source_subblock.children.size()) {
    auto* following = std::get_if<Group>(&source_subblock.children[index + 1]);
    if (following != nullptr && is_owned_child_group_type(following->header.group_type)) {
      if (following->header.label_as_u32() != record.header.form_id.value()) {
        return {CellRelocateStatus::malformed};
      }
      cell_children = std::move(*following);
    }
  }
  Record moved = std::move(record);
  source_subblock.children.erase(
      source_subblock.children.begin() + static_cast<std::ptrdiff_t>(index),
      source_subblock.children.begin() +
          static_cast<std::ptrdiff_t>(index + 1 + (cell_children.has_value() ? 1 : 0)));

  Subrecord* xclc = nullptr;
  for (auto& subrecord : moved.subrecords) {
    if (subrecord.signature != sig_xclc) continue;
    if (xclc != nullptr) return {CellRelocateStatus::malformed};
    xclc = &subrecord;
  }
  if (xclc == nullptr || xclc->data.size() < k_xclc_minimum_size) {
    return {CellRelocateStatus::malformed};
  }
  std::memcpy(xclc->data.data(), &x, sizeof(x));
  std::memcpy(xclc->data.data() + sizeof(x), &y, sizeof(y));
  moved.original_range.clear();
  moved.header.data_size = 0;
  moved.uncompressed_size = 0;

  if (cell_children.has_value()) {
    const ChildShift context{
        next, current->first, current->second, x, y,
        Shift{static_cast<float>(x - current->first) * k_cell_world_units_f32,
              static_cast<float>(y - current->second) * k_cell_world_units_f32}};
    if (!shift_children(context, *cell_children)) return {CellRelocateStatus::malformed};
  }

  // An emptied source sub-block must not leave an empty block group behind.
  try {
    if (auto* source_block =
            find_group(*world_children, GroupType::exterior_cell_block, source_block_label)) {
      erase_group_if_empty(*source_block, GroupType::exterior_cell_subblock,
                           source_subblock_label);
      if (source_block->children.empty()) {
        erase_group_if_empty(*world_children, GroupType::exterior_cell_block,
                             source_block_label);
      }
    }

    auto* destination = ensure_group(*world_children, GroupType::exterior_cell_block,
                                     destination_block);
    auto* destination_group = ensure_group(*destination, GroupType::exterior_cell_subblock,
                                           destination_subblock);
    insert_cell(*destination_group, std::move(moved), std::move(cell_children));
  } catch (const CellError&) {
    return {CellRelocateStatus::malformed};
  }

  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
  return {CellRelocateStatus::relocated};
}

}  // namespace fo4::forms
