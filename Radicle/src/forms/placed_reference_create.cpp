#include "fo4/forms/placed_reference.hpp"
#include "fo4/binary/raw.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fo4::forms {
namespace {

using fo4::binary::raw::read_i32;

struct CellSlot {
  Group* parent{nullptr};
};

CellSlot find_cell_slot(Group& group, const FormKey& cell) {
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    auto& child = group.children[index];
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->form_key == cell && record->header.signature == sig_cell) return {&group};
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

void require_placement_in_cell(const Record& cell, const PlacedReferenceCreation& creation) {
  const auto* data = find_subrecord(cell, sig_data);
  if (data == nullptr || data->data.empty()) {
    throw PlacedReferenceError("placed reference CELL is missing DATA");
  }
  if ((std::to_integer<std::uint8_t>(data->data[0]) & 1u) != 0) return;

  if (creation.persistent) {
    throw PlacedReferenceError(
        "persistent references in exterior cells go in the worldspace persistent cell; not supported yet");
  }
  const auto* grid = find_subrecord(cell, sig_xclc);
  if (grid == nullptr || grid->data.size() < 8) {
    throw PlacedReferenceError("exterior placed reference CELL is missing XCLC");
  }
  if (detail::grid_coordinate(creation.transform.x) != read_i32(grid->data, 0) ||
      detail::grid_coordinate(creation.transform.y) != read_i32(grid->data, 4)) {
    throw PlacedReferenceError("placed reference position is outside its exterior CELL grid");
  }
}

}  // namespace

FormKey create_placed_reference(Plugin& plugin, const PlacedReferenceCreation& creation) {
  auto next = plugin;
  assign_record_form_keys(next);

  if (creation.cell.owner() != next.key && !creation.cell_is_override) {
    throw PlacedReferenceError(
        "placed reference CELL must be owned by this plugin; copy the cell as an override first");
  }
  if (creation.kind == PlacedReferenceKind::variant) {
    throw PlacedReferenceError("placed reference variants cannot be created yet");
  }
  const bool npc = creation.kind == PlacedReferenceKind::npc;
  const auto* cell_record = find_record(next, creation.cell);
  if (cell_record == nullptr || cell_record->header.signature != sig_cell) {
    throw PlacedReferenceError("placed reference CELL is not a CELL record in this plugin");
  }
  require_decodable(*cell_record);
  if (creation.base.owner() == next.key) {
    const auto* base_record = find_record(next, creation.base);
    if (base_record == nullptr) {
      throw PlacedReferenceError("placed reference base is not a record in this plugin");
    }
    if (npc && base_record->header.signature != sig_npc_) {
      throw PlacedReferenceError("placed NPC base is not an NPC_ record");
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
      *slot.parent, detail::group_header(GroupType::cell_children, cell_label), creation.cell,
      context, context);
  const auto bucket_type = creation.persistent ? GroupType::cell_persistent_children
                                               : GroupType::cell_temporary_children;
  auto* bucket = detail::ensure_reference_bucket(
      *children, detail::group_header(bucket_type, cell_label), context, context);

  const auto key = reserve_local_form_key(next);
  RecordFlags::value_type flags = creation.persistent ? RecordFlags::persistent : 0u;
  if (creation.initially_disabled) flags |= RecordFlags::initially_disabled;

  Record record;
  record.header.signature = npc ? sig_achr : sig_refr;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  record.header.form_version = next.header.form_version;
  record.header.flags = RecordFlags{flags};
  record.form_key = key;
  record.subrecords.push_back(
      Subrecord{sig_name, std::vector<std::byte>(base_bytes.begin(), base_bytes.end()), false});
  record.subrecords.push_back(Subrecord{sig_data, std::vector<std::byte>(24, std::byte{0}), false});
  bucket->children.emplace_back(std::move(record));

  if (!set_placed_reference_transform(next, key, creation.transform)) {
    throw PlacedReferenceError("placed reference disappeared before its transform was written");
  }

  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
  return key;
}

RawCellChildResult create_raw_cell_child(Plugin& plugin, const RawCellChildCreation& creation) {
  const auto constraint = schema::record_authoring_constraint(creation.signature);
  const bool placed = constraint && constraint->placement == schema::RecordPlacement::cell_reference_children;
  if (!placed && creation.signature != sig_land) return {RawCellChildStatus::not_a_cell_child_type, {}};
  if (creation.signature == sig_land && creation.persistent) return {RawCellChildStatus::persistent_land, {}};

  auto next = plugin;
  assign_record_form_keys(next);
  const auto* cell_record = find_record(next, creation.cell);
  if (cell_record == nullptr) return {RawCellChildStatus::cell_missing, {}};
  if (cell_record->header.signature != sig_cell) return {RawCellChildStatus::not_a_cell, {}};
  require_decodable(*cell_record);

  CellSlot slot;
  for (auto& group : next.groups) {
    slot = find_cell_slot(group, creation.cell);
    if (slot.parent != nullptr) break;
  }
  if (slot.parent == nullptr) throw PlacedReferenceError("CELL is not inside a GRUP in this plugin");

  FormKey key;
  if (creation.local_id == 0) {
    key = reserve_local_form_key(next);
  } else {
    const auto [lowest, highest] = local_form_id_range(next);
    if (creation.local_id < lowest || creation.local_id > highest) return {RawCellChildStatus::id_out_of_range, {}};
    key = FormKey{next.key, LocalFormId{creation.local_id}};
    if (find_record(next, key) != nullptr) return {RawCellChildStatus::id_taken, {}};
    if (creation.local_id >= (next.header.next_form_id & k_full_local_mask)) {
      set_hedr_next_form_id(next, creation.local_id + 1u);
    }
  }

  const auto context = plugin_form_id_context(next);
  const auto cell_label = encode_plugin_form_id(creation.cell, context).value();
  auto* children = detail::ensure_cell_children_group(
      *slot.parent, detail::group_header(GroupType::cell_children, cell_label), creation.cell, context, context);
  const auto bucket_type = creation.persistent ? GroupType::cell_persistent_children
                                               : GroupType::cell_temporary_children;
  auto* bucket = detail::ensure_reference_bucket(*children, detail::group_header(bucket_type, cell_label), context, context);

  Record record;
  record.header.signature = creation.signature;
  record.header.form_id = encode_plugin_form_id(key, context);
  record.header.form_version = creation.form_version != 0 ? creation.form_version : next.header.form_version;
  record.form_key = key;
  bucket->children.emplace_back(std::move(record));

  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
  return {RawCellChildStatus::created, key};
}

}  // namespace fo4::forms
