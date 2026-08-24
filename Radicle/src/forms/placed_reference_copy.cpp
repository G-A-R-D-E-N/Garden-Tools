#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_wrld = Signature::from_string("WRLD");
constexpr auto k_refr = Signature::from_string("REFR");
constexpr auto k_achr = Signature::from_string("ACHR");

struct RecordLocation {
  const Record* record{nullptr};
  std::vector<const Group*> path;
};

void locate_record_in_group(const Group& group,
                            const FormKey& key,
                            std::vector<const Group*>& current,
                            RecordLocation& found) {
  current.push_back(&group);
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (record->form_key != key) continue;
      if (found.record != nullptr) {
        throw PlacedReferenceError(
            "duplicate record FormKey while resolving placed-reference containment: " +
            key.to_string());
      }
      found.record = record;
      found.path = current;
      continue;
    }
    locate_record_in_group(std::get<Group>(child), key, current, found);
  }
  current.pop_back();
}

RecordLocation locate_record(const Plugin& plugin, const FormKey& key) {
  RecordLocation found;
  std::vector<const Group*> current;
  for (const auto& group : plugin.groups) {
    locate_record_in_group(group, key, current, found);
  }
  return found;
}

bool label_is_form_id(GroupType type) {
  switch (type) {
    case GroupType::world_children:
    case GroupType::cell_children:
    case GroupType::topic_children:
    case GroupType::cell_persistent_children:
    case GroupType::cell_temporary_children:
    case GroupType::cell_visible_distant_children:
      return true;
    case GroupType::top:
    case GroupType::interior_cell_block:
    case GroupType::interior_cell_subblock:
    case GroupType::exterior_cell_block:
    case GroupType::exterior_cell_subblock:
      return false;
  }
  throw PlacedReferenceError("unknown GRUP type in placed-reference containment");
}

std::array<std::byte, 4> u32_label(std::uint32_t value) {
  return {
      std::byte{static_cast<std::uint8_t>(value)},
      std::byte{static_cast<std::uint8_t>(value >> 8)},
      std::byte{static_cast<std::uint8_t>(value >> 16)},
      std::byte{static_cast<std::uint8_t>(value >> 24)},
  };
}

GroupHeader remap_group_header(const GroupHeader& source,
                               const PluginFormIdContext& source_context,
                               const PluginFormIdContext& destination_context) {
  auto out = source;
  out.group_size = 0;
  const auto type = static_cast<GroupType>(source.group_type);
  if (!label_is_form_id(type)) return out;

  const RawFormId raw{source.label_as_u32()};
  if (raw.is_null()) {
    throw PlacedReferenceError("FormID-bearing GRUP has a null label");
  }
  const auto key = resolve_plugin_form_id(raw, source_context);
  out.label = u32_label(encode_plugin_form_id(key, destination_context).value());
  return out;
}

bool same_group_identity(const GroupHeader& left, const GroupHeader& right) {
  return left.group_type == right.group_type && left.label == right.label;
}

Group* unique_top_group(Plugin& plugin, const GroupHeader& wanted) {
  Group* found = nullptr;
  for (auto& group : plugin.groups) {
    if (!same_group_identity(group.header, wanted)) continue;
    if (found != nullptr) {
      throw PlacedReferenceError("duplicate top-level GRUP while copying placed reference");
    }
    found = &group;
  }
  return found;
}

Group* unique_child_group(Group& parent, const GroupHeader& wanted) {
  Group* found = nullptr;
  for (auto& child : parent.children) {
    auto* group = std::get_if<Group>(&child);
    if (group == nullptr || !same_group_identity(group->header, wanted)) continue;
    if (found != nullptr) {
      throw PlacedReferenceError("duplicate nested GRUP while copying placed reference");
    }
    found = group;
  }
  return found;
}

