#include "check_dialogue.hpp"

#include "../environment/scene_layout.hpp"
#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr Signature sig_dial = Signature::from_string("DIAL");
constexpr Signature sig_dlbr = Signature::from_string("DLBR");
constexpr Signature sig_pnam = Signature::from_string("PNAM");
constexpr Signature sig_qnam = Signature::from_string("QNAM");
constexpr Signature sig_bnam = Signature::from_string("BNAM");
constexpr Signature sig_tifc = Signature::from_string("TIFC");
constexpr Signature sig_tnam = Signature::from_string("TNAM");
constexpr Signature sig_enam = Signature::from_string("ENAM");
constexpr Signature sig_inam = Signature::from_string("INAM");
constexpr Signature sig_alid = Signature::from_string("ALID");

constexpr std::array k_action_topic_links{
    Signature::from_string("DATA"), Signature::from_string("PTOP"),
    Signature::from_string("NTOP"), Signature::from_string("NETO"),
    Signature::from_string("QTOP"), Signature::from_string("NPOT"),
    Signature::from_string("NNGT"), Signature::from_string("NNUT"),
    Signature::from_string("NQUT"), Signature::from_string("STSC")};

constexpr std::int32_t k_dialogue_action = 0;
constexpr std::int32_t k_player_dialogue_action = 3;
constexpr std::int32_t k_npc_response_action = 5;
constexpr std::int32_t k_radio_action = 6;

using Topics = std::map<FormKey, std::optional<FormKey>>;

std::optional<std::uint32_t> read_u32(const Subrecord& sub) {
  if (sub.data.size() != 4) return std::nullopt;
  std::uint32_t value = 0;
  std::memcpy(&value, sub.data.data(), sizeof(value));
  return value;
}

std::optional<std::int32_t> read_i32(const Subrecord& sub) {
  if (sub.data.size() != 4) return std::nullopt;
  std::int32_t value = 0;
  std::memcpy(&value, sub.data.data(), sizeof(value));
  return value;
}

std::optional<FormKey> resolve_label(RawFormId raw, const PluginFormIdContext& context) {
  if (raw.is_null() || raw.is_created()) return std::nullopt;
  try {
    // Requires: out-of-range labels fold to the current plugin so they still resolve.
    return resolve_plugin_link(raw, context);
  } catch (const FormIdError&) {
    return std::nullopt;
  }
}

struct StoredLink {
  bool is_null{false};
  std::optional<FormKey> key;
};

StoredLink read_stored_link(const Subrecord& sub, const PluginFormIdContext& context) {
  StoredLink out;
  const auto value = read_u32(sub);
  if (!value) return out;
  const RawFormId raw{*value};
  if (raw.is_null() || raw.is_none()) {
    out.is_null = true;
    return out;
  }
  // Requires: an index past the master table is reported by the generic link checker.
  if (raw.index_byte() > context.masters.size()) return out;
  out.key = resolve_label(raw, context);
  if (out.key && out.key->owner().filename().empty()) out.key.reset();
  return out;
}

StoredLink first_stored_link(const Record& record, Signature signature,
                             const PluginFormIdContext& context) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return read_stored_link(sub, context);
  }
  return {};
}

const Subrecord* first_subrecord_from(const Record& record, std::size_t from, Signature signature) {
  for (std::size_t at = from; at < record.subrecords.size(); ++at) {
    if (record.subrecords[at].signature == signature) return &record.subrecords[at];
  }
  return nullptr;
}

void add_topics(const Group& group, const PluginFormIdContext& context, Topics& out) {
  std::optional<FormKey> topic;
  if (group.header.group_type == static_cast<std::int32_t>(GroupType::topic_children)) {
    topic = resolve_label(RawFormId{group.header.label_as_u32()}, context);
  }
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (record->header.signature == sig_info) out[record->form_key] = topic;
    } else {
      add_topics(std::get<Group>(child), context, out);
    }
  }
}

Topics plugin_topics(const Plugin& plugin) {
  const auto context = plugin_form_id_context(plugin);
  Topics out;
  for (const auto& group : plugin.groups) add_topics(group, context, out);
  return out;
}

