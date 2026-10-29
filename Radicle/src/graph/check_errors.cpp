#include "fo4/graph/check_errors.hpp"

#include "check_errors_records.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/condition_functions.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "fo4/schema/preflight.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace fo4 {
namespace {

constexpr Signature k_sig_lvli = Signature::from_string("LVLI");
constexpr Signature k_sig_lvln = Signature::from_string("LVLN");
constexpr Signature k_sig_lvsp = Signature::from_string("LVSP");
constexpr Signature k_sig_info = Signature::from_string("INFO");
constexpr Signature k_sig_dial = Signature::from_string("DIAL");
constexpr Signature k_sig_dlbr = Signature::from_string("DLBR");
constexpr Signature k_sig_snam = Signature::from_string("SNAM");
constexpr Signature k_sig_rfgp = Signature::from_string("RFGP");
constexpr Signature k_sig_rnam = Signature::from_string("RNAM");
constexpr Signature k_sig_regn = Signature::from_string("REGN");
constexpr Signature k_sig_wnam = Signature::from_string("WNAM");
constexpr Signature k_sig_refr = Signature::from_string("REFR");
constexpr Signature k_sig_achr = Signature::from_string("ACHR");
constexpr Signature k_sig_scco = Signature::from_string("SCCO");
constexpr Signature k_sig_xrfg = Signature::from_string("XRFG");
constexpr Signature k_sig_xmsp = Signature::from_string("XMSP");
constexpr Signature k_sig_xlcn = Signature::from_string("XLCN");
constexpr Signature k_sig_xlkr = Signature::from_string("XLKR");
constexpr Signature k_sig_vmad = Signature::from_string("VMAD");

struct GameMasterLinkExemption {
  Signature record;
  Signature subrecord;
};

constexpr std::array<GameMasterLinkExemption, 7> k_game_master_link_exemptions{{
    {k_sig_rfgp, k_sig_rnam},
    {k_sig_regn, k_sig_wnam},
    {k_sig_refr, k_sig_xrfg},
    {k_sig_refr, k_sig_xmsp},
    {k_sig_refr, k_sig_xlkr},
    {k_sig_achr, k_sig_xlcn},
    {k_sig_scco, k_sig_snam},
}};

struct WinnerInfo {
  Signature signature{};
  bool deleted{false};
  PluginKey plugin;
};

bool is_leveled(Signature signature) noexcept {
  return signature == k_sig_lvli || signature == k_sig_lvln || signature == k_sig_lvsp;
}

std::map<FormKey, WinnerInfo> winning_records(std::span<const Plugin* const> load_order) {
  std::map<FormKey, WinnerInfo> winners;
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    for (const auto* record : collect_first_records(*plugin)) {
      if (record->form_key.owner().filename().empty()) continue;
      winners[record->form_key] = {record->header.signature, record->header.flags.is_deleted(),
                                    plugin->key};
    }
  }
  return winners;
}

struct SlotTargets {
  bool wildcard{false};
  std::span<const Signature> targets;
};

constexpr std::size_t k_ctda_compare = 4;
constexpr std::size_t k_ctda_function = 8;
constexpr std::size_t k_ctda_param1 = 12;
constexpr std::size_t k_ctda_param2 = 16;
constexpr std::size_t k_ctda_run_on = 20;
constexpr std::size_t k_ctda_reference = 24;

constexpr std::array<Signature, 1> k_glob_targets{Signature::from_string("GLOB")};
constexpr std::array<Signature, 12> k_reference_targets{
    Signature::from_string("NULL"), Signature::from_string("PLYR"), Signature::from_string("ACHR"),
    Signature::from_string("REFR"), Signature::from_string("PGRE"), Signature::from_string("PHZD"),
    Signature::from_string("PMIS"), Signature::from_string("PARW"), Signature::from_string("PBAR"),
    Signature::from_string("PBEA"), Signature::from_string("PCON"), Signature::from_string("PFLA")};