Group* ensure_group_path(Plugin& plugin,
                         const std::vector<const Group*>& source_path,
                         const PluginFormIdContext& source_context,
                         const PluginFormIdContext& destination_context) {
  if (source_path.empty()) {
    throw PlacedReferenceError("record containment path is empty");
  }

  const auto top_header = remap_group_header(
      source_path.front()->header, source_context, destination_context);
  if (top_header.group_type != static_cast<std::int32_t>(GroupType::top)) {
    throw PlacedReferenceError("record containment does not begin at a top-level GRUP");
  }

  auto* current = unique_top_group(plugin, top_header);
  if (current == nullptr) {
    Group created;
    created.header = top_header;
    plugin.groups.push_back(std::move(created));
    current = &plugin.groups.back();
  }

  for (std::size_t index = 1; index < source_path.size(); ++index) {
    const auto header = remap_group_header(
        source_path[index]->header, source_context, destination_context);
    auto* child = unique_child_group(*current, header);
    if (child == nullptr) {
      Group created;
      created.header = header;
      current->children.emplace_back(std::move(created));
      child = &std::get<Group>(current->children.back());
    }
    current = child;
  }
  return current;
}

Group* ensure_cell_children_group(Group& parent,
                                  const GroupHeader& source_header,
                                  const FormKey& cell,
                                  const PluginFormIdContext& source_context,
                                  const PluginFormIdContext& destination_context) {
  const auto header = remap_group_header(
      source_header, source_context, destination_context);
  if (header.group_type != static_cast<std::int32_t>(GroupType::cell_children)) {
    throw PlacedReferenceError("placed reference is not beneath a CELL children GRUP");
  }
  if (auto* existing = unique_child_group(parent, header); existing != nullptr) {
    return existing;
  }

  Group created;
  created.header = header;
  for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
    const auto* record = std::get_if<Record>(&*it);
    if (record == nullptr || record->form_key != cell) continue;
    return &std::get<Group>(*parent.children.insert(std::next(it), std::move(created)));
  }
  throw PlacedReferenceError(
      "destination CELL record disappeared before its child GRUP was created");
}

Group* ensure_reference_bucket(Group& cell_children,
                               const GroupHeader& source_header,
                               const PluginFormIdContext& source_context,
                               const PluginFormIdContext& destination_context) {
  const auto header = remap_group_header(
      source_header, source_context, destination_context);
  const auto type = static_cast<GroupType>(header.group_type);
  if (type != GroupType::cell_persistent_children &&
      type != GroupType::cell_temporary_children) {
    throw PlacedReferenceError(
        "placed reference is not in a persistent or temporary CELL bucket");
  }
  if (auto* existing = unique_child_group(cell_children, header); existing != nullptr) {
    return existing;
  }
  Group created;
  created.header = header;
  cell_children.children.emplace_back(std::move(created));
  return &std::get<Group>(cell_children.children.back());
}

PluginKind owner_kind(const Plugin& source, const PluginKey& owner) {
  if (owner == source.key) {
    return source.header.tes4_flags.is_small_file() ? PluginKind::light
                                                   : PluginKind::full;
  }
  for (const auto& master : source.header.masters) {
    if (master.key == owner) return master.kind;
  }
  return owner.looks_light() ? PluginKind::light : PluginKind::full;
}

bool has_master(const Plugin& plugin, const PluginKey& owner) {
  return std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                     [&](const auto& master) { return master.key == owner; });
}

void collect_group_label_owner(const Group& group,
                               const PluginFormIdContext& source_context,
                               std::set<PluginKey>& owners) {
  const auto type = static_cast<GroupType>(group.header.group_type);
  if (!label_is_form_id(type)) return;
  const RawFormId raw{group.header.label_as_u32()};
  if (raw.is_null()) {
    throw PlacedReferenceError("FormID-bearing GRUP has a null label");
  }
  const auto key = resolve_plugin_form_id(raw, source_context);
  if (!key.owner().empty()) owners.insert(key.owner());
}

Record copy_record_for_destination(const Record& source_record,
                                   const FormKey& key,
                                   const PluginFormIdContext& source_context,
                                   const PluginFormIdContext& destination_context) {
  auto copied = source_record;
  copied.header.form_id = encode_plugin_form_id(key, destination_context);
  remap_record_form_links(copied, source_context, destination_context);
  copied.original_range.clear();
  copied.decompressed_payload.clear();
  copied.header.data_size = 0;
  copied.uncompressed_size = 0;
  return copied;
}

