#include "check_dialogue.hpp"
#include "check_errors_internal.hpp"
#include "check_errors_records.hpp"
#include "fo4/graph/check_errors.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace fo4 {
namespace {

constexpr Signature k_sig_dial = Signature::from_string("DIAL");
constexpr Signature k_sig_dlbr = Signature::from_string("DLBR");
constexpr Signature k_sig_qnam = Signature::from_string("QNAM");
constexpr Signature k_sig_bnam = Signature::from_string("BNAM");
constexpr std::int32_t k_group_topic_children = static_cast<std::int32_t>(GroupType::topic_children);
constexpr std::int32_t k_group_quest_children = static_cast<std::int32_t>(GroupType::quest_children);
constexpr std::uint32_t k_first_non_hardcoded_id = 0x800;
constexpr std::string_view k_game_master_name = "Fallout4.esm";

constexpr std::array<Signature, 15> k_scope_link_subrecords{
    Signature::from_string("BNAM"), Signature::from_string("QNAM"), Signature::from_string("SNAM"),
    Signature::from_string("PNAM"), Signature::from_string("TPIC"), Signature::from_string("DATA"),
    Signature::from_string("PTOP"), Signature::from_string("NTOP"), Signature::from_string("NETO"),
    Signature::from_string("QTOP"), Signature::from_string("NPOT"), Signature::from_string("NNGT"),
    Signature::from_string("NNUT"), Signature::from_string("NQUT"),
    Signature::from_string("STSC")};

struct FormKeyHash {
  std::size_t operator()(const FormKey& key) const noexcept {
    return hash_value(key.owner()) ^
           (static_cast<std::size_t>(key.local().value()) * 0x9E3779B97F4A7C15ull);
  }
};

using FormKeySet = std::unordered_set<FormKey, FormKeyHash>;

std::optional<FormKey> resolve_label(RawFormId raw, const PluginFormIdContext& context) {
  if (raw.is_null() || raw.is_created()) return std::nullopt;
  try {
    return resolve_plugin_link(raw, context);
  } catch (const FormIdError&) {
    return std::nullopt;
  }
}

RawFormId read_raw(const Subrecord& sub) {
  std::uint32_t value = 0;
  std::memcpy(&value, sub.data.data(), sizeof(value));
  return RawFormId{value};
}

struct Located {
  const Plugin* plugin{nullptr};
  const Record* record{nullptr};
  std::size_t plugin_index{0};
  std::optional<FormKey> topic;
};

using LocatedMap = std::unordered_map<FormKey, Located, FormKeyHash>;

void locate_record(const Plugin& plugin, std::size_t index, const Record& record,
                   const std::optional<FormKey>& group_topic, const FormKeySet& wanted,
                   const FormKeySet& labels, LocatedMap& found) {
  const auto& key = record.form_key;
  if (key.owner().filename().empty()) return;
  std::optional<FormKey> topic;
  if (record.header.signature == sig_info) topic = group_topic;
  const bool label_hit = topic && labels.contains(*topic);
  auto entry = found.find(key);
  if (entry == found.end()) {
    if (!label_hit && !wanted.contains(key)) return;
    entry = found.emplace(key, Located{}).first;
  }
  if (entry->second.record != nullptr && entry->second.plugin_index == index) return;
  entry->second = Located{&plugin, &record, index, topic};
}

void locate_group(const Plugin& plugin, std::size_t index, const PluginFormIdContext& context,
                  const Group& group, const FormKeySet& wanted, const FormKeySet& labels,
                  LocatedMap& found) {
  std::optional<FormKey> topic;
  if (group.header.group_type == k_group_topic_children) {
    topic = resolve_label(RawFormId{group.header.label_as_u32()}, context);
  }
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      locate_record(plugin, index, *record, topic, wanted, labels, found);
    } else {
      locate_group(plugin, index, context, std::get<Group>(child), wanted, labels, found);
    }
  }
}

LocatedMap locate(std::span<const Plugin* const> load_order, const FormKeySet& wanted,
                  const FormKeySet& labels) {
  LocatedMap found;
  for (std::size_t index = 0; index < load_order.size(); ++index) {
    const auto* plugin = load_order[index];
    if (plugin == nullptr) continue;
    const auto context = plugin_form_id_context(*plugin);
    for (const auto& group : plugin->groups) {
      locate_group(*plugin, index, context, group, wanted, labels, found);
    }
  }
  return found;
}

