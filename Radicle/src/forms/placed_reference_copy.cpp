#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/form_link_mixed.hpp"
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
constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_dial = Signature::from_string("DIAL");
constexpr auto k_info = Signature::from_string("INFO");
constexpr auto k_qnam = Signature::from_string("QNAM");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr std::int32_t k_quest_children_group = 10;

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
    return classify_plugin_kind(source.header.tes4_flags, source.key);
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

struct PlacedContainment {
  RecordLocation reference;
  std::size_t cell_children_index{0};
  FormKey cell;
  RecordLocation source_cell;
  std::optional<FormKey> world;
  std::optional<RecordLocation> source_world;
};

PlacedContainment resolve_placed_containment(
    const Plugin& source,
    RecordLocation source_reference,
    const PluginFormIdContext& source_context) {
  PlacedContainment out;
  out.reference = std::move(source_reference);

  std::optional<std::size_t> cell_children_index;
  for (std::size_t index = 0; index < out.reference.path.size(); ++index) {
    if (out.reference.path[index]->header.group_type !=
        static_cast<std::int32_t>(GroupType::cell_children)) {
      continue;
    }
    if (cell_children_index.has_value()) {
      throw PlacedReferenceError("placed-reference containment has multiple CELL children GRUPs");
    }
    cell_children_index = index;
  }
  if (!cell_children_index.has_value() ||
      *cell_children_index + 2 != out.reference.path.size()) {
    throw PlacedReferenceError(
        "placed-reference containment must end in CELL children plus persistent/temporary GRUP");
  }
  out.cell_children_index = *cell_children_index;

  const auto* cell_children = out.reference.path[*cell_children_index];
  const RawFormId cell_raw{cell_children->header.label_as_u32()};
  if (cell_raw.is_null()) {
    throw PlacedReferenceError("CELL children GRUP has a null cell FormID label");
  }
  out.cell = resolve_plugin_form_id(cell_raw, source_context);
  out.source_cell = locate_record(source, out.cell);
  if (out.source_cell.record == nullptr || out.source_cell.record->header.signature != k_cell) {
    throw PlacedReferenceError("placed-reference parent CELL record is missing from the source plugin");
  }
  if (out.source_cell.path.size() != *cell_children_index) {
    throw PlacedReferenceError("parent CELL and placed reference do not share the same containment root");
  }
  for (std::size_t index = 0; index < out.source_cell.path.size(); ++index) {
    if (out.source_cell.path[index] != out.reference.path[index]) {
      throw PlacedReferenceError("parent CELL and placed reference do not share the same containment root");
    }
  }

  for (std::size_t index = 0; index < out.source_cell.path.size(); ++index) {
    const auto* group = out.source_cell.path[index];
    if (group->header.group_type != static_cast<std::int32_t>(GroupType::world_children)) {
      continue;
    }
    if (out.world.has_value()) {
      throw PlacedReferenceError("CELL containment has multiple WRLD children GRUPs");
    }
    const RawFormId world_raw{group->header.label_as_u32()};
    if (world_raw.is_null()) {
      throw PlacedReferenceError("WRLD children GRUP has a null worldspace FormID label");
    }
    out.world = resolve_plugin_form_id(world_raw, source_context);
    out.source_world = locate_record(source, *out.world);
    if (out.source_world->record == nullptr || out.source_world->record->header.signature != k_wrld) {
      throw PlacedReferenceError("exterior CELL parent WRLD record is missing from the source plugin");
    }
    if (out.source_world->path.size() != index) {
      throw PlacedReferenceError("WRLD record and exterior CELL do not share the same containment root");
    }
    for (std::size_t prefix = 0; prefix < out.source_world->path.size(); ++prefix) {
      if (out.source_world->path[prefix] != out.source_cell.path[prefix]) {
        throw PlacedReferenceError("WRLD record and exterior CELL do not share the same containment root");
      }
    }
  }
  return out;
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

FormKey resolve_group_form_key(const Group& group,
                               const PluginFormIdContext& context) {
  const RawFormId raw{group.header.label_as_u32()};
  if (raw.is_null()) {
    throw InfoCopyError("INFO containment group has a null label");
  }
  return resolve_plugin_form_id(raw, context);
}

const Subrecord* unique_info_subrecord(const Record& record, Signature signature) {
  const Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw InfoCopyError("INFO containment record has duplicate link subrecords");
    }
    found = &subrecord;
  }
  return found;
}

