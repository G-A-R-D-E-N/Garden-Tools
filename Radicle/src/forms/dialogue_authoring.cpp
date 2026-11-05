#include "fo4/forms/dialogue.hpp"

#include "../plugin/record_edit.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "placed_reference_copy_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {

namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_dlbr = Signature::from_string("DLBR");
constexpr auto k_dial = Signature::from_string("DIAL");
constexpr auto k_info = Signature::from_string("INFO");
constexpr auto k_scen = Signature::from_string("SCEN");
constexpr auto k_edid = Signature::from_string("EDID");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_bnam = Signature::from_string("BNAM");
constexpr auto k_qnam = Signature::from_string("QNAM");
constexpr auto k_knam = Signature::from_string("KNAM");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_snam = Signature::from_string("SNAM");
constexpr auto k_tifc = Signature::from_string("TIFC");
constexpr auto k_tnam = Signature::from_string("TNAM");
constexpr auto k_dnam = Signature::from_string("DNAM");
constexpr auto k_enam = Signature::from_string("ENAM");
constexpr auto k_trda = Signature::from_string("TRDA");
constexpr auto k_nam0 = Signature::from_string("NAM0");
constexpr auto k_nam1 = Signature::from_string("NAM1");
constexpr auto k_nam2 = Signature::from_string("NAM2");
constexpr auto k_nam3 = Signature::from_string("NAM3");
constexpr auto k_nam4 = Signature::from_string("NAM4");
constexpr auto k_inam = Signature::from_string("INAM");

constexpr std::uint32_t k_none_link = 0xFFFFFFFFu;

using Located = std::optional<fo4::detail::MutableLocation>;

template <typename Value>
void append_scalar(std::vector<std::byte>& out, Value value) {
  const auto at = out.size();
  out.resize(at + sizeof(Value));
  std::memcpy(out.data() + at, &value, sizeof(Value));
}

template <typename... Values>
std::vector<std::byte> packed(Values... values) {
  std::vector<std::byte> out;
  ((append_scalar(out, values)), ...);
  return out;
}

std::vector<std::byte> raw_bytes(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::vector<std::byte> zstring_bytes(std::string_view text) {
  auto out = raw_bytes(text);
  out.push_back(std::byte{0});
  return out;
}

bool contains_nul(std::string_view text) {
  return text.find('\0') != std::string_view::npos;
}

std::uint32_t raw_form_id(const FormKey& key, const Plugin& plugin) {
  return encode_plugin_form_id(key, plugin_form_id_context(plugin)).value();
}

Record make_record(const Plugin& plugin,
                   const FormKey& key,
                   Signature signature,
                   std::vector<Subrecord> subrecords) {
  Record record;
  record.header.signature = signature;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(plugin));
  record.header.form_version = plugin.header.form_version;
  record.form_key = key;
  record.subrecords = std::move(subrecords);
  return record;
}

bool is_record(const GroupChild& child, Signature signature) {
  const auto* record = std::get_if<Record>(&child);
  return record != nullptr && record->header.signature == signature;
}

std::size_t index_of_first_record(const Group& group, Signature signature) {
  std::size_t index = 0;
  while (index < group.children.size() && !is_record(group.children[index], signature)) ++index;
  return index;
}

std::size_t end_of_leading_records(const Group& group, Signature signature) {
  std::size_t index = 0;
  while (index < group.children.size() && is_record(group.children[index], signature)) ++index;
  return index;
}

void insert_record(Group& group, std::size_t position, Record record) {
  group.children.insert(group.children.begin() + static_cast<std::ptrdiff_t>(position),
                        GroupChild{std::move(record)});
}

Subrecord* unique_subrecord(Record& record, Signature signature) {
  Subrecord* found = nullptr;
  for (auto& subrecord : record.subrecords) {
    if (subrecord.signature != signature) continue;
    if (found != nullptr) {
      throw std::runtime_error("duplicate " + signature.to_string() + " subrecords");
    }
    found = &subrecord;
  }
  return found;
}

void set_required(Record& record, Signature signature, std::vector<std::byte> data) {
  auto* subrecord = unique_subrecord(record, signature);
  if (subrecord == nullptr) {
    throw std::runtime_error(record.header.signature.to_string() + " record is missing " +
                             signature.to_string());
  }
  subrecord->data = std::move(data);
}