template <typename T>
T read_payload(const Subrecord& sub, std::size_t offset) {
  T value{};
  std::memcpy(&value, sub.data.data() + offset, sizeof(T));
  return value;
}

const Subrecord* nth_subrecord(const Record& record, Signature signature, std::size_t ordinal) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature && ordinal-- == 0) return &sub;
  }
  return nullptr;
}

std::optional<SlotTargets> ctda_slot_targets(const Record& record, std::size_t ordinal,
                                             std::size_t offset) {
  const auto* sub = nth_subrecord(record, sig_ctda, ordinal);
  if (sub == nullptr) return std::nullopt;
  if (offset == k_ctda_compare) return SlotTargets{false, k_glob_targets};
  if (offset == k_ctda_reference) return SlotTargets{false, k_reference_targets};
  if (offset != k_ctda_param1 && offset != k_ctda_param2) return std::nullopt;
  const auto* desc = schema::condition_function(read_payload<std::uint16_t>(*sub, k_ctda_function));
  if (desc == nullptr) return std::nullopt;
  const auto* type = desc->parameters[offset == k_ctda_param1 ? 0 : 1];
  if (type == nullptr || type->kind != schema::ConditionParameterKind::form_link ||
      type->targets.empty()) {
    return SlotTargets{true, {}};
  }
  return SlotTargets{false, type->targets};
}

std::optional<SlotTargets> slot_targets(const Record& record,
                                        Signature subrecord,
                                        std::size_t ordinal,
                                        std::size_t offset) {
  if (subrecord == sig_ctda) return ctda_slot_targets(record, ordinal, offset);
  if (const auto field = schema::formlink_field_at(schema::formlink_fields(record.header.signature),
                                                   record, subrecord, ordinal, offset)) {
    return SlotTargets{false, schema::formlink_field_targets(*field)};
  }
  for (const auto& list : schema::k_generated_formlink_lists) {
    if (list.record != record.header.signature || list.subrecord != subrecord) continue;
    const bool slot_matches = list.storage == schema::FormLinkListStorage::per_subrecord
                                  ? offset == 0
                                  : offset % 4 == 0;
    if (!slot_matches) continue;
    return SlotTargets{list.wildcard_target, schema::formlink_list_targets(list)};
  }
  return std::nullopt;
}

std::string join_signatures(std::span<const Signature> signatures) {
  std::string out;
  for (const auto signature : signatures) {
    if (!out.empty()) out += ", ";
    out += signature.to_string();
  }
  return out;
}

struct FindingSink {
  std::size_t limit{0};
  std::vector<CheckFinding> findings;
  bool truncated{false};
  PluginKey truncated_plugin;

  [[nodiscard]] bool full() const noexcept { return limit != 0 && findings.size() >= limit; }
  void add(CheckFinding finding) {
    if (full()) {
      truncated = true;
      if (truncated_plugin.filename().empty()) truncated_plugin = finding.plugin;
      return;
    }
    findings.push_back(std::move(finding));
  }
};

constexpr std::uint32_t k_first_non_hardcoded_id = 0x800;
constexpr std::string_view k_game_master = "Fallout4.esm";