std::optional<FormKey> read_info_form_link(const Record& record,
                                           Signature signature,
                                           const PluginFormIdContext& context) {
  const auto* subrecord = unique_info_subrecord(record, signature);
  if (subrecord == nullptr) return std::nullopt;
  if (subrecord->data.size() != 4) {
    throw InfoCopyError("INFO link subrecord must be exactly 4 bytes");
  }
  std::uint32_t value = 0;
  std::memcpy(&value, subrecord->data.data(), sizeof(value));
  const RawFormId raw{value};
  if (raw.is_null()) return std::nullopt;
  return resolve_plugin_form_id(raw, context);
}

bool path_matches(const std::vector<const Group*>& left,
                  const std::vector<const Group*>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

struct InfoContainment {
  RecordLocation info;
  RecordLocation quest;
  RecordLocation dial;
  FormKey quest_key;
  FormKey dial_key;
};

InfoContainment resolve_info_containment(
    const Plugin& source,
    RecordLocation info,
    const PluginFormIdContext& source_context) {
  if (info.path.size() != 4) {
    throw InfoCopyError("INFO containment must use the complete QUST/DIAL ancestry");
  }
  const auto* quest_top = info.path[0];
  const auto* quest_children = info.path[1];
  const auto* dial_top = info.path[2];
  const auto* topic_children = info.path[3];
  if (quest_top->header.group_type != static_cast<std::int32_t>(GroupType::top) ||
      quest_top->header.top_label() != k_qust ||
      quest_children->header.group_type != k_quest_children_group ||
      dial_top->header.group_type != static_cast<std::int32_t>(GroupType::top) ||
      dial_top->header.top_label() != k_dial ||
      topic_children->header.group_type !=
          static_cast<std::int32_t>(GroupType::topic_children)) {
    throw InfoCopyError("INFO is not in the approved QUST/DIAL containment");
  }

  InfoContainment out;
  out.info = std::move(info);
  out.quest_key = resolve_group_form_key(*quest_children, source_context);
  out.dial_key = resolve_group_form_key(*topic_children, source_context);
  out.quest = locate_record(source, out.quest_key);
  out.dial = locate_record(source, out.dial_key);
  if (out.quest.record == nullptr || out.quest.record->header.signature != k_qust ||
      out.quest.path.size() != 1 || out.quest.path[0] != quest_top) {
    throw InfoCopyError("INFO Quest Children label does not resolve to its parent QUST");
  }
  if (out.dial.record == nullptr || out.dial.record->header.signature != k_dial ||
      out.dial.path.size() != 3 ||
      !path_matches(out.dial.path, {quest_top, quest_children, dial_top})) {
    throw InfoCopyError("INFO Topic Children label does not resolve to its parent DIAL");
  }
  if (!path_matches(out.info.path, {quest_top, quest_children, dial_top, topic_children})) {
    throw InfoCopyError("INFO and DIAL do not share the approved containment prefix");
  }
  std::size_t topic_group_count = 0;
  for (const auto& child : dial_top->children) {
    const auto* group = std::get_if<Group>(&child);
    if (group == nullptr ||
        group->header.group_type != static_cast<std::int32_t>(GroupType::topic_children)) {
      continue;
    }
    ++topic_group_count;
  }
  if (topic_group_count != 1) {
    throw InfoCopyError("DIAL must have one unambiguous Topic Children group");
  }

  const auto qnam = read_info_form_link(*out.dial.record, k_qnam, source_context);
  if (qnam.has_value() && *qnam != out.quest_key) {
    throw InfoCopyError("DIAL QNAM does not match its Quest Children parent");
  }
  return out;
}

Group* existing_group_path(Plugin& plugin,
                           const std::vector<const Group*>& source_path,
                           const PluginFormIdContext& source_context,
                           const PluginFormIdContext& destination_context) {
  if (source_path.empty()) {
    throw InfoCopyError("INFO destination containment path is empty");
  }
  auto* current = unique_top_group(
      plugin, remap_group_header(source_path.front()->header,
                                 source_context, destination_context));
  if (current == nullptr) {
    throw InfoCopyError("INFO destination top-level containment group is missing");
  }
  for (std::size_t index = 1; index < source_path.size(); ++index) {
    current = unique_child_group(
        current[0], remap_group_header(source_path[index]->header,
                                       source_context, destination_context));
    if (current == nullptr) {
      throw InfoCopyError("INFO destination containment group is missing");
    }
  }
  return current;
}

std::optional<std::size_t> direct_record_index(Group& group, const FormKey& key) {
  std::optional<std::size_t> found;
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    const auto* record = std::get_if<Record>(&group.children[index]);
    if (record == nullptr || record->form_key != key) continue;
    if (found.has_value()) {
      throw InfoCopyError("INFO destination group contains a duplicate record FormKey");
    }
    found = index;
  }
  return found;
}