void set_or_append(Record& record, Signature signature, std::vector<std::byte> data) {
  if (auto* subrecord = unique_subrecord(record, signature); subrecord != nullptr) {
    subrecord->data = std::move(data);
    return;
  }
  record.subrecords.push_back(Subrecord{signature, std::move(data), false});
}

Group& ensure_adjacent_group(Group& parent,
                             std::size_t record_index,
                             const GroupHeader& wanted) {
  std::optional<std::size_t> found;
  for (std::size_t index = 0; index < parent.children.size(); ++index) {
    const auto* group = std::get_if<Group>(&parent.children[index]);
    if (group == nullptr || !detail::same_group_identity(group->header, wanted)) continue;
    if (found.has_value()) {
      throw std::runtime_error("duplicate dialogue child group");
    }
    found = index;
  }
  if (found.has_value()) {
    if (*found != record_index + 1) {
      throw std::runtime_error("dialogue child group is not adjacent to its record");
    }
    return std::get<Group>(parent.children[*found]);
  }
  Group created;
  created.header = wanted;
  const auto it = parent.children.insert(
      parent.children.begin() + static_cast<std::ptrdiff_t>(record_index + 1),
      GroupChild{std::move(created)});
  return std::get<Group>(*it);
}

std::optional<DialogueAuthorStatus> refusal(const Located& location,
                                            Signature signature,
                                            DialogueAuthorStatus missing) {
  if (!location.has_value() || location->record().header.signature != signature) {
    return missing;
  }
  if (location->record().undecodable) return DialogueAuthorStatus::undecodable;
  return std::nullopt;
}

const Subrecord* quest_link(const Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_qnam) return &subrecord;
  }
  return nullptr;
}

void commit(Plugin& plugin, Plugin& next) {
  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
}

DialogueCreateResult branch_impl(Plugin& plugin,
                                 const FormKey& quest,
                                 const DialogueBranchSpec& spec) {
  if (quest.owner() != plugin.key) return {DialogueAuthorStatus::not_local, {}};
  if (contains_nul(spec.editor_id)) return {DialogueAuthorStatus::invalid_argument, {}};
  auto next = plugin;
  assign_record_form_keys(next);
  auto quest_location = fo4::detail::find_mutable_location(next, quest);
  if (auto refused = refusal(quest_location, k_qust, DialogueAuthorStatus::quest_missing)) {
    return {*refused, {}};
  }
  if (quest_location->path.size() != 1) {
    throw std::runtime_error("QUST is not a top-level group member");
  }

  const auto key = reserve_local_form_key(next);
  const auto raw_quest = raw_form_id(quest, next);
  auto& quest_children = ensure_adjacent_group(
      *quest_location->path.front(), quest_location->index,
      detail::group_header(GroupType::quest_children, raw_quest));

  std::vector<Subrecord> subrecords;
  if (!spec.editor_id.empty()) {
    subrecords.push_back(Subrecord{k_edid, zstring_bytes(stored_editor_id(spec.editor_id)), false});
  }
  subrecords.push_back(Subrecord{k_qnam, packed(raw_quest), false});
  subrecords.push_back(Subrecord{k_tnam, packed(spec.category), false});
  subrecords.push_back(Subrecord{k_dnam, packed(spec.flags), false});
  subrecords.push_back(Subrecord{k_snam, packed(std::uint32_t{0}), false});

  insert_record(quest_children, end_of_leading_records(quest_children, k_dlbr),
                make_record(next, key, k_dlbr, std::move(subrecords)));
  commit(plugin, next);
  return {DialogueAuthorStatus::ok, key};
}

