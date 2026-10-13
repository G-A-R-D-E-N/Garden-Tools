#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_refr = Signature::from_string("REFR");
constexpr auto k_name = Signature::from_string("NAME");
constexpr auto k_data = Signature::from_string("DATA");

std::array<std::byte, 4> u32_bytes(std::uint32_t value) {
  std::array<std::byte, 4> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
  return bytes;
}

GroupHeader group_header(GroupType type, std::uint32_t label) {
  GroupHeader header;
  header.group_type = static_cast<std::int32_t>(type);
  header.label = u32_bytes(label);
  return header;
}

struct CellSlot {
  Group* parent{nullptr};
  std::size_t index{0};
};

CellSlot find_cell_slot(Group& group, const FormKey& cell) {
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    auto& child = group.children[index];
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->form_key == cell && record->header.signature == k_cell) return {&group, index};
    } else if (auto* nested = std::get_if<Group>(&child)) {
      const auto found = find_cell_slot(*nested, cell);
      if (found.parent != nullptr) return found;
    }
  }
  return {};
}

Group* ensure_child_group(Group& parent, const GroupHeader& header, std::size_t position) {
  if (auto* existing = detail::unique_child_group(parent, header)) return existing;
  Group created;
  created.header = header;
  const auto inserted = parent.children.insert(
      parent.children.begin() + static_cast<std::ptrdiff_t>(position), std::move(created));
  return &std::get<Group>(*inserted);
}

std::vector<std::byte> transform_bytes(const PositionRotation& transform) {
  std::vector<std::byte> data;
  data.reserve(24);
  for (const float value : {transform.x, transform.y, transform.z,
                            transform.rx, transform.ry, transform.rz}) {
    const auto bytes = u32_bytes(std::bit_cast<std::uint32_t>(value));
    data.insert(data.end(), bytes.begin(), bytes.end());
  }
  return data;
}

}  // namespace

FormKey create_placed_reference(Plugin& plugin, const PlacedReferenceCreation& creation) {
  auto next = plugin;
  assign_record_form_keys(next);

  if (creation.cell.owner() != next.key) {
    throw PlacedReferenceError("placed reference CELL must be owned by this plugin");
  }
  const auto* cell_record = find_record(next, creation.cell);
  if (cell_record == nullptr || cell_record->header.signature != k_cell) {
    throw PlacedReferenceError("placed reference CELL is not a CELL record in this plugin");
  }
  require_decodable(*cell_record);
  if (creation.base.owner() != next.key && !detail::has_master(next, creation.base.owner())) {
    throw PlacedReferenceError("placed reference base must come from this plugin or one of its masters");
  }

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
  const auto base_bytes = u32_bytes(encode_plugin_form_id(creation.base, context).value());

  auto* children = ensure_child_group(
      *slot.parent, group_header(GroupType::cell_children, cell_label), slot.index + 1);
  const auto bucket_type = creation.persistent ? GroupType::cell_persistent_children
                                               : GroupType::cell_temporary_children;
  auto* bucket = ensure_child_group(
      *children, group_header(bucket_type, cell_label), children->children.size());

  const auto key = reserve_local_form_key(next);
  Record record;
  record.header.signature = k_refr;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  record.header.form_version = next.header.form_version;
  record.header.flags = creation.persistent ? RecordFlags{RecordFlags::persistent} : RecordFlags{};
  record.form_key = key;
  record.subrecords.push_back(
      Subrecord{k_name, std::vector<std::byte>(base_bytes.begin(), base_bytes.end()), false});
  record.subrecords.push_back(Subrecord{k_data, transform_bytes(creation.transform), false});
  bucket->children.emplace_back(std::move(record));

  next.original_range.clear();
  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
