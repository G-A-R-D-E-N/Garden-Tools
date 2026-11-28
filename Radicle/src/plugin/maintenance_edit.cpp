#include "fo4/plugin/maintenance.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace fo4 {

CleanDeletedResult clean_deleted_records(Plugin& plugin) {
  CleanDeletedResult result;
  assign_record_form_keys(plugin);
  auto next = plugin;
  visit_records(next, [&](Record& record) {
    if (record.header.signature == sig_tes4 || !record.header.flags.is_deleted()) return;
    if (record.undecodable) {
      ++result.skipped;
      return;
    }
    record.header.flags = RecordFlags{(record.header.flags.value() & ~RecordFlags::deleted) |
                                      RecordFlags::initially_disabled};
    detail::mark_record_modified(next, record);
    ++result.undeleted;
  });
  if (result.undeleted != 0) plugin = std::move(next);
  return result;
}

RenumberAllResult renumber_all_local_records(Plugin& plugin, std::uint32_t start_local) {
  RenumberAllResult result;
  detail::require_plugin_decodable(plugin, "renumber");
  assign_record_form_keys(plugin);

  std::set<FormKey> top_level;
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top)) continue;
    for (const auto& child : group.children) {
      if (const auto* record = std::get_if<Record>(&child)) top_level.insert(record->form_key);
    }
  }

  std::vector<std::pair<std::uint32_t, FormKey>> own;
  for (const auto* record : collect_records(plugin)) {
    if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) continue;
    if (!top_level.contains(record->form_key)) {
      result.blockers.push_back(record->form_key);
      continue;
    }
    own.emplace_back(record->form_key.local().value(), record->form_key);
  }
  if (!result.blockers.empty()) {
    result.status = RenumberAllStatus::blocked_nested_records;
    return result;
  }
  if (own.empty()) return result;
  std::sort(own.begin(), own.end(), [](const auto& left, const auto& right) { return left.first < right.first; });

  const auto [lowest, highest] = local_form_id_range(plugin);
  if (start_local < lowest || start_local > highest || own.size() - 1 > highest - start_local) {
    result.status = RenumberAllStatus::out_of_range;
    return result;
  }

  std::vector<std::pair<std::uint32_t, FormKey>> down;
  std::vector<std::pair<std::uint32_t, FormKey>> up;
  for (std::size_t i = 0; i < own.size(); ++i) {
    const auto target = start_local + static_cast<std::uint32_t>(i);
    if (target < own[i].first) down.emplace_back(target, own[i].second);
    if (target > own[i].first) up.emplace_back(target, own[i].second);
  }
  if (down.empty() && up.empty()) {
    result.status = RenumberAllStatus::unchanged;
    return result;
  }

  auto next = plugin;
  const auto move = [&](const std::pair<std::uint32_t, FormKey>& item) {
    if (detail::renumber_local_record_in_place(next, item.second, LocalFormId{item.first}, true) != RecordRenumberStatus::renumbered) {
      result.blockers.push_back(item.second);
      return false;
    }
    ++result.renumbered;
    return true;
  };
  for (const auto& item : down) {
    if (!move(item)) {
      result.status = RenumberAllStatus::rekey_failed;
      result.renumbered = 0;
      return result;
    }
  }
  for (auto it = up.rbegin(); it != up.rend(); ++it) {
    if (!move(*it)) {
      result.status = RenumberAllStatus::rekey_failed;
      result.renumbered = 0;
      return result;
    }
  }
  plugin = std::move(next);
  result.status = RenumberAllStatus::renumbered;
  return result;
}

}  // namespace fo4