std::optional<FormKey> topic_of(const Topics& topics, const FormKey& key) {
  const auto found = topics.find(key);
  return found == topics.end() ? std::nullopt : found->second;
}

std::vector<DialogueCheckContext::InfoCycle> find_info_cycles(
    std::span<const Plugin* const> load_order, const DialogueCheckContext& context) {
  std::map<FormKey, std::vector<FormKey>> edges;
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    const auto id_context = plugin_form_id_context(*plugin);
    for (const auto* record : collect_first_records(*plugin)) {
      if (record->header.signature != sig_info) continue;
      const auto owner = context.winners.find(record->form_key);
      if (owner == context.winners.end() || owner->second.plugin != plugin->key) continue;
      auto& targets = edges[record->form_key];
      const auto previous = first_stored_link(*record, sig_pnam, id_context);
      if (!previous.key) continue;
      const auto type = context.winners.find(*previous.key);
      if (type == context.winners.end() || type->second.signature != sig_info) continue;
      if (topic_of(context.topics, record->form_key) !=
          topic_of(context.topics, *previous.key)) {
        continue;
      }
      targets.push_back(*previous.key);
    }
  }

  std::map<FormKey, int> state;
  std::set<std::vector<FormKey>> reported;
  std::vector<DialogueCheckContext::InfoCycle> cycles;
  for (const auto& entry : edges) {
    const auto& root = entry.first;
    if (state[root] != 0) continue;
    std::vector<FormKey> path{root};
    std::vector<std::pair<FormKey, std::size_t>> stack{{root, 0}};
    state[root] = 1;
    while (!stack.empty()) {
      auto& frame = stack.back();
      const auto node = frame.first;
      const auto& children = edges.at(node);
      if (frame.second >= children.size()) {
        state[node] = 2;
        stack.pop_back();
        path.pop_back();
        continue;
      }
      const auto child = children[frame.second++];
      if (!edges.contains(child)) continue;
      if (state[child] == 1) {
        const auto start = std::find(path.begin(), path.end(), child);
        std::vector<FormKey> cycle(start, path.end());
        cycle.push_back(child);
        auto identity = cycle;
        identity.pop_back();
        std::rotate(identity.begin(), std::min_element(identity.begin(), identity.end()),
                    identity.end());
        if (!reported.insert(identity).second) continue;
        DialogueCheckContext::InfoCycle found;
        found.record = identity.front();
        const auto owner = context.winners.find(found.record);
        if (owner != context.winners.end()) found.plugin = owner->second.plugin;
        for (const auto& key : cycle) {
          if (!found.chain.empty()) found.chain += " -> ";
          found.chain += key.to_string();
        }
        cycles.push_back(std::move(found));
      } else if (state[child] == 0) {
        state[child] = 1;
        path.push_back(child);
        stack.emplace_back(child, 0);
      }
    }
  }
  return cycles;
}

CheckFinding make_finding(const PluginKey& plugin, const FormKey& record, CheckCode code,
                          CheckSeverity severity, Signature subrecord, const FormKey& target,
                          std::string message) {
  CheckFinding finding;
  finding.code = code;
  finding.severity = severity;
  finding.plugin = plugin;
  finding.record = record;
  finding.subrecord = subrecord;
  finding.target = target;
  finding.message = std::move(message);
  return finding;
}