struct TopicInfo {
  const Record* record{nullptr};
  FormKey topic;
};

struct CheckedQuestScope {
  const Record* quest{nullptr};
  std::vector<const Record*> records;
  FormKeySet keys;
  FormKeySet dials;
  DialogueTopics topics;
};

void add_scoped(CheckedQuestScope& scope, const Record& record) {
  if (scope.keys.insert(record.form_key).second) scope.records.push_back(&record);
}

void scope_record(const Record& record, bool scoped, const std::optional<FormKey>& topic,
                  const PluginFormIdContext& context, const FormKey& quest,
                  CheckedQuestScope& scope, std::vector<TopicInfo>& infos) {
  const auto signature = record.header.signature;
  if (signature == sig_qust) {
    if (record.form_key == quest && scope.quest == nullptr) scope.quest = &record;
    return;
  }
  if (signature == k_sig_dial || signature == k_sig_dlbr) {
    if (!scoped && first_link_target(record, k_sig_qnam, context) != quest) return;
    add_scoped(scope, record);
    if (signature == k_sig_dial) scope.dials.insert(record.form_key);
    return;
  }
  if (signature == sig_scen) {
    if (scoped || scene_parent_is(record, quest, context)) add_scoped(scope, record);
    return;
  }
  if (signature == sig_info && topic) infos.push_back(TopicInfo{&record, *topic});
}

void scope_group(const Group& group, const PluginFormIdContext& context, const FormKey& quest,
                 bool in_quest, CheckedQuestScope& scope, std::vector<TopicInfo>& infos) {
  const bool quest_group = group.header.group_type == k_group_quest_children &&
                           resolve_label(RawFormId{group.header.label_as_u32()}, context) == quest;
  const bool scoped = in_quest || quest_group;
  std::optional<FormKey> topic;
  if (group.header.group_type == k_group_topic_children) {
    topic = resolve_label(RawFormId{group.header.label_as_u32()}, context);
  }
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      scope_record(*record, scoped, topic, context, quest, scope, infos);
    } else {
      scope_group(std::get<Group>(child), context, quest, scoped, scope, infos);
    }
  }
}

CheckedQuestScope collect_scope(const Plugin& plugin, const FormKey& quest) {
  CheckedQuestScope scope;
  const auto context = plugin_form_id_context(plugin);
  std::vector<TopicInfo> infos;
  for (const auto& group : plugin.groups) scope_group(group, context, quest, false, scope, infos);
  if (scope.quest == nullptr) return scope;
  scope.keys.insert(scope.quest->form_key);
  scope.records.insert(scope.records.begin(), scope.quest);
  for (const auto& info : infos) {
    if (!scope.dials.contains(info.topic)) continue;
    add_scoped(scope, *info.record);
    scope.topics[info.record->form_key] = info.topic;
  }
  return scope;
}

void want_key(FormKeySet& wanted, const FormKey& key) {
  if (!key.owner().filename().empty()) wanted.insert(key);
}

void want_raw(FormKeySet& wanted, RawFormId raw, const PluginFormIdContext& context,
              const PluginKey& game_master) {
  if (raw.is_null() || raw.is_none()) return;
  if (raw.index_byte() > context.masters.size()) return;
  try {
    want_key(wanted, resolve_plugin_link(raw, context));
  } catch (const FormIdError&) {
    return;
  }
  if (raw.value() < k_first_non_hardcoded_id) {
    want_key(wanted, FormKey{game_master, LocalFormId{raw.local_24()}});
  }
}

FormKeySet wanted_keys(const CheckedQuestScope& scope, const PluginFormIdContext& context,
                       const FormLinkCollection& links, const PluginKey& game_master) {
  FormKeySet wanted;
  for (const auto& hit : links.links) want_raw(wanted, hit.raw, context, game_master);
  for (const auto* record : scope.records) {
    for (const auto& sub : record->subrecords) {
      if (sub.data.size() != 4) continue;
      if (std::find(k_scope_link_subrecords.begin(), k_scope_link_subrecords.end(), sub.signature) ==
          k_scope_link_subrecords.end()) {
        continue;
      }
      want_raw(wanted, read_raw(sub), context, game_master);
    }
  }
  for (const auto& dial : scope.dials) want_key(wanted, dial);
  return wanted;
}

