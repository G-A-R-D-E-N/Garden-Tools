#include "fo4/forms/placed_reference.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fo4::forms {

using namespace detail;

namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_dial = Signature::from_string("DIAL");
constexpr auto k_info = Signature::from_string("INFO");
constexpr auto k_qnam = Signature::from_string("QNAM");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr std::int32_t k_quest_children_group = 10;

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
  if (info.path.size() != 3) {
    throw InfoCopyError("INFO containment must use the complete QUST/DIAL ancestry");
  }
  const auto* quest_top = info.path[0];
  const auto* quest_children = info.path[1];
  const auto* topic_children = info.path[2];
  if (quest_top->header.group_type != static_cast<std::int32_t>(GroupType::top) ||
      quest_top->header.top_label() != k_qust ||
      quest_children->header.group_type != k_quest_children_group ||
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
      out.dial.path.size() != 2 ||
      !path_matches(out.dial.path, {quest_top, quest_children})) {
    throw InfoCopyError("INFO Topic Children label does not resolve to its parent DIAL");
  }
  if (!path_matches(out.info.path, {quest_top, quest_children, topic_children})) {
    throw InfoCopyError("INFO and DIAL do not share the approved containment prefix");
  }
  std::size_t topic_group_count = 0;
  for (const auto& child : quest_children->children) {
    const auto* group = std::get_if<Group>(&child);
    if (group == nullptr || !same_group_identity(group->header, topic_children->header)) {
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
    Group& quest_children,
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
  for (std::size_t index = 0; index < quest_children.children.size(); ++index) {
    auto* group = std::get_if<Group>(&quest_children.children[index]);
    if (group == nullptr || !same_group_identity(group->header, wanted)) continue;
    if (found != nullptr) {
      throw InfoCopyError("DIAL contains duplicate Topic Children groups");
    }
    found = group;
    found_index = index;
  }

  const auto dial_index = direct_record_index(quest_children, dial_key);
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
  auto it = quest_children.children.begin() +
            static_cast<std::ptrdiff_t>(*dial_index + 1);
  return &std::get<Group>(*quest_children.children.insert(it, std::move(created)));
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
  require_decodable(*containment.info.record);
  require_decodable(*containment.dial.record);
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
  require_decodable(destination_dial_record);
  const auto destination_qnam = read_info_form_link(
      destination_dial_record, k_qnam, destination_context);
  if (destination_qnam.has_value() && *destination_qnam != containment.quest_key) {
    throw InfoCopyError("destination DIAL QNAM does not match its Quest Children parent");
  }

  auto* quest_children = existing_group_path(
      next, containment.dial.path, source_context, destination_context);
  auto* topic = ensure_topic_children_group(
      quest_children[0], containment.info.path.back()->header, containment.dial_key,
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
  next.original_structure_fingerprint = 0;
  destination = std::move(next);
  return {InfoCopyNewStatus::created, new_key};
}

}  // namespace fo4::forms