void check_dial(const Plugin& plugin, const Record& record,
                const DialogueCheckContext& context, const PluginFormIdContext& id_context,
                std::vector<CheckFinding>& out) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != sig_tifc) continue;
    const auto declared = read_u32(sub);
    if (!declared) continue;
    const auto found = context.info_counts.find(record.form_key);
    const auto stored = found == context.info_counts.end() ? std::size_t{0} : found->second;
    if (static_cast<std::size_t>(*declared) != stored) {
      out.push_back(make_finding(
          plugin.key, record.form_key, CheckCode::dialogue_info_count_mismatch,
          CheckSeverity::warning, sig_tifc, FormKey{},
          "TIFC info count " + std::to_string(*declared) + " does not match the " +
              std::to_string(stored) + " INFO records in the topic children group"));
    }
    break;
  }

  const auto branch = first_stored_link(record, sig_bnam, id_context);
  if (!branch.key) return;
  const auto branch_type = context.winners.find(*branch.key);
  if (branch_type == context.winners.end() || branch_type->second.signature != sig_dlbr) return;
  const auto quest = first_stored_link(record, sig_qnam, id_context);
  if (!quest.key) return;
  const auto quest_type = context.winners.find(*quest.key);
  if (quest_type == context.winners.end() || quest_type->second.signature != sig_qust) return;
  const auto branch_quest = context.branch_quests.find(*branch.key);
  if (branch_quest == context.branch_quests.end() || branch_quest->second == *quest.key) return;
  out.push_back(make_finding(
      plugin.key, record.form_key, CheckCode::dialogue_branch_quest_mismatch,
      CheckSeverity::warning, sig_bnam, *branch.key,
      "BNAM branch " + branch.key->to_string() + " names quest " +
          branch_quest->second.to_string() + " but QNAM names " + quest.key->to_string()));
}

void check_dlbr(const Plugin& plugin, const Record& record,
                std::vector<CheckFinding>& out) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != sig_tnam) continue;
    const auto value = read_u32(sub);
    if (!value) continue;
    if (*value != 0 && *value != 1) {
      out.push_back(make_finding(plugin.key, record.form_key,
                                 CheckCode::dialogue_branch_category_invalid,
                                 CheckSeverity::warning, sig_tnam, FormKey{},
                                 "DLBR TNAM category " + std::to_string(*value) +
                                     " is not 0 or 1"));
    }
    break;
  }
}

void check_info(const Plugin& plugin, const Record& record, const Topics& topics,
                const DialogueCheckContext& context, const PluginFormIdContext& id_context,
                std::vector<CheckFinding>& out) {
  const auto previous = first_stored_link(record, sig_pnam, id_context);
  if (!previous.key) return;
  const auto type = context.winners.find(*previous.key);
  if (type == context.winners.end() || type->second.signature != sig_info) return;
  const auto source_topic = topic_of(topics, record.form_key);
  const auto target_topic = topic_of(context.topics, *previous.key);
  if (!source_topic || !target_topic || *source_topic == *target_topic) return;
  out.push_back(make_finding(
      plugin.key, record.form_key, CheckCode::dialogue_previous_info_topic_mismatch,
      CheckSeverity::warning, sig_pnam, *previous.key,
      "PNAM previous INFO " + previous.key->to_string() + " lives under topic " +
          target_topic->to_string() + ", not " + source_topic->to_string()));
}

void check_scene_parent_quest(const Plugin& plugin, const Record& record,
                              const env::detail::SceneLayout& layout,
                              const DialogueCheckContext& context,
                              const PluginFormIdContext& id_context,
                              std::vector<CheckFinding>& out) {
  const auto* sub = first_subrecord_from(record, layout.tail, sig_pnam);
  if (sub == nullptr) {
    out.push_back(make_finding(plugin.key, record.form_key, CheckCode::scene_parent_quest_missing,
                               CheckSeverity::error, sig_pnam, FormKey{},
                               "SCEN tail PNAM (Parent Quest) is missing"));
    return;
  }
  if (sub->data.size() != 4) return;
  const auto link = read_stored_link(*sub, id_context);
  if (link.is_null) {
    out.push_back(make_finding(plugin.key, record.form_key, CheckCode::scene_parent_quest_missing,
                               CheckSeverity::error, sig_pnam, FormKey{},
                               "SCEN PNAM (Parent Quest) is null"));
    return;
  }
  if (!link.key) return;
  const auto type = context.winners.find(*link.key);
  if (type == context.winners.end() || type->second.signature == sig_qust) return;
  out.push_back(make_finding(plugin.key, record.form_key, CheckCode::scene_parent_quest_missing,
                             CheckSeverity::error, sig_pnam, *link.key,
                             "SCEN PNAM (Parent Quest) target " + link.key->to_string() + " is a " +
                                 type->second.signature.to_string() + ", not a QUST"));
}