FormKeySet branch_quest_keys(const CheckedQuestScope& scope, const PluginFormIdContext& context,
                             const LocatedMap& found) {
  FormKeySet extra;
  for (const auto* record : scope.records) {
    if (record->header.signature != k_sig_dial) continue;
    const auto branch = first_link_target(*record, k_sig_bnam, context);
    if (!branch) continue;
    const auto entry = found.find(*branch);
    if (entry == found.end() || entry->second.record->header.signature != k_sig_dlbr) continue;
    const auto quest = first_link_target(*entry->second.record, k_sig_qnam,
                                         plugin_form_id_context(*entry->second.plugin));
    if (quest && !found.contains(*quest)) extra.insert(*quest);
  }
  return extra;
}

void fill_contexts(const LocatedMap& found, WinnerMap& winners, DialogueCheckContext& dialogue) {
  for (const auto& [key, located] : found) {
    const auto& record = *located.record;
    winners[key] = WinnerInfo{record.header.signature, record.header.flags.is_deleted(),
                              located.plugin->key};
    dialogue.winners[key] = DialogueCheckContext::Winner{record.header.signature, located.plugin->key};
    if (record.header.signature == sig_info) {
      dialogue.topics[key] = located.topic;
    }
  }
  for (const auto& [key, located] : found) {
    if (located.record->header.signature != k_sig_dlbr) continue;
    const auto quest = first_link_target(*located.record, k_sig_qnam,
                                         plugin_form_id_context(*located.plugin));
    if (!quest) continue;
    const auto type = dialogue.winners.find(*quest);
    if (type != dialogue.winners.end() && type->second.signature == sig_qust) {
      dialogue.branch_quests[key] = *quest;
    }
  }
}

}  // namespace

std::vector<CheckFinding> check_quest(std::span<const Plugin* const> load_order,
                                      const QuestCheckRequest& request,
                                      CheckRunSummary* summary) {
  const Plugin* target = nullptr;
  for (const auto* plugin : load_order) {
    if (plugin != nullptr && plugin->key == request.plugin) {
      target = plugin;
      break;
    }
  }
  if (target == nullptr) return {};
  const auto scope = collect_scope(*target, request.quest);
  if (scope.quest == nullptr) return {};

  const auto game_master = PluginKey::from_filename(k_game_master_name);
  const auto target_context = plugin_form_id_context(*target);
  const auto type_lookup = plugin_type_lookup(*target);

  FormLinkCollection links;
  for (const auto* record : scope.records) {
    auto part = collect_record_form_links(*record, target_context, type_lookup);
    links.links.insert(links.links.end(), std::make_move_iterator(part.links.begin()),
                       std::make_move_iterator(part.links.end()));
    links.records_with_incomplete_vmad += part.records_with_incomplete_vmad;
    links.unsupported_vmad_records.insert(links.unsupported_vmad_records.end(),
                                          part.unsupported_vmad_records.begin(),
                                          part.unsupported_vmad_records.end());
  }

  std::map<FormKey, const Record*> records_map;
  for (const auto* record : scope.records) records_map.try_emplace(record->form_key, record);

  const auto wanted = wanted_keys(scope, target_context, links, game_master);
  auto found = locate(load_order, wanted, scope.dials);
  const auto extra = branch_quest_keys(scope, target_context, found);
  if (!extra.empty()) {
    const auto more = locate(load_order, extra, FormKeySet{});
    found.insert(more.begin(), more.end());
  }

  WinnerMap winners;
  DialogueCheckContext dialogue;
  fill_contexts(found, winners, dialogue);

  FindingSink sink{request.limit, {}, false, {}};
  check_links(*target, links, records_map, winners, game_master, sink);
  check_dialogue_links(*target, scope.records, scope.topics, winners, sink);
  for (auto& finding : check_dialogue_records(*target, scope.records, scope.topics, dialogue)) {
    if (finding.code == CheckCode::dialogue_info_count_mismatch) continue;
    sink.add(std::move(finding));
  }
  check_preflight(*target, scope.records, sink);
  for (auto& finding : check_record_levels(*target, scope.records)) sink.add(std::move(finding));
  check_conditions(*target, scope.records, sink);

  if (sink.truncated) {
    CheckFinding finding;
    finding.code = CheckCode::results_truncated;
    finding.severity = CheckSeverity::warning;
    finding.plugin = sink.truncated_plugin;
    finding.message =
        "finding limit " + std::to_string(request.limit) + " reached; results truncated";
    sink.findings.push_back(std::move(finding));
  }
  if (summary != nullptr) summary->truncated = sink.truncated;
  return std::move(sink.findings);
}

}  // namespace fo4