void check_links(const Plugin& plugin,
                 const std::map<FormKey, const Record*>& records,
                 const std::map<FormKey, WinnerInfo>& winners,
                 const PluginKey& game_master,
                 FindingSink& sink) {
  const auto context = plugin_form_id_context(plugin);
  const auto detailed = collect_form_links_detailed(plugin);
  for (const auto& key : detailed.unsupported_vmad_records) {
    if (sink.full()) return;
    CheckFinding unsupported;
    unsupported.code = CheckCode::unsupported_vmad_property;
    unsupported.severity = CheckSeverity::warning;
    unsupported.plugin = plugin.key;
    unsupported.record = key;
    unsupported.subrecord = k_sig_vmad;
    unsupported.message =
        "VMAD holds properties Radicle cannot parse; links after them are unchecked";
    sink.add(std::move(unsupported));
  }
  for (const auto& hit : detailed.links) {
    if (sink.full()) return;
    if (hit.raw.is_null() || hit.raw.is_none()) continue;

    // Requires: an index past the master table never names a plugin.
    if (hit.raw.index_byte() > context.masters.size()) {
      CheckFinding finding;
      finding.code = CheckCode::invalid_master_index;
      finding.severity = CheckSeverity::warning;
      finding.plugin = plugin.key;
      finding.record = hit.record;
      finding.subrecord = hit.subrecord;
      finding.offset = hit.offset;
      finding.message = hit.subrecord.to_string() + " holds master index " +
                        std::to_string(hit.raw.index_byte()) + " which is outside " +
                        std::string{plugin.key.filename()} + "'s master table";
      sink.add(std::move(finding));
      continue;
    }

    CheckFinding finding;
    finding.plugin = plugin.key;
    finding.record = hit.record;
    finding.subrecord = hit.subrecord;
    finding.offset = hit.offset;
    finding.target = hit.key;

    const auto owner = records.find(hit.record);
    const auto slot = owner == records.end()
                          ? std::nullopt
                          : slot_targets(*owner->second, hit.subrecord, hit.subrecord_ordinal, hit.offset);
    // Requires: xEdit resolves ObjectID < $800 in the game master (masterless files null the file ID first).
    auto target = winners.find(hit.key);
    if (target == winners.end()) {
      auto effective = hit.raw;
      if (effective.local_24() < k_first_non_hardcoded_id && context.masters.empty()) {
        effective = RawFormId{effective.local_24()};
      }
      if (effective.value() < k_first_non_hardcoded_id) {
        target = winners.find(FormKey{game_master, LocalFormId{effective.local_24()}});
        if (target == winners.end()) continue;
      }
    }
    if (target == winners.end()) {
      if (hit.subrecord == k_sig_snam && owner != records.end() &&
          owner->second->header.signature == k_sig_dlbr) {
        continue;
      }
      if (hit.key.owner() == game_master && owner != records.end() &&
          hit.record.owner() == game_master &&
          std::any_of(k_game_master_link_exemptions.begin(), k_game_master_link_exemptions.end(),
                      [&](const GameMasterLinkExemption& exemption) {
                        return exemption.record == owner->second->header.signature &&
                               exemption.subrecord == hit.subrecord;
                      })) {
        continue;
      }
      finding.code = CheckCode::unresolved_link;
      finding.severity = slot ? CheckSeverity::error : CheckSeverity::warning;
      finding.message = hit.subrecord.to_string() + " target " + hit.key.to_string() +
                        " is not in any loaded plugin" +
                        (slot ? "" : " (slot not proven by the schema; it may not be a form ID)");
      sink.add(std::move(finding));
      continue;
    }

    if (target->second.deleted) {
      finding.code = CheckCode::deleted_target;
      finding.severity = CheckSeverity::warning;
      finding.message = hit.subrecord.to_string() + " target " + hit.key.to_string() +
                        " is deleted in its winning version";
      sink.add(finding);
    }

    // Requires: DLBR SNAM type errors belong to check_dialogue_links (one finding, like missing).
    if (hit.subrecord == k_sig_snam && owner != records.end() &&
        owner->second->header.signature == k_sig_dlbr && target->second.signature != k_sig_dial) {
      continue;
    }
    if (!slot || slot->wildcard || slot->targets.empty()) continue;
    if (std::find(slot->targets.begin(), slot->targets.end(), target->second.signature) !=
        slot->targets.end()) {
      continue;
    }
    finding.code = CheckCode::wrong_target_type;
    finding.severity = CheckSeverity::error;
    finding.message = hit.subrecord.to_string() + " target " + hit.key.to_string() + " is " +
                      target->second.signature.to_string() + ", expected one of " +
                      join_signatures(slot->targets);
    sink.add(std::move(finding));
  }
}