DialogueCreateResult topic_impl(Plugin& plugin,
                                const FormKey& quest,
                                const DialogueTopicSpec& spec) {
  if (quest.owner() != plugin.key ||
      (spec.branch.has_value() && spec.branch->owner() != plugin.key)) {
    return {DialogueAuthorStatus::not_local, {}};
  }
  if (contains_nul(spec.editor_id) || contains_nul(spec.name) ||
      contains_nul(spec.subtype_name)) {
    return {DialogueAuthorStatus::invalid_argument, {}};
  }
  if (!spec.subtype_name.empty() && spec.subtype_name.size() != 4) {
    return {DialogueAuthorStatus::invalid_argument, {}};
  }
  if (!spec.name.empty() && plugin.header.tes4_flags.is_localized()) {
    return {DialogueAuthorStatus::localized_unsupported, {}};
  }
  auto next = plugin;
  assign_record_form_keys(next);
  auto quest_location = fo4::detail::find_mutable_location(next, quest);
  if (auto refused = refusal(quest_location, k_qust, DialogueAuthorStatus::quest_missing)) {
    return {*refused, {}};
  }
  if (quest_location->path.size() != 1) {
    throw std::runtime_error("QUST is not a top-level group member");
  }
  if (spec.branch.has_value()) {
    auto branch_location = fo4::detail::find_mutable_location(next, *spec.branch);
    if (auto refused = refusal(branch_location, k_dlbr, DialogueAuthorStatus::branch_missing)) {
      return {*refused, {}};
    }
    const auto* branch_quest = quest_link(branch_location->record());
    if (branch_quest == nullptr || branch_quest->data != packed(raw_form_id(quest, next))) {
      return {DialogueAuthorStatus::structure_invalid, {}};
    }
  }

  const auto key = reserve_local_form_key(next);
  const auto raw_quest = raw_form_id(quest, next);
  auto& quest_children = ensure_adjacent_group(
      *quest_location->path.front(), quest_location->index,
      detail::group_header(GroupType::quest_children, raw_quest));

  std::vector<Subrecord> subrecords;
  if (!spec.editor_id.empty()) {
    subrecords.push_back(Subrecord{k_edid, zstring_bytes(stored_editor_id(spec.editor_id)), false});
  }
  if (!spec.name.empty()) {
    subrecords.push_back(Subrecord{
        k_full, zstring_bytes(stored_inline_text(spec.name, "topic name")), false});
  }
  subrecords.push_back(Subrecord{k_pnam, packed(spec.priority), false});
  if (spec.branch.has_value()) {
    subrecords.push_back(Subrecord{k_bnam, packed(raw_form_id(*spec.branch, next)), false});
  }
  subrecords.push_back(Subrecord{k_qnam, packed(raw_quest), false});
  if (spec.keyword.has_value()) {
    subrecords.push_back(Subrecord{k_knam, packed(raw_form_id(*spec.keyword, next)), false});
  }
  subrecords.push_back(Subrecord{
      k_data, packed(spec.topic_flags, spec.category, spec.subtype), false});
  if (!spec.subtype_name.empty()) {
    subrecords.push_back(Subrecord{k_snam, raw_bytes(spec.subtype_name), false});
  }
  subrecords.push_back(Subrecord{k_tifc, packed(std::uint32_t{0}), false});

  insert_record(quest_children, index_of_first_record(quest_children, k_scen),
                make_record(next, key, k_dial, std::move(subrecords)));
  commit(plugin, next);
  return {DialogueAuthorStatus::ok, key};
}

DialogueAuthorStatus start_impl(Plugin& plugin, const FormKey& branch, const FormKey& topic) {
  if (branch.owner() != plugin.key || topic.owner() != plugin.key) {
    return DialogueAuthorStatus::not_local;
  }
  auto next = plugin;
  assign_record_form_keys(next);
  auto branch_location = fo4::detail::find_mutable_location(next, branch);
  if (auto refused = refusal(branch_location, k_dlbr, DialogueAuthorStatus::branch_missing)) {
    return *refused;
  }
  auto topic_location = fo4::detail::find_mutable_location(next, topic);
  if (auto refused = refusal(topic_location, k_dial, DialogueAuthorStatus::topic_missing)) {
    return *refused;
  }
  const auto* branch_quest = quest_link(branch_location->record());
  const auto* topic_quest = quest_link(topic_location->record());
  if (branch_quest == nullptr || topic_quest == nullptr || branch_quest->data != topic_quest->data) {
    return DialogueAuthorStatus::structure_invalid;
  }

  auto& record = branch_location->record();
  set_required(record, k_snam, packed(raw_form_id(topic, next)));
  fo4::detail::mark_record_modified(next, record);
  commit(plugin, next);
  return DialogueAuthorStatus::ok;
}