Group* ensure_topic_children_group(
    Group& dial_group,
    const GroupHeader& source_header,
    const FormKey& dial_key,
    const PluginFormIdContext& source_context,
    const PluginFormIdContext& destination_context) {
  const auto wanted = remap_group_header(source_header, source_context, destination_context);
  if (wanted.group_type != static_cast<std::int32_t>(GroupType::topic_children)) {
    throw InfoCopyError("INFO containment does not end in Topic Children");
  }

  Group* found = nullptr;
  std::optional<std::size_t> found_index;
  for (std::size_t index = 0; index < dial_group.children.size(); ++index) {
    auto* group = std::get_if<Group>(&dial_group.children[index]);
    if (group == nullptr || group->header.group_type != wanted.group_type) continue;
    if (!same_group_identity(group->header, wanted)) {
      throw InfoCopyError("DIAL contains an unexpected Topic Children group");
    }
    if (found != nullptr) {
      throw InfoCopyError("DIAL contains duplicate Topic Children groups");
    }
    found = group;
    found_index = index;
  }

  const auto dial_index = direct_record_index(dial_group, dial_key);
  if (!dial_index.has_value()) {
    throw InfoCopyError("destination DIAL record disappeared before Topic Children insertion");
  }
  if (found != nullptr) {
    if (!found_index.has_value() || *found_index != *dial_index + 1) {
      throw InfoCopyError("DIAL Topic Children group is not adjacent to its DIAL record");
    }
    return found;
  }

  Group created;
  created.header = wanted;
  auto it = dial_group.children.begin() +
            static_cast<std::ptrdiff_t>(*dial_index + 1);
  return &std::get<Group>(*dial_group.children.insert(it, std::move(created)));
}

enum class InfoPnamState {
  absent,
  explicit_null,
  linked,
};

struct InfoPnam {
  InfoPnamState state{InfoPnamState::absent};
  FormKey previous{};
};

InfoPnam read_info_pnam(const Plugin& source,
                        const Record& info,
                        const PluginFormIdContext& source_context,
                        const Group* source_topic) {
  const auto* pnam = unique_info_subrecord(info, k_pnam);
  if (pnam == nullptr) return {};
  if (pnam->data.size() != 4) {
    throw InfoCopyError("INFO PNAM must be exactly 4 bytes");
  }
  std::uint32_t value = 0;
  std::memcpy(&value, pnam->data.data(), sizeof(value));
  const RawFormId raw{value};
  if (raw.is_null()) return {InfoPnamState::explicit_null, {}};

  InfoPnam result;
  result.state = InfoPnamState::linked;
  result.previous = resolve_plugin_form_id(raw, source_context);
  const auto previous = locate_record(source, result.previous);
  if (previous.record != nullptr) {
    if (previous.record->header.signature != k_info || previous.path.empty() ||
        previous.path.back() != source_topic) {
      throw InfoCopyError("INFO PNAM predecessor is not in the source Topic Children group");
    }
  } else if (result.previous.owner() == source.key) {
    throw InfoCopyError("INFO PNAM local predecessor is missing from the source plugin");
  }
  return result;
}