void check_conditions(const Plugin& plugin, FindingSink& sink) {
  const auto add = [&](const Record& record, std::size_t offset, std::string message) {
    CheckFinding finding;
    finding.code = CheckCode::invalid_condition;
    finding.severity = CheckSeverity::error;
    finding.plugin = plugin.key;
    finding.record = record.form_key;
    finding.subrecord = sig_ctda;
    finding.offset = offset;
    finding.message = std::move(message);
    sink.add(std::move(finding));
  };
  for (const auto* record : collect_records(plugin)) {
    for (const auto& sub : record->subrecords) {
      if (sub.signature != sig_ctda || sub.data.size() < k_ctda_function + 2) continue;
      const auto function = read_payload<std::uint16_t>(sub, k_ctda_function);
      if (schema::condition_function(function) == nullptr) {
        add(*record, k_ctda_function,
            "CTDA function index " + std::to_string(function) +
                " is not a known condition function");
      }
      if (sub.data.size() < k_ctda_run_on + 4) continue;
      const auto run_on = read_payload<std::uint32_t>(sub, k_ctda_run_on);
      if (schema::condition_run_on(run_on) == nullptr) {
        const auto run_ons = schema::condition_run_ons();
        add(*record, k_ctda_run_on,
            "CTDA Run On " + std::to_string(run_on) + " is not a known Run On value (" +
                std::to_string(run_ons.front().value) + "-" + std::to_string(run_ons.back().value) +
                ")");
      }
    }
  }
}

void check_preflight(const Plugin& plugin, FindingSink& sink) {
  for (auto& issue : schema::preflight_plugin(plugin)) {
    if (sink.full()) return;
    CheckFinding finding;
    finding.code = CheckCode::schema_preflight;
    finding.severity = issue.severity == schema::PreflightSeverity::error ? CheckSeverity::error
                                                                          : CheckSeverity::warning;
    finding.plugin = plugin.key;
    finding.record = issue.record;
    finding.message = issue.path + ": " + issue.message;
    finding.detail = issue.code;
    sink.add(std::move(finding));
  }
}

using InfoTopics = std::map<FormKey, std::optional<FormKey>>;

std::optional<FormKey> resolved_or_empty(RawFormId raw, const PluginFormIdContext& context) {
  if (raw.is_null() || raw.is_created()) return FormKey{};
  try {
    // Requires: out-of-range labels fold to the current plugin (resolve_plugin_link) so they are checked.
    return resolve_plugin_link(raw, context);
  } catch (const FormIdError&) {
    return std::nullopt;
  }
}

bool has_null_snam(const Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != k_sig_snam) continue;
    if (sub.data.size() != 4) return false;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data(), sizeof(value));
    return RawFormId{value}.is_null();
  }
  return false;
}

std::optional<FormKey> stored_form_link(const Record& record, Signature subrecord,
                                        const PluginFormIdContext& context) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != subrecord) continue;
    if (sub.data.size() != 4) return std::nullopt;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data(), sizeof(value));
    const RawFormId raw{value};
    if (raw.is_null()) return std::nullopt;
    return resolved_or_empty(raw, context);
  }
  return std::nullopt;
}

void add_info_topics(const Group& group, const PluginFormIdContext& context, InfoTopics& out) {
  std::optional<FormKey> topic;
  if (group.header.group_type == static_cast<std::int32_t>(GroupType::topic_children)) {
    topic = resolved_or_empty(RawFormId{group.header.label_as_u32()}, context);
  }
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (record->header.signature == k_sig_info) out.try_emplace(record->form_key, topic);
    } else {
      add_info_topics(std::get<Group>(child), context, out);
    }
  }
}

InfoTopics info_topics(const Plugin& plugin) {
  const auto context = plugin_form_id_context(plugin);
  InfoTopics out;
  for (const auto& group : plugin.groups) add_info_topics(group, context, out);
  return out;
}

bool plugin_selected(const CheckOptions& options, const PluginKey& key) {
  return options.plugins.empty() ||
         std::find(options.plugins.begin(), options.plugins.end(), key) != options.plugins.end();
}