std::optional<std::uint16_t> scene_action_type(const Record& record,
                                               const env::detail::SceneActionSpan& span) {
  const auto& data = record.subrecords[span.open].data;
  if (data.size() != 2) return std::nullopt;
  const auto value = static_cast<int>(data[0]) | (static_cast<int>(data[1]) << 8);
  return static_cast<std::uint16_t>(value);
}

bool is_action_topic_link(Signature signature) {
  return std::find(k_action_topic_links.begin(), k_action_topic_links.end(), signature) !=
         k_action_topic_links.end();
}

bool is_dialogue_action(std::uint16_t type) {
  return type == k_dialogue_action || type == k_player_dialogue_action ||
         type == k_npc_response_action || type == k_radio_action;
}

void check_scene_actions(const Plugin& plugin, const Record& record,
                         const env::detail::SceneLayout& layout,
                         const DialogueCheckContext& context,
                         const PluginFormIdContext& id_context,
                         std::vector<CheckFinding>& out) {
  std::set<std::int32_t> actor_aliases;
  for (const auto at : layout.actors) {
    const auto alias = read_i32(record.subrecords[at]);
    if (alias) actor_aliases.insert(*alias);
  }
  for (std::size_t index = 0; index < layout.actions.size(); ++index) {
    const auto& span = layout.actions[index];
    const auto type = scene_action_type(record, span);
    if (!type) continue;
    for (std::size_t at = span.open + 1; at < span.header_end; ++at) {
      const auto& sub = record.subrecords[at];
      if (sub.signature == sig_alid) {
        const auto alias = read_i32(sub);
        if (alias && *alias >= 0 && !actor_aliases.contains(*alias)) {
          out.push_back(make_finding(
              plugin.key, record.form_key, CheckCode::scene_action_alias_unknown,
              CheckSeverity::warning, sub.signature, FormKey{},
              "SCEN action " + std::to_string(index) + " ALID alias " +
                  std::to_string(*alias) + " matches no scene actor"));
        }
      } else if (sub.signature == sig_snam || sub.signature == sig_enam) {
        const auto phase = read_u32(sub);
        if (phase && *phase != 0xFFFFFFFFu &&
            static_cast<std::size_t>(*phase) >= layout.phases.size()) {
          const auto label = sub.signature == sig_snam ? " start phase " : " end phase ";
          out.push_back(make_finding(
              plugin.key, record.form_key, CheckCode::scene_action_phase_out_of_range,
              CheckSeverity::warning, sub.signature, FormKey{},
              "SCEN action " + std::to_string(index) + " " + sub.signature.to_string() + label +
                  std::to_string(*phase) + " is outside the scene's " +
                  std::to_string(layout.phases.size()) + " phases"));
        }
      }
    }
    if (!is_dialogue_action(*type)) continue;
    for (std::size_t at = span.header_end; at < span.close; ++at) {
      const auto& sub = record.subrecords[at];
      if (!is_action_topic_link(sub.signature)) continue;
      const auto link = read_stored_link(sub, id_context);
      if (!link.key) continue;
      const auto found = context.winners.find(*link.key);
      if (found == context.winners.end() || found->second.signature == sig_dial) continue;
      out.push_back(make_finding(
          plugin.key, record.form_key, CheckCode::scene_action_topic_not_dial,
          CheckSeverity::error, sub.signature, *link.key,
          "SCEN action " + std::to_string(index) + " " + sub.signature.to_string() +
              " target " + link.key->to_string() + " is a " +
              found->second.signature.to_string() + ", not a DIAL"));
    }
  }
}