void validate_destination_quest_dial(
    const Plugin& destination,
    const InfoContainment& source_containment,
    const PluginFormIdContext& source_context,
    const PluginFormIdContext& destination_context) {
  validate_destination_record_path(
      destination, source_containment.quest_key, k_qust,
      source_containment.quest.path, source_context, destination_context, "QUST");
  validate_destination_record_path(
      destination, source_containment.dial_key, k_dial,
      source_containment.dial.path, source_context, destination_context, "DIAL");
}

void insert_info(Group& topic,
                 Record copied,
                 bool at_head,
                 std::optional<FormKey> after) {
  if (after.has_value()) {
    const auto index = direct_record_index(topic, *after);
    if (!index.has_value()) {
      throw InfoCopyError("INFO insertion predecessor disappeared");
    }
    topic.children.insert(
        topic.children.begin() + static_cast<std::ptrdiff_t>(*index + 1),
        std::move(copied));
    return;
  }
  if (at_head) {
    topic.children.insert(topic.children.begin(), std::move(copied));
    return;
  }
  topic.children.emplace_back(std::move(copied));
}

}  // namespace

PlacedReferenceCopyStatus copy_placed_reference_as_override(
    Plugin& destination,
    const Plugin& source,
    const FormKey& reference,
    bool overwrite) {
  const auto located = locate_record(source, reference);
  if (located.record == nullptr) {
    return PlacedReferenceCopyStatus::source_missing;
  }
  if (!is_placed_reference_signature(located.record->header.signature)) {
    throw PlacedReferenceError("placed-reference override requires a placed-reference source record");
  }

  const auto source_context = plugin_form_id_context(source);
  const auto containment = resolve_placed_containment(source, located, source_context);
  const auto& source_reference = containment.reference;
  const auto cell_children_index = containment.cell_children_index;
  const auto& cell = containment.cell;
  const auto& source_cell = containment.source_cell;
  const auto& world = containment.world;
  const auto& source_world = containment.source_world;

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
      source_reference.path[cell_children_index]->header,
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

PlacedReferenceCopyNewResult copy_placed_reference_as_new(
    Plugin& destination,
    const Plugin& source,
    const FormKey& reference) {
  const auto located = locate_record(source, reference);
  if (located.record == nullptr) {
    return {PlacedReferenceCopyNewStatus::source_missing, {}};
  }
  const auto signature = located.record->header.signature;
  if (!is_placed_reference_signature(signature)) {
    return {PlacedReferenceCopyNewStatus::unsupported_signature, {}};
  }

  const auto source_context = plugin_form_id_context(source);
  const auto containment = resolve_placed_containment(source, located, source_context);
  const auto& cell = containment.cell;
  const auto& source_cell = containment.source_cell;
  const auto& world = containment.world;
  const auto& source_world = containment.source_world;

  auto next = destination;
  assign_record_form_keys(next);
  if (find_record(next, cell) == nullptr) {
    return {PlacedReferenceCopyNewStatus::parent_cell_missing, {}};
  }
  if (world.has_value() && find_record(next, *world) == nullptr) {
    return {PlacedReferenceCopyNewStatus::parent_cell_missing, {}};
  }

  std::set<PluginKey> required_owners;
  if (!cell.owner().empty()) required_owners.insert(cell.owner());
  if (world.has_value() && !world->owner().empty()) required_owners.insert(world->owner());
  for (const auto* group : containment.reference.path) {
    collect_group_label_owner(*group, source_context, required_owners);
  }
  const auto links = collect_form_links_detailed(source);
  for (const auto& link : links.links) {
    if (link.record != reference || link.key.owner().empty()) continue;
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
  }
  validate_destination_record_path(
      next, cell, k_cell, source_cell.path,
      source_context, destination_context, "CELL");
  auto* cell_parent = ensure_group_path(
      next, source_cell.path, source_context, destination_context);

  auto* destination_cell_children = ensure_cell_children_group(
      *cell_parent,
      containment.reference.path[containment.cell_children_index]->header,
      cell, source_context, destination_context);
  auto* bucket = ensure_reference_bucket(
      *destination_cell_children,
      containment.reference.path.back()->header,
      source_context, destination_context);

  const auto new_key = reserve_local_form_key(next);
  auto copied = copy_record_for_destination(
      *containment.reference.record, new_key, source_context, destination_context);
  copied.form_key = new_key;
  bucket->children.emplace_back(std::move(copied));

  next.original_range.clear();
  destination = std::move(next);
  return {PlacedReferenceCopyNewStatus::created, new_key};
}

InfoCopyNewResult copy_info_as_new(
    Plugin& destination,
    const Plugin& source,
    const FormKey& info) {
  const auto located = locate_record(source, info);
  if (located.record == nullptr) {
    return {InfoCopyNewStatus::source_missing, {}};
  }
  if (located.record->header.signature != k_info) {
    return {InfoCopyNewStatus::unsupported_signature, {}};
  }

  const auto source_context = plugin_form_id_context(source);
  const auto containment = resolve_info_containment(source, located, source_context);
  const auto* source_topic = containment.info.path.back();
  const auto& source_info_record = *containment.info.record;
  const auto pnam = read_info_pnam(
      source, source_info_record, source_context, source_topic);

  auto next = destination;
  assign_record_form_keys(next);

  std::set<PluginKey> required_owners;
  if (!containment.quest_key.owner().empty()) {
    required_owners.insert(containment.quest_key.owner());
  }
  if (!containment.dial_key.owner().empty()) {
    required_owners.insert(containment.dial_key.owner());
  }
  if (pnam.state == InfoPnamState::linked && !pnam.previous.owner().empty()) {
    required_owners.insert(pnam.previous.owner());
  }
  for (const auto* group : containment.info.path) {
    collect_group_label_owner(*group, source_context, required_owners);
  }
  const auto links = collect_form_links_detailed(source);
  for (const auto& link : links.links) {
    if (link.record != info || link.key.owner().empty()) continue;
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
  const auto destination_quest = locate_record(next, containment.quest_key);
  const auto destination_dial = locate_record(next, containment.dial_key);
  if (destination_quest.record == nullptr || destination_dial.record == nullptr) {
    return {InfoCopyNewStatus::parent_dial_missing, {}};
  }
  validate_destination_quest_dial(
      next, containment, source_context, destination_context);
  const auto& destination_dial_record = *destination_dial.record;
  const auto destination_qnam = read_info_form_link(
      destination_dial_record, k_qnam, destination_context);
  if (destination_qnam.has_value() && *destination_qnam != containment.quest_key) {
    throw InfoCopyError("destination DIAL QNAM does not match its Quest Children parent");
  }

  auto* dial_group = existing_group_path(
      next, containment.dial.path, source_context, destination_context);
  auto* topic = ensure_topic_children_group(
      dial_group[0], containment.info.path.back()->header, containment.dial_key,
      source_context, destination_context);

  bool at_head = false;
  std::optional<FormKey> after;
  const auto destination_source_info = locate_record(next, info);
  if (destination_source_info.record != nullptr) {
    if (destination_source_info.record->header.signature != k_info ||
        destination_source_info.path.back() != topic ||
        !direct_record_index(topic[0], info).has_value()) {
      throw InfoCopyError("destination source INFO is not in its Topic Children group");
    }
    after = info;
  } else if (pnam.state == InfoPnamState::explicit_null) {
    at_head = true;
  } else if (pnam.state == InfoPnamState::linked) {
    const auto destination_previous = locate_record(next, pnam.previous);
    if (destination_previous.record != nullptr) {
      if (destination_previous.record->header.signature != k_info ||
          destination_previous.path.empty() ||
          destination_previous.path.back() != topic ||
          !direct_record_index(topic[0], pnam.previous).has_value()) {
        throw InfoCopyError("destination INFO PNAM predecessor is not in its Topic Children group");
      }
      after = pnam.previous;
    } else if (pnam.previous.owner() == next.key) {
      throw InfoCopyError("destination local INFO PNAM predecessor is missing");
    } else {
      encode_plugin_form_id(pnam.previous, destination_context);
      at_head = true;
    }
  }

  const auto new_key = reserve_local_form_key(next);
  auto copied = copy_record_for_destination(
      source_info_record, new_key, source_context, destination_context);
  copied.form_key = new_key;
  insert_info(topic[0], std::move(copied), at_head, after);

  next.original_range.clear();
  destination = std::move(next);
  return {InfoCopyNewStatus::created, new_key};
}

}  // namespace fo4::forms