bool replace_record(Plugin& plugin, const FormKey& key, Record replacement) {
  bool replaced = false;
  visit_records(plugin, [&](Record& record) {
    if (replaced || record.header.signature == sig_tes4 || record.form_key != key) return;
    record = std::move(replacement);
    replaced = true;
  });
  return replaced;
}

void validate_destination_record_path(
    const Plugin& destination,
    const FormKey& key,
    Signature signature,
    const std::vector<const Group*>& source_path,
    const PluginFormIdContext& source_context,
    const PluginFormIdContext& destination_context,
    std::string_view label) {
  const auto located = locate_record(destination, key);
  if (located.record == nullptr) return;
  if (located.record->header.signature != signature) {
    throw PlacedReferenceError(
        "destination " + std::string(label) + " FormKey collides with another record type");
  }
  if (located.path.size() != source_path.size()) {
    throw PlacedReferenceError(
        "destination " + std::string(label) + " containment differs from source containment");
  }
  for (std::size_t index = 0; index < source_path.size(); ++index) {
    const auto expected = remap_group_header(
        source_path[index]->header, source_context, destination_context);
    if (!same_group_identity(located.path[index]->header, expected)) {
      throw PlacedReferenceError(
          "destination " + std::string(label) + " containment differs from source containment");
    }
  }
}

}  // namespace