void check_injected_records(std::span<const Plugin* const> load_order,
                            const CheckOptions& options,
                            FindingSink& sink) {
  std::map<PluginKey, std::set<FormKey>> present;
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    auto& keys = present[plugin->key];
    for (const auto* record : collect_records(*plugin)) keys.insert(record->form_key);
  }

  std::set<FormKey> claimed;
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    for (const auto* record : collect_records(*plugin)) {
      if (sink.full()) return;
      const auto& key = record->form_key;
      const bool first = claimed.insert(key).second;
      if (key.owner() == plugin->key) continue;
      const auto owner = present.find(key.owner());
      if (owner == present.end()) continue;
      if (owner->second.contains(key)) continue;
      if (!first) continue;
      if (!plugin_selected(options, plugin->key)) continue;
      CheckFinding finding;
      finding.code = CheckCode::injected_record;
      finding.severity = CheckSeverity::warning;
      finding.plugin = plugin->key;
      finding.record = key;
      finding.message = key.to_string() + " sits in " + std::string{plugin->key.filename()} +
                        " but its form ID belongs to " +
                        std::string{key.owner().filename()} + ", which has no such record";
      sink.add(std::move(finding));
    }
  }
}

void check_dialogue_links(const Plugin& plugin,
                          const std::map<FormKey, WinnerInfo>& winners,
                          FindingSink& sink) {
  const auto context = plugin_form_id_context(plugin);
  const auto topics = info_topics(plugin);
  const auto is_loaded = [&](const FormKey& key, Signature signature) {
    const auto found = winners.find(key);
    return found != winners.end() && found->second.signature == signature;
  };
  const auto add = [&](const Record& record, CheckCode code, CheckSeverity severity,
                       Signature subrecord, const FormKey& target, std::string message) {
    CheckFinding finding;
    finding.code = code;
    finding.severity = severity;
    finding.plugin = plugin.key;
    finding.record = record.form_key;
    finding.subrecord = subrecord;
    finding.target = target;
    finding.message = std::move(message);
    sink.add(std::move(finding));
  };

  for (const auto* record : collect_first_records(plugin)) {
    if (record->header.signature == k_sig_dlbr) {
      const auto start = stored_form_link(*record, k_sig_snam, context);
      // Requires: DLBR SNAM is required (xEdit SetRequired); a null link must report.
      if (!start && has_null_snam(*record)) {
        add(*record, CheckCode::branch_starting_topic_not_dial, CheckSeverity::error, k_sig_snam,
            FormKey{}, "SNAM is null but a branch starting topic is required");
      } else if (start && !is_loaded(*start, k_sig_dial)) {
        add(*record, CheckCode::branch_starting_topic_not_dial, CheckSeverity::error, k_sig_snam,
            *start, "SNAM target " + start->to_string() + " is not a loaded DIAL");
      }
      continue;
    }
    if (record->header.signature != k_sig_info) continue;
    const auto topic = topics.find(record->form_key);
    if (topic == topics.end()) continue;
    if (topic->second && !is_loaded(*topic->second, k_sig_dial)) {
      add(*record, CheckCode::topic_children_not_dial, CheckSeverity::warning, Signature{},
          *topic->second,
          "INFO topic children label " + topic->second->to_string() + " is not a loaded DIAL");
    }
  }
}

}  // namespace

std::string_view check_code_name(CheckCode code) noexcept {
  switch (code) {
    case CheckCode::invalid_master_index: return "invalid_master_index";
    case CheckCode::unresolved_link: return "unresolved_link";
    case CheckCode::wrong_target_type: return "wrong_target_type";
    case CheckCode::deleted_target: return "deleted_target";
    case CheckCode::circular_leveled_list: return "circular_leveled_list";
    case CheckCode::schema_preflight: return "schema_preflight";
    case CheckCode::branch_starting_topic_not_dial: return "branch_starting_topic_not_dial";
    case CheckCode::topic_children_not_dial: return "topic_children_not_dial";
    case CheckCode::deleted_record_content: return "deleted_record_content";
    case CheckCode::partial_form_content: return "partial_form_content";
    case CheckCode::light_object_id_overflow: return "light_object_id_overflow";
    case CheckCode::internal_form_id_mismatch: return "internal_form_id_mismatch";
    case CheckCode::invalid_condition: return "invalid_condition";
    case CheckCode::injected_record: return "injected_record";
    case CheckCode::unsupported_vmad_property: return "unsupported_vmad_property";
    case CheckCode::results_truncated: return "results_truncated";
  }
  return "unknown";
}