std::vector<Subrecord> response_subrecords(const Plugin& plugin,
                                           const DialogueResponseSpec& response,
                                           bool localized) {
  const auto emotion = response.emotion.has_value() ? raw_form_id(*response.emotion, plugin)
                                                    : k_none_link;
  const auto sound = response.sound.has_value() ? raw_form_id(*response.sound, plugin)
                                                : std::uint32_t{0};
  std::vector<Subrecord> out;
  out.push_back(Subrecord{k_trda, packed(emotion, response.number, sound, std::uint8_t{0},
                                         response.interrupt_percent,
                                         response.camera_target_alias,
                                         response.camera_location_alias), false});
  out.push_back(Subrecord{k_nam1, localized ? packed(std::uint32_t{0})
                                            : zstring_bytes(stored_inline_text(
                                                  response.text, "response text")), false});
  out.push_back(Subrecord{k_nam2, zstring_bytes(stored_inline_text(
                                      response.script_notes, "script notes")), false});
  out.push_back(Subrecord{k_nam3, zstring_bytes(""), false});
  out.push_back(Subrecord{k_nam4, zstring_bytes(""), false});
  return out;
}

DialogueCreateResult info_impl(Plugin& plugin,
                               const FormKey& topic,
                               const DialogueInfoSpec& spec) {
  if (topic.owner() != plugin.key) return {DialogueAuthorStatus::not_local, {}};
  for (const auto& response : spec.responses) {
    if (contains_nul(response.text) || contains_nul(response.script_notes)) {
      return {DialogueAuthorStatus::invalid_argument, {}};
    }
  }
  const bool localized = plugin.header.tes4_flags.is_localized();
  if (localized) {
    for (const auto& response : spec.responses) {
      if (!response.text.empty()) return {DialogueAuthorStatus::localized_unsupported, {}};
    }
  }
  auto next = plugin;
  assign_record_form_keys(next);
  auto topic_location = fo4::detail::find_mutable_location(next, topic);
  if (auto refused = refusal(topic_location, k_dial, DialogueAuthorStatus::topic_missing)) {
    return {*refused, {}};
  }
  if (topic_location->path.size() != 2) {
    throw std::runtime_error("DIAL is not in a Quest Children group");
  }

  const auto key = reserve_local_form_key(next);
  const auto raw_topic = raw_form_id(topic, next);
  auto& quest_children = *topic_location->path[1];
  const auto dial_index = topic_location->index;
  auto& topic_children = ensure_adjacent_group(
      quest_children, dial_index, detail::group_header(GroupType::topic_children, raw_topic));

  std::uint32_t previous = 0;
  if (!topic_children.children.empty()) {
    const auto* last = std::get_if<Record>(&topic_children.children.back());
    if (last == nullptr) {
      throw std::runtime_error("topic child group contains a non-INFO child");
    }
    previous = raw_form_id(last->form_key, next);
  }

  std::vector<Subrecord> subrecords;
  subrecords.push_back(Subrecord{k_enam, packed(spec.flags, spec.reset_hours), false});
  subrecords.push_back(Subrecord{k_pnam, packed(previous), false});
  for (const auto& response : spec.responses) {
    auto parts = response_subrecords(next, response, localized);
    for (auto& part : parts) subrecords.push_back(std::move(part));
  }
  auto conditions = serialize_ctda_run(spec.conditions);
  for (auto& condition : conditions) subrecords.push_back(std::move(condition));
  subrecords.push_back(Subrecord{k_nam0, zstring_bytes(""), false});
  subrecords.push_back(Subrecord{k_inam, packed(std::uint32_t{1}), false});

  topic_children.children.emplace_back(make_record(next, key, k_info, std::move(subrecords)));
  auto& dial = std::get<Record>(quest_children.children[dial_index]);
  set_or_append(dial, k_tifc, packed(static_cast<std::uint32_t>(topic_children.children.size())));
  fo4::detail::mark_record_modified(next, dial);
  commit(plugin, next);
  return {DialogueAuthorStatus::ok, key};
}