PlacedReferenceCopyStatus copy_placed_reference_as_override(
    Plugin& destination,
    const Plugin& source,
    const FormKey& reference,
    bool overwrite) {
  const auto source_reference = locate_record(source, reference);
  if (source_reference.record == nullptr) {
    return PlacedReferenceCopyStatus::source_missing;
  }
  if (source_reference.record->header.signature != k_refr &&
      source_reference.record->header.signature != k_achr) {
    throw PlacedReferenceError("placed-reference override requires a REFR or ACHR source record");
  }

  std::optional<std::size_t> cell_children_index;
  for (std::size_t index = 0; index < source_reference.path.size(); ++index) {
    if (source_reference.path[index]->header.group_type !=
        static_cast<std::int32_t>(GroupType::cell_children)) {
      continue;
    }
    if (cell_children_index.has_value()) {
      throw PlacedReferenceError("placed-reference containment has multiple CELL children GRUPs");
    }
    cell_children_index = index;
  }
  if (!cell_children_index.has_value() ||
      *cell_children_index + 2 != source_reference.path.size()) {
    throw PlacedReferenceError(
        "placed-reference containment must end in CELL children plus persistent/temporary GRUP");
  }

  const auto source_context = plugin_form_id_context(source);
  const auto cell_children = source_reference.path[*cell_children_index];
  const RawFormId cell_raw{cell_children->header.label_as_u32()};
  if (cell_raw.is_null()) {
    throw PlacedReferenceError("CELL children GRUP has a null cell FormID label");
  }
  const auto cell = resolve_plugin_form_id(cell_raw, source_context);
  const auto source_cell = locate_record(source, cell);
  if (source_cell.record == nullptr || source_cell.record->header.signature != k_cell) {
    throw PlacedReferenceError("placed-reference parent CELL record is missing from the source plugin");
  }
  if (source_cell.path.size() != *cell_children_index) {
    throw PlacedReferenceError("parent CELL and placed reference do not share the same containment root");
  }
  for (std::size_t index = 0; index < source_cell.path.size(); ++index) {
    if (source_cell.path[index] != source_reference.path[index]) {
      throw PlacedReferenceError("parent CELL and placed reference do not share the same containment root");
    }
  }

  std::optional<FormKey> world;
  std::optional<RecordLocation> source_world;
  for (std::size_t index = 0; index < source_cell.path.size(); ++index) {
    const auto* group = source_cell.path[index];
    if (group->header.group_type != static_cast<std::int32_t>(GroupType::world_children)) {
      continue;
    }
    if (world.has_value()) {
      throw PlacedReferenceError("CELL containment has multiple WRLD children GRUPs");
    }
    const RawFormId world_raw{group->header.label_as_u32()};
    if (world_raw.is_null()) {
      throw PlacedReferenceError("WRLD children GRUP has a null worldspace FormID label");
    }
    world = resolve_plugin_form_id(world_raw, source_context);
    source_world = locate_record(source, *world);
    if (source_world->record == nullptr || source_world->record->header.signature != k_wrld) {
      throw PlacedReferenceError("exterior CELL parent WRLD record is missing from the source plugin");
    }
    if (source_world->path.size() != index) {
      throw PlacedReferenceError("WRLD record and exterior CELL do not share the same containment root");
    }
    for (std::size_t prefix = 0; prefix < source_world->path.size(); ++prefix) {
      if (source_world->path[prefix] != source_cell.path[prefix]) {
        throw PlacedReferenceError("WRLD record and exterior CELL do not share the same containment root");
      }
    }
  }

  auto next = destination;
  assign_record_form_keys(next);
  const bool target_exists = find_record(next, reference) != nullptr;
  if (target_exists && !overwrite) {
    return PlacedReferenceCopyStatus::already_exists;
  }

  std::set<PluginKey> required_owners;
  if (!reference.owner().empty()) required_owners.insert(reference.owner());
  if (!cell.owner().empty()) required_owners.insert(cell.owner());
  if (world.has_value() && !world->owner().empty()) required_owners.insert(world->owner());
  for (const auto* group : source_reference.path) {
    collect_group_label_owner(*group, source_context, required_owners);
  }
  const auto links = collect_form_links_detailed(source);
  for (const auto& link : links.links) {
    const bool relevant = link.record == reference || link.record == cell ||
                          (world.has_value() && link.record == *world);
    if (!relevant || link.key.owner().empty()) continue;
    required_owners.insert(link.key.owner());
  }

  std::vector<MasterDescriptor> masters;
  masters.reserve(next.header.masters.size() + required_owners.size());
  for (const auto& master : next.header.masters) {
    masters.push_back({master.key, master.kind});
  }
  bool added_master = false;
  for (const auto& owner : required_owners) {
    if (owner == next.key || has_master(next, owner)) continue;
    masters.push_back({owner, owner_kind(source, owner)});
    added_master = true;
  }
  if (added_master) {
    next = remap_plugin_masters(std::move(next), std::move(masters));
  }

  const auto destination_context = plugin_form_id_context(next);

  if (world.has_value() && source_world.has_value()) {
    validate_destination_record_path(
        next, *world, k_wrld, source_world->path,
        source_context, destination_context, "WRLD");
    auto* world_parent = ensure_group_path(
        next, source_world->path, source_context, destination_context);
    if (find_record(next, *world) == nullptr) {
      world_parent->children.emplace_back(copy_record_for_destination(
          *source_world->record, *world, source_context, destination_context));
    }
  }

  validate_destination_record_path(
      next, cell, k_cell, source_cell.path,
      source_context, destination_context, "CELL");
  auto* cell_parent = ensure_group_path(
      next, source_cell.path, source_context, destination_context);
  if (find_record(next, cell) == nullptr) {
    cell_parent->children.emplace_back(copy_record_for_destination(
        *source_cell.record, cell, source_context, destination_context));
  }

  auto* destination_cell_children = ensure_cell_children_group(
      *cell_parent,
      source_reference.path[*cell_children_index]->header,
      cell,
      source_context,
      destination_context);
  auto* bucket = ensure_reference_bucket(
      *destination_cell_children,
      source_reference.path.back()->header,
      source_context,
      destination_context);

  auto copied_reference = copy_record_for_destination(
      *source_reference.record, reference, source_context, destination_context);
  if (target_exists) {
    if (!replace_record(next, reference, std::move(copied_reference))) {
      throw PlacedReferenceError("destination placed reference disappeared during override replacement");
    }
  } else {
    bucket->children.emplace_back(std::move(copied_reference));
  }

  next.original_range.clear();
  destination = std::move(next);
  return target_exists ? PlacedReferenceCopyStatus::replaced
                       : PlacedReferenceCopyStatus::inserted;
}

}  // namespace fo4::forms
