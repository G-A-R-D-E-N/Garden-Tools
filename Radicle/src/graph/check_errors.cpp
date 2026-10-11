#include "fo4/graph/check_errors.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/formlink_slots.hpp"
#include "fo4/schema/preflight.hpp"

#include <algorithm>
#include <cstdint>
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

std::optional<SlotTargets> slot_targets(const Record& record,
                                        Signature subrecord,
                                        std::size_t offset) {
  if (const auto field = schema::formlink_field_at(schema::formlink_fields(record.header.signature),
                                                   record, subrecord, offset)) {
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

  [[nodiscard]] bool full() const noexcept { return limit != 0 && findings.size() >= limit; }
  void add(CheckFinding finding) {
    if (!full()) findings.push_back(std::move(finding));
  }
};

constexpr std::uint32_t k_first_non_hardcoded_id = 0x800;
constexpr std::string_view k_game_master = "Fallout4.esm";

void check_links(const Plugin& plugin,
                 const std::map<FormKey, const Record*>& records,
                 const std::map<FormKey, WinnerInfo>& winners,
                 const PluginKey& game_master,
                 FindingSink& sink) {
  for (const auto& hit : collect_form_links(plugin)) {
    if (sink.full()) return;
    if (hit.raw.is_null() || hit.raw.is_none()) continue;

    CheckFinding finding;
    finding.plugin = plugin.key;
    finding.record = hit.record;
    finding.subrecord = hit.subrecord;
    finding.offset = hit.offset;
    finding.target = hit.key;

    if (hit.key.owner().filename().empty()) {
      finding.code = CheckCode::invalid_master_index;
      finding.severity = CheckSeverity::warning;
      finding.message = hit.subrecord.to_string() + " holds a form ID that does not resolve to any plugin";
      sink.add(std::move(finding));
      continue;
    }

    const auto owner = records.find(hit.record);
    const auto slot = owner == records.end()
                          ? std::nullopt
                          : slot_targets(*owner->second, hit.subrecord, hit.offset);
    const auto target = winners.find(hit.key);
    if (target == winners.end() && hit.key.owner() == game_master &&
        hit.key.local().value() < k_first_non_hardcoded_id) {
      continue;
    }
    if (target == winners.end()) {
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

void check_preflight(const Plugin& plugin, FindingSink& sink) {
  for (auto& issue : schema::preflight_plugin(plugin)) {
    if (sink.full()) return;
    CheckFinding finding;
    finding.code = CheckCode::schema_preflight;
    finding.severity = issue.severity == schema::PreflightSeverity::error ? CheckSeverity::error
                                                                          : CheckSeverity::warning;
    finding.plugin = plugin.key;
    finding.record = issue.record;
    finding.message = issue.path + ": " + issue.message + " (" + issue.code + ")";
    sink.add(std::move(finding));
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
                                           const CheckOptions& options) {
  const auto selected = [&](const PluginKey& key) {
    return options.plugins.empty() ||
           std::find(options.plugins.begin(), options.plugins.end(), key) != options.plugins.end();
  };

  const auto winners = winning_records(load_order);
  FindingSink sink{options.limit, {}};
  const auto game_master = PluginKey::from_filename(k_game_master);

  for (const auto* plugin : load_order) {
    if (plugin == nullptr || !selected(plugin->key)) continue;
    std::map<FormKey, const Record*> records;
    for (const auto* record : collect_records(*plugin)) {
      records.try_emplace(record->form_key, record);
    }
    check_links(*plugin, records, winners, game_master, sink);
    check_preflight(*plugin, sink);
  }

  for (const auto& cycle : find_leveled_list_cycles(load_order)) {
    const auto member = std::find_if(cycle.begin(), cycle.end(), [&](const FormKey& key) {
      const auto it = winners.find(key);
      return it != winners.end() && selected(it->second.plugin);
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

  return std::move(sink.findings);
}

}  // namespace fo4