std::string_view check_severity_name(CheckSeverity severity) noexcept {
  return severity == CheckSeverity::error ? "error" : "warning";
}

std::vector<std::vector<FormKey>> find_leveled_list_cycles(
    std::span<const Plugin* const> load_order) {
  std::map<FormKey, std::vector<FormKey>> edges;
  for (const auto* plugin : load_order) {
    if (plugin == nullptr) continue;
    std::map<FormKey, std::vector<FormKey>> by_record;
    for (const auto& link : collect_form_links(*plugin)) {
      by_record[link.record].push_back(link.key);
    }
    for (const auto* record : collect_first_records(*plugin)) {
      if (!is_leveled(record->header.signature)) continue;
      edges[record->form_key] = std::move(by_record[record->form_key]);
    }
  }

  std::map<FormKey, int> state;
  std::set<std::vector<FormKey>> reported;
  std::vector<std::vector<FormKey>> cycles;
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
        std::rotate(identity.begin(),
                    std::min_element(identity.begin(), identity.end()),
                    identity.end());
        if (reported.insert(identity).second) cycles.push_back(std::move(cycle));
      } else if (state[child] == 0) {
        state[child] = 1;
        path.push_back(child);
        stack.emplace_back(child, 0);
      }
    }
  }
  return cycles;
}

std::vector<CheckFinding> check_load_order(std::span<const Plugin* const> load_order,
                                           const CheckOptions& options,
                                           CheckRunSummary* summary) {
  const auto winners = winning_records(load_order);
  FindingSink sink{options.limit, {}};
  const auto game_master = PluginKey::from_filename(k_game_master);

  // Requires: leveled-list cycles report before per-record findings so a limit cannot starve them.
  for (const auto& cycle : find_leveled_list_cycles(load_order)) {
    const auto member = std::find_if(cycle.begin(), cycle.end(), [&](const FormKey& key) {
      const auto it = winners.find(key);
      return it != winners.end() && plugin_selected(options, it->second.plugin);
    });
    if (member == cycle.end()) continue;
    std::string chain;
    for (const auto& key : cycle) {
      if (!chain.empty()) chain += " -> ";
      chain += key.to_string();
    }
    CheckFinding finding;
    finding.code = CheckCode::circular_leveled_list;
    finding.severity = CheckSeverity::error;
    finding.plugin = winners.at(*member).plugin;
    finding.record = *member;
    finding.message = "leveled list cycle: " + chain;
    sink.add(std::move(finding));
  }

  for (const auto* plugin : load_order) {
    if (plugin == nullptr || !plugin_selected(options, plugin->key)) continue;
    std::map<FormKey, const Record*> records;
    for (const auto* record : collect_records(*plugin)) {
      records.try_emplace(record->form_key, record);
    }
    check_links(*plugin, records, winners, game_master, sink);
    check_dialogue_links(*plugin, winners, sink);
    check_preflight(*plugin, sink);
    for (auto& finding : check_record_levels(*plugin)) sink.add(std::move(finding));
    check_conditions(*plugin, sink);
  }

  check_injected_records(load_order, options, sink);

  if (sink.truncated) {
    CheckFinding finding;
    finding.code = CheckCode::results_truncated;
    finding.severity = CheckSeverity::warning;
    finding.plugin = sink.truncated_plugin;
    finding.message =
        "finding limit " + std::to_string(options.limit) + " reached; results truncated";
    sink.findings.push_back(std::move(finding));
  }
  if (summary != nullptr) summary->truncated = sink.truncated;

  return std::move(sink.findings);
}

}  // namespace fo4
