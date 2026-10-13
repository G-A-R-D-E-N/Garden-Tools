#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_refr = Signature::from_string("REFR");
constexpr auto k_name = Signature::from_string("NAME");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_xclc = Signature::from_string("XCLC");
constexpr float k_exterior_cell_span = 4096.0f;

GroupHeader group_header(GroupType type, std::uint32_t label) {
  GroupHeader header;
  header.group_type = static_cast<std::int32_t>(type);
  header.label = detail::u32_label(label);
  return header;
}

struct CellSlot {
  Group* parent{nullptr};
};

CellSlot find_cell_slot(Group& group, const FormKey& cell) {
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    auto& child = group.children[index];
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->form_key == cell && record->header.signature == k_cell) return {&group};
    } else if (auto* nested = std::get_if<Group>(&child)) {
      const auto found = find_cell_slot(*nested, cell);
      if (found.parent != nullptr) return found;
    }
  }
  return {};
}

const Subrecord* find_subrecord(const Record& record, Signature signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

std::int32_t grid_coordinate(float world) {
  const auto cells = std::floor(world / k_exterior_cell_span);
  if (!(std::fabs(cells) < 2147483648.0f)) {
    throw PlacedReferenceError("placed reference position is not a finite grid coordinate");
  }
  return static_cast<std::int32_t>(cells);
}

void require_placement_in_cell(const Record& cell, const PlacedReferenceCreation& creation) {
  const auto* data = find_subrecord(cell, k_data);
  if (data == nullptr || data->data.empty()) {
    throw PlacedReferenceError("placed reference CELL is missing DATA");
  }
  if ((std::to_integer<std::uint8_t>(data->data[0]) & 1u) != 0) return;

  if (creation.persistent) {
    throw PlacedReferenceError(
        "persistent references in exterior cells go in the worldspace persistent cell; not supported yet");
  }
  const auto* grid = find_subrecord(cell, k_xclc);
  if (grid == nullptr || grid->data.size() < 8) {
    throw PlacedReferenceError("exterior placed reference CELL is missing XCLC");
  }
  if (grid_coordinate(creation.transform.x) != read_i32(grid->data, 0) ||
      grid_coordinate(creation.transform.y) != read_i32(grid->data, 4)) {
    throw PlacedReferenceError("placed reference position is outside its exterior CELL grid");
  }
}

}  // namespace

FormKey create_placed_reference(Plugin& plugin, const PlacedReferenceCreation& creation) {
  auto next = plugin;
  assign_record_form_keys(next);

  if (creation.cell.owner() != next.key) {
    throw PlacedReferenceError(
        "placed reference CELL must be owned by this plugin; copy the cell as an override first");
  }
  const auto* cell_record = find_record(next, creation.cell);
  if (cell_record == nullptr || cell_record->header.signature != k_cell) {
    throw PlacedReferenceError("placed reference CELL is not a CELL record in this plugin");
  }
  require_decodable(*cell_record);
  if (creation.base.owner() == next.key) {
    if (find_record(next, creation.base) == nullptr) {
      throw PlacedReferenceError("placed reference base is not a record in this plugin");
    }
  } else if (!detail::has_master(next, creation.base.owner())) {
    throw PlacedReferenceError("placed reference base must come from this plugin or one of its masters");
  }
  require_placement_in_cell(*cell_record, creation);

  CellSlot slot;
  for (auto& group : next.groups) {
    slot = find_cell_slot(group, creation.cell);
    if (slot.parent != nullptr) break;
  }
  if (slot.parent == nullptr) {
    throw PlacedReferenceError("placed reference CELL is not inside a GRUP in this plugin");
  }

  const auto context = plugin_form_id_context(next);
  const auto cell_label = encode_plugin_form_id(creation.cell, context).value();
  const auto base_bytes = detail::u32_label(encode_plugin_form_id(creation.base, context).value());

  auto* children = detail::ensure_cell_children_group(
      *slot.parent, group_header(GroupType::cell_children, cell_label), creation.cell, context, context);
  const auto bucket_type = creation.persistent ? GroupType::cell_persistent_children
                                               : GroupType::cell_temporary_children;
  auto* bucket = detail::ensure_reference_bucket(
      *children, group_header(bucket_type, cell_label), context, context);

  const auto key = reserve_local_form_key(next);
  RecordFlags::value_type flags = creation.persistent ? RecordFlags::persistent : 0u;
  if (creation.initially_disabled) flags |= RecordFlags::initially_disabled;

  Record record;
  record.header.signature = k_refr;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  record.header.form_version = next.header.form_version;
  record.header.flags = RecordFlags{flags};
  record.form_key = key;
  record.subrecords.push_back(
      Subrecord{k_name, std::vector<std::byte>(base_bytes.begin(), base_bytes.end()), false});
  record.subrecords.push_back(Subrecord{k_data, std::vector<std::byte>(24, std::byte{0}), false});
  bucket->children.emplace_back(std::move(record));

  if (!set_placed_reference_transform(next, key, creation.transform)) {
    throw PlacedReferenceError("placed reference disappeared before its transform was written");
  }

  next.original_range.clear();
  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