void check_scene_index_order(const Plugin& plugin, const Record& record,
                             const env::detail::SceneLayout& layout,
                             std::vector<CheckFinding>& out) {
  if (layout.actions.empty()) return;
  std::uint32_t highest = 0;
  for (const auto& span : layout.actions) {
    for (std::size_t at = span.open + 1; at < span.header_end; ++at) {
      if (record.subrecords[at].signature != sig_inam) continue;
      const auto value = read_u32(record.subrecords[at]);
      if (value && *value > highest) highest = *value;
      break;
    }
  }
  const auto* tail = first_subrecord_from(record, layout.tail, sig_inam);
  if (tail == nullptr) {
    out.push_back(make_finding(
        plugin.key, record.form_key, CheckCode::scene_action_index_order,
        CheckSeverity::warning, sig_inam, FormKey{},
        "SCEN tail INAM (Last Action Index) is missing while " +
            std::to_string(layout.actions.size()) + " actions are present"));
    return;
  }
  const auto value = read_u32(*tail);
  if (!value || *value >= highest) return;
  out.push_back(make_finding(
      plugin.key, record.form_key, CheckCode::scene_action_index_order,
      CheckSeverity::warning, sig_inam, FormKey{},
      "SCEN tail INAM (Last Action Index) " + std::to_string(*value) +
          " is below action INAM " + std::to_string(highest)));
}

void check_scene(const Plugin& plugin, const Record& record,
                 const DialogueCheckContext& context, const PluginFormIdContext& id_context,
                 std::vector<CheckFinding>& out) {
  env::detail::SceneLayout layout;
  try {
    layout = env::detail::scene_layout(record.subrecords);
  } catch (const env::detail::SceneLayoutError&) {
    return;
  }
  check_scene_parent_quest(plugin, record, layout, context, id_context, out);
  check_scene_actions(plugin, record, layout, context, id_context, out);
  check_scene_index_order(plugin, record, layout, out);
}

}  // namespace

DialogueCheckContext make_dialogue_check_context(std::span<const Plugin* const> load_order) {
  DialogueCheckContext context;
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    const auto id_context = plugin_form_id_context(*plugin);
    for (const auto& group : plugin->groups) add_topics(group, id_context, context.topics);
    for (const auto* record : collect_first_records(*plugin)) {
      context.winners[record->form_key] = {record->header.signature, plugin->key};
    }
  }
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    const auto id_context = plugin_form_id_context(*plugin);
    for (const auto* record : collect_first_records(*plugin)) {
      if (record->header.signature != sig_dlbr) continue;
      const auto owner = context.winners.find(record->form_key);
      if (owner == context.winners.end() || owner->second.plugin != plugin->key) continue;
      const auto quest = first_stored_link(*record, sig_qnam, id_context);
      if (!quest.key) continue;
      const auto type = context.winners.find(*quest.key);
      if (type != context.winners.end() && type->second.signature == sig_qust) {
        context.branch_quests[record->form_key] = *quest.key;
      }
    }
  }
  context.info_cycles = find_info_cycles(load_order, context);
  for (const auto& entry : context.topics) {
    if (entry.second) ++context.info_counts[*entry.second];
  }
  return context;
}

std::vector<CheckFinding> check_dialogue(const Plugin& plugin,
                                         const DialogueCheckContext& context) {
  const auto id_context = plugin_form_id_context(plugin);
  const auto topics = plugin_topics(plugin);

  std::vector<CheckFinding> findings;
  for (const auto& cycle : context.info_cycles) {
    if (cycle.plugin != plugin.key) continue;
    findings.push_back(make_finding(plugin.key, cycle.record,
                                    CheckCode::dialogue_previous_info_cycle,
                                    CheckSeverity::error, sig_pnam, FormKey{},
                                    "INFO previous-info cycle: " + cycle.chain));
  }

  for (const auto* record : collect_first_records(plugin)) {
    const auto signature = record->header.signature;
    if (signature == sig_dial) {
      check_dial(plugin, *record, context, id_context, findings);
    } else if (signature == sig_dlbr) {
      check_dlbr(plugin, *record, findings);
    } else if (signature == sig_info) {
      check_info(plugin, *record, topics, context, id_context, findings);
    } else if (signature == sig_scen) {
      check_scene(plugin, *record, context, id_context, findings);
    }
  }
  return findings;
}

}  // namespace fo4
