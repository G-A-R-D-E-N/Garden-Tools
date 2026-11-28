#include "fo4/plugin/navmesh_structure.hpp"

#include "placed_reference_copy_internal.hpp"
#include "fo4/flags.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/signature.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace fo4 {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_xclc = Signature::from_string("XCLC");
constexpr auto k_navm = Signature::from_string("NAVM");
constexpr auto k_nvnm = Signature::from_string("NVNM");
constexpr std::uint32_t k_navmesh_version = 15;
constexpr std::uint32_t k_pathing_cell_hash = 0xA5E9A03Cu;
constexpr std::size_t k_counted_blocks = 7;
constexpr std::size_t k_grid_header_words = 9;

struct CellSlot {
  Group* parent{nullptr};
  std::uint32_t world_label{0};
};

CellSlot find_cell_slot(Group& group, const FormKey& cell, std::uint32_t world_label) {
  for (auto& child : group.children) {
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->form_key == cell && record->header.signature == k_cell) {
        return {&group, world_label};
      }
    } else if (auto* nested = std::get_if<Group>(&child)) {
      const auto nested_world =
          nested->header.group_type == static_cast<std::int32_t>(GroupType::world_children)
              ? nested->header.label_as_u32()
              : world_label;
      const auto found = find_cell_slot(*nested, cell, nested_world);
      if (found.parent != nullptr) return found;
    }
  }
  return {};
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  const auto* begin = reinterpret_cast<const std::byte*>(&value);
  out.insert(out.end(), begin, begin + sizeof value);
}

std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof value);
  return value;
}

std::vector<std::byte> empty_geometry(std::uint32_t parent_world, std::uint32_t parent) {
  std::vector<std::byte> out;
  append_u32(out, k_navmesh_version);
  append_u32(out, k_pathing_cell_hash);
  append_u32(out, parent_world);
  append_u32(out, parent);
  for (std::size_t block = 0; block < k_counted_blocks; ++block) append_u32(out, 0);
  for (std::size_t word = 0; word < k_grid_header_words; ++word) append_u32(out, 0);
  return out;
}

std::uint32_t exterior_parent_word(const Subrecord& grid) {
  if (grid.data.size() < 8) throw schema::ScalarError("exterior navmesh CELL XCLC is too short");
  const auto grid_x = read_i32(grid.data, 0);
  const auto grid_y = read_i32(grid.data, 4);
  if (grid_x < std::numeric_limits<std::int16_t>::min() ||
      grid_x > std::numeric_limits<std::int16_t>::max() ||
      grid_y < std::numeric_limits<std::int16_t>::min() ||
      grid_y > std::numeric_limits<std::int16_t>::max()) {
    throw schema::ScalarError("exterior navmesh grid coordinate does not fit a signed 16-bit pair");
  }
  return static_cast<std::uint32_t>(static_cast<std::uint16_t>(grid_y)) |
         (static_cast<std::uint32_t>(static_cast<std::uint16_t>(grid_x)) << 16);
}

}

FormKey create_navmesh(Plugin& plugin, const FormKey& cell) {
  auto next = plugin;
  assign_record_form_keys(next);

  // An override of a master CELL keeps the master as owner, so presence, not ownership, must gate this.
  const auto* cell_record = find_record(next, cell);
  if (cell_record == nullptr || cell_record->header.signature != k_cell) {
    throw schema::ScalarError("navmesh parent is not a CELL record in this plugin");
  }
  require_decodable(*cell_record);
  const auto navm_form_version = next.header.form_version;
  if (navm_form_version < 128) {
    throw schema::ScalarError("navmesh creation requires form version 128 or later");
  }
  const auto* data = find_subrecord(*cell_record, k_data);
  if (data == nullptr || data->data.empty()) throw schema::ScalarError("navmesh parent CELL is missing DATA");
  const bool interior = (std::to_integer<std::uint8_t>(data->data[0]) & 1u) != 0;

  CellSlot slot;
  for (auto& group : next.groups) {
    slot = find_cell_slot(group, cell, 0);
    if (slot.parent != nullptr) break;
  }
  if (slot.parent == nullptr) throw schema::ScalarError("navmesh parent CELL is not inside a GRUP in this plugin");

  const auto context = plugin_form_id_context(next);
  const auto cell_label = encode_plugin_form_id(cell, context).value();
  std::uint32_t parent_world = 0;
  std::uint32_t parent_word = static_cast<std::uint32_t>(cell_label);
  if (!interior) {
    if (slot.world_label == 0) {
      throw schema::ScalarError("exterior navmesh CELL is not inside a worldspace group");
    }
    const auto* grid = find_subrecord(*cell_record, k_xclc);
    if (grid == nullptr) throw schema::ScalarError("exterior navmesh CELL is missing XCLC");
    parent_world = slot.world_label;
    parent_word = exterior_parent_word(*grid);
  }

  auto* children = forms::detail::ensure_cell_children_group(
      *slot.parent, forms::detail::group_header(GroupType::cell_children, cell_label), cell, context, context);
  auto* bucket = forms::detail::ensure_reference_bucket(
      *children, forms::detail::group_header(GroupType::cell_temporary_children, cell_label), context, context);

  const auto key = reserve_local_form_key(next);
  Record record;
  record.header.signature = k_navm;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  record.header.form_version = navm_form_version;
  record.form_key = key;
  record.subrecords.push_back(Subrecord{k_nvnm, empty_geometry(parent_world, parent_word), false});
  bucket->children.emplace_back(std::move(record));

  next.original_range.clear();
  plugin = std::move(next);
  return key;
}

}