DialogueAuthorStatus remove_impl(Plugin& plugin, const FormKey& info) {
  if (info.owner() != plugin.key) return DialogueAuthorStatus::not_local;
  auto next = plugin;
  assign_record_form_keys(next);
  auto info_location = fo4::detail::find_mutable_location(next, info);
  if (auto refused = refusal(info_location, k_info, DialogueAuthorStatus::info_missing)) {
    return *refused;
  }
  if (info_location->path.size() != 3 ||
      info_location->path[2]->header.group_type !=
          static_cast<std::int32_t>(GroupType::topic_children)) {
    throw std::runtime_error("INFO is not in a Topic Children group");
  }

  auto& quest_children = *info_location->path[1];
  auto& topic_children = *info_location->path[2];
  const auto index = info_location->index;
  auto& removed = std::get<Record>(topic_children.children[index]);
  auto previous = packed(std::uint32_t{0});
  if (const auto* pnam = unique_subrecord(removed, k_pnam); pnam != nullptr) {
    if (pnam->data.size() != 4) throw std::runtime_error("INFO PNAM must be exactly 4 bytes");
    previous = pnam->data;
  }
  if (index + 1 < topic_children.children.size()) {
    if (auto* following = std::get_if<Record>(&topic_children.children[index + 1])) {
      if (following->header.signature != k_info) {
        throw std::runtime_error("topic child group contains a non-INFO child");
      }
      if (following->undecodable) return DialogueAuthorStatus::undecodable;
      set_required(*following, k_pnam, previous);
      fo4::detail::mark_record_modified(next, *following);
    }
  }
  topic_children.children.erase(topic_children.children.begin() +
                                static_cast<std::ptrdiff_t>(index));

  std::size_t topic_index = 0;
  while (topic_index < quest_children.children.size() &&
         std::get_if<Group>(&quest_children.children[topic_index]) != &topic_children) {
    ++topic_index;
  }
  if (topic_index == 0 || topic_index >= quest_children.children.size()) {
    throw std::runtime_error("INFO topic group is not adjacent to a DIAL record");
  }
  auto* dial = std::get_if<Record>(&quest_children.children[topic_index - 1]);
  if (dial == nullptr || dial->header.signature != k_dial ||
      raw_form_id(dial->form_key, next) != topic_children.header.label_as_u32()) {
    throw std::runtime_error("INFO topic group does not resolve to its parent DIAL");
  }
  if (dial->undecodable) return DialogueAuthorStatus::undecodable;
  set_or_append(*dial, k_tifc, packed(static_cast<std::uint32_t>(topic_children.children.size())));
  fo4::detail::mark_record_modified(next, *dial);

  if (topic_children.children.empty()) {
    quest_children.children.erase(quest_children.children.begin() +
                                  static_cast<std::ptrdiff_t>(topic_index));
  }
  commit(plugin, next);
  return DialogueAuthorStatus::ok;
}

}

DialogueCreateResult create_dialogue_branch(Plugin& plugin,
                                            const FormKey& quest,
                                            const DialogueBranchSpec& spec) {
  try {
    return branch_impl(plugin, quest, spec);
  } catch (...) {
    return {DialogueAuthorStatus::structure_invalid, {}};
  }
}

DialogueCreateResult create_dialogue_topic(Plugin& plugin,
                                           const FormKey& quest,
                                           const DialogueTopicSpec& spec) {
  try {
    return topic_impl(plugin, quest, spec);
  } catch (...) {
    return {DialogueAuthorStatus::structure_invalid, {}};
  }
}

DialogueAuthorStatus set_dialogue_branch_start(Plugin& plugin,
                                               const FormKey& branch,
                                               const FormKey& topic) {
  try {
    return start_impl(plugin, branch, topic);
  } catch (...) {
    return DialogueAuthorStatus::structure_invalid;
  }
}

DialogueCreateResult create_dialogue_info(Plugin& plugin,
                                          const FormKey& topic,
                                          const DialogueInfoSpec& spec) {
  try {
    return info_impl(plugin, topic, spec);
  } catch (...) {
    return {DialogueAuthorStatus::structure_invalid, {}};
  }
}

DialogueAuthorStatus remove_dialogue_info(Plugin& plugin, const FormKey& info) {
  try {
    return remove_impl(plugin, info);
  } catch (...) {
    return DialogueAuthorStatus::structure_invalid;
  }
}

}
