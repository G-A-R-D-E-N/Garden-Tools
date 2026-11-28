#include "fo4/plugin/maintenance.hpp"

#include "child_groups.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "identical_record.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/placement.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/graph/conflict_graph.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_links.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fo4 {

InjectedCleanupStatus cleanup_injected_form_list(std::span<Plugin*> load_order,
                                                  std::size_t plugin_index,
                                                  const FormKey& key) {
  const auto unsupported = InjectedCleanupStatus::unsupported;
  try {
    if (plugin_index >= load_order.size() || load_order[plugin_index] == nullptr) return unsupported;
    std::map<PluginKey, std::size_t> indices;
    for (std::size_t i = 0; i < load_order.size(); ++i) {
      if (load_order[i] == nullptr || !indices.emplace(load_order[i]->key, i).second) return unsupported;
      for (const auto& master : load_order[i]->header.masters) {
        const auto found = indices.find(master.key);
        if (found == indices.end() || found->second >= i) return unsupported;
      }
    }
    const auto& referring = *load_order[plugin_index];
    if (key.owner() != referring.key) return unsupported;
    const auto* record = find_record(referring, key);
    if (record == nullptr || record->undecodable || record->header.signature.view() != "FLST") return unsupported;
    std::set<FormKey> relevant{key};
    for (const auto& sub : record->subrecords) {
      if (sub.signature.view() != "LNAM" || sub.data.size() != 4) continue;
      std::uint32_t raw = 0;
      for (unsigned j = 0; j != 4; ++j) raw |= std::to_integer<std::uint32_t>(sub.data[j]) << (8 * j);
      if (raw != 0 && raw != 0xffffffffu)
        relevant.insert(resolve_plugin_form_id(RawFormId{raw}, plugin_form_id_context(referring)));
    }
    std::set<std::size_t> duplicate_plugins{plugin_index};
    for (const auto& target : relevant) {
      for (std::size_t i = 0; i < load_order.size(); ++i) {
        if (find_record(*load_order[i], target) != nullptr) duplicate_plugins.insert(i);
      }
    }
    for (std::size_t i : duplicate_plugins) {
      std::set<FormKey> seen;
      for (const auto* item : collect_records(*load_order[i])) {
        if ((i == plugin_index || relevant.contains(item->form_key)) && !seen.insert(item->form_key).second)
          return unsupported;
      }
    }
    std::map<FormKey, std::size_t> first_holder;
    auto first_holder_index = [&](const FormKey& target) -> std::optional<std::size_t> {
      const auto cached = first_holder.find(target);
      if (cached != first_holder.end()) return cached->second;
      for (std::size_t j = 0; j < load_order.size(); ++j) {
        if (find_record(*load_order[j], target) != nullptr) {
          first_holder.emplace(target, j);
          return j;
        }
      }
      return std::nullopt;
    };
    std::size_t editor_ids = 0;
    std::optional<std::size_t> source_index;
    std::vector<std::size_t> remove;
    for (std::size_t i = 0; i < record->subrecords.size(); ++i) {
      const auto& sub = record->subrecords[i];
      if (sub.signature.view() == "EDID") { if (++editor_ids > 1) return unsupported; continue; }
      if (sub.signature.view() != "LNAM" || sub.data.size() != 4) return unsupported;
      std::uint32_t raw = 0;
      for (unsigned j = 0; j != 4; ++j) raw |= std::to_integer<std::uint32_t>(sub.data[j]) << (8 * j);
      if (raw == 0) continue;
      if (raw == 0xffffffffu) return unsupported;
      const auto target = resolve_plugin_form_id(RawFormId{raw}, plugin_form_id_context(referring));
      const auto owner = indices.find(target.owner());
      if (owner == indices.end()) return unsupported;
      const auto first = first_holder_index(target);
      if (!first) return unsupported;
      if (*first == owner->second || *first == plugin_index) continue;
      const auto& source_key = load_order[*first]->key;
      bool source_is_master = false;
      for (const auto& master : referring.header.masters) source_is_master |= master.key == source_key;
      if (source_is_master) continue;
      if (*first <= plugin_index || (source_index && *source_index != *first)) return unsupported;
      source_index = *first;
      remove.push_back(i);
    }
    if (!source_index) return InjectedCleanupStatus::no_injected_references;
    std::vector<const Plugin*> graph_plugins(load_order.begin(), load_order.end());
    const auto graph = ConflictGraph::build(graph_plugins);
    const auto* winner = graph.winner(key);
    if (winner == nullptr || winner->load_order != plugin_index) return unsupported;
    const auto& source = *load_order[*source_index];
    std::set<FormKey> source_records;
    for (const auto* item : collect_records(source)) {
      if (!source_records.insert(item->form_key).second) return unsupported;
    }
    for (const auto& sub : source.tes4.subrecords) {
      const auto sig = sub.signature.view();
      if (sig != "HEDR" && sig != "MAST" && sig != "DATA" && sig != "CNAM" && sig != "SNAM") return unsupported;
    }
    for (const auto& group : source.groups) {
      if (group.header.group_type != 0 || group.header.top_label().view() != "KYWD") return unsupported;
      for (const auto& child : group.children) {
        const auto* item = std::get_if<Record>(&child);
        if (item == nullptr || item->undecodable || item->header.signature.view() != "KYWD") return unsupported;
        for (const auto& sub : item->subrecords) if (sub.signature.view() != "EDID") return unsupported;
      }
    }
    auto next_source = source;
    auto next_referring = referring;
    if (copy_record_as_override(next_source, referring, key, false) != RecordCopyStatus::inserted) return unsupported;
    std::vector<PluginKey> load_order_keys;
    for (const auto* item : load_order) load_order_keys.push_back(item->key);
    next_source = sort_plugin_masters(std::move(next_source), load_order_keys);
    std::optional<std::size_t> previous_master;
    for (const auto& master : next_source.header.masters) {
      const auto index = indices.at(master.key);
      if (previous_master && index <= *previous_master) return unsupported;
      previous_master = index;
    }
    visit_records(next_referring, [&](Record& item) {
      if (item.form_key != key) return;
      for (auto it = remove.rbegin(); it != remove.rend(); ++it)
        item.subrecords.erase(item.subrecords.begin() + static_cast<std::ptrdiff_t>(*it));
      detail::mark_record_modified(next_referring, item);
    });
    static_assert(std::is_nothrow_move_assignable_v<Plugin>);
    *load_order[*source_index] = std::move(next_source);
    *load_order[plugin_index] = std::move(next_referring);
    return InjectedCleanupStatus::updated;
  } catch (const std::exception&) {
    return unsupported;
  }
}

namespace {

using detail::require_plugin_decodable;

using identical::remapped_into;
using identical::same_record;

struct ItmScan {
  std::span<const Plugin* const> load_order;
  std::size_t plugin_index;
  const Plugin& plugin;
  PluginFormIdContext context;
  std::vector<IdenticalToMaster> out;
  std::map<std::size_t, std::map<FormKey, RecordPlacement>> placements{};
  bool exact{false};

  const RecordPlacement& placement_in(std::size_t index, const FormKey& key) {
    auto [it, inserted] = placements.try_emplace(index);
    if (inserted) it->second = record_placements(*load_order[index]);
    return it->second[key];
  }

  std::optional<std::size_t> owner_index(const FormKey& key) const {
    for (std::size_t index = 0; index < plugin_index; ++index) {
      if (load_order[index] != nullptr && load_order[index]->key == key.owner()) return index;
    }
    return std::nullopt;
  }

  bool held_after(const FormKey& key) const {
    for (std::size_t index = plugin_index + 1; index < load_order.size(); ++index) {
      if (load_order[index] != nullptr && find_record(*load_order[index], key) != nullptr) return true;
    }
    return false;
  }

  bool same_version(const Record& version, std::size_t index, const Record& master, std::size_t master_index) {
    return placement_in(index, version.form_key) == placement_in(master_index, version.form_key) &&
           same_record(version, master, exact);
  }

  const Plugin* identical_previous(const Record& record) {
    if (record.undecodable || record.form_key.owner().empty() || record.form_key.owner() == plugin.key) {
      return nullptr;
    }
    const auto owner = owner_index(record.form_key);
    if (!owner) return nullptr;
    const auto* master = find_record(*load_order[*owner], record.form_key);
    if (master == nullptr || master->undecodable) return nullptr;
    const auto master_here = remapped_into(*master, *load_order[*owner], context);
    if (!master_here || !same_version(record, plugin_index, *master_here, *owner)) return nullptr;
    if (held_after(record.form_key)) return load_order[*owner];
    for (std::size_t index = *owner + 1; index < plugin_index; ++index) {
      if (load_order[index] == nullptr) continue;
      const auto* holder = find_record(*load_order[index], record.form_key);
      if (holder == nullptr) continue;
      if (holder->undecodable) return nullptr;
      const auto holder_here = remapped_into(*holder, *load_order[index], context);
      if (!holder_here || !same_version(*holder_here, index, *master_here, *owner)) return nullptr;
    }
    return load_order[*owner];
  }

  bool scan(const Group& group) {
    bool cleared = true;
    for (std::size_t i = 0; i < group.children.size(); ++i) {
      if (const auto* sub = std::get_if<Group>(&group.children[i])) {
        cleared = scan(*sub) && cleared;
        continue;
      }
      const auto& record = std::get<Record>(group.children[i]);
      const auto* child = owned_child_group(group, i);
      const bool children_cleared = child == nullptr || scan(*child);
      if (child != nullptr) ++i;
      const auto* previous = children_cleared ? identical_previous(record) : nullptr;
      if (previous == nullptr) {
        cleared = false;
        continue;
      }
      out.push_back({record.form_key, record.header.signature, previous->key});
    }
    return cleared;
  }
};

std::size_t remove_from(Group& group, const std::set<FormKey>& keys) {
  std::size_t removed = 0;
  std::vector<GroupChild> kept;
  kept.reserve(group.children.size());
  for (std::size_t i = 0; i < group.children.size(); ++i) {
    auto& child = group.children[i];
    if (auto* sub = std::get_if<Group>(&child)) {
      const auto count = remove_from(*sub, keys);
      removed += count;
      if (count != 0 && sub->children.empty()) continue;
    } else if (keys.contains(std::get<Record>(child).form_key)) {
      ++removed;
      const auto* owned = record_child_group(group, i);
      if (owned != nullptr && owned->children.empty()) ++i;
      continue;
    }
    kept.push_back(std::move(child));
  }
  group.children = std::move(kept);
  return removed;
}

}  // namespace

std::vector<IdenticalToMaster> find_identical_to_master(std::span<const Plugin* const> load_order,
                                                        std::size_t plugin_index,
                                                        std::size_t limit,
                                                        bool exact_only) {
  if (plugin_index >= load_order.size() || load_order[plugin_index] == nullptr) return {};
  const auto& plugin = *load_order[plugin_index];
  ItmScan scan{load_order, plugin_index, plugin, plugin_form_id_context(plugin), {}, {}, exact_only};
  for (const auto& group : plugin.groups) scan.scan(group);
  if (limit != 0 && scan.out.size() > limit) scan.out.resize(limit);
  return std::move(scan.out);
}

std::size_t remove_records(Plugin& plugin, std::span<const FormKey> keys) {
  const std::set<FormKey> wanted(keys.begin(), keys.end());
  visit_records(plugin, [&](const Record& record) {
    if (wanted.contains(record.form_key)) require_decodable(record);
  });
  std::size_t removed = 0;
  std::vector<Group> kept;
  for (auto& group : plugin.groups) {
    const auto count = remove_from(group, wanted);
    removed += count;
    if (count != 0 && group.children.empty()) continue;
    kept.push_back(std::move(group));
  }
  plugin.groups = std::move(kept);
  if (removed != 0) {
    plugin.original_range.clear();
    plugin.original_structure_fingerprint = 0;
  }
  return removed;
}

DeletedRecordCounts count_deleted_records(const Plugin& plugin) {
  DeletedRecordCounts counts;
  visit_records(plugin, [&](const Record& record) {
    if (record.header.signature == sig_tes4 || !record.header.flags.is_deleted()) return;
    ++counts.records;
    if (is_placed_reference_signature(record.header.signature)) ++counts.references;
    if (record.header.signature == sig_navm) ++counts.navmeshes;
  });
  return counts;
}

EslEligibility esl_eligibility(const Plugin& plugin) {
  EslEligibility out;
  const auto first = allows_hardcoded_range(plugin) ? k_first_light_local_id : k_first_full_local_id;
  if (first == k_first_full_local_id) out.capacity = k_light_local_mask + 1 - k_first_full_local_id;
  for (const auto* record : collect_records(plugin)) {
    if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) continue;
    ++out.native_records;
    const auto local = record->form_key.local().value();
    if (local < first || local > k_light_local_mask) ++out.out_of_range;
  }
  return out;
}

EslSlotRange esl_slot_range(const Plugin& plugin) {
  const auto first =
      allows_hardcoded_range(plugin) ? k_first_light_local_id : k_first_full_local_id;
  return EslSlotRange{first, k_light_local_mask,
                      static_cast<std::size_t>(k_light_local_mask) + 1u - first};
}

EslCompactionResult compact_to_esl(Plugin& plugin) {
  EslCompactionResult result;
  require_plugin_decodable(plugin, "compact a plugin into the light range");
  assign_record_form_keys(plugin);

  const auto range = esl_slot_range(plugin);
  std::set<std::uint32_t> occupied;
  std::set<FormKey> top_level;
  for (const auto& group : plugin.groups) {
    if (group.header.group_type != static_cast<std::int32_t>(GroupType::top)) continue;
    for (const auto& child : group.children) {
      if (const auto* record = std::get_if<Record>(&child)) top_level.insert(record->form_key);
    }
  }

  struct Move {
    FormKey key;
    Signature signature{};
  };
  std::vector<Move> moves;
  std::size_t native_records = 0;
  for (const auto* record : collect_records(plugin)) {
    if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) continue;
    ++native_records;
    const auto local = record->form_key.local().value();
    if (local >= range.first && local <= range.last) {
      occupied.insert(local);
      continue;
    }
    if (!top_level.contains(record->form_key)) {
      result.blockers.push_back({record->form_key, record->header.signature, true});
      continue;
    }
    moves.push_back({record->form_key, record->header.signature});
  }

  if (!result.blockers.empty()) {
    result.status = EslCompactionStatus::blocked_nested_records;
    return result;
  }

  if (moves.empty()) {
    const auto lowered = lowered_light_next_form_id(plugin);
    if (lowered) {
      auto next = plugin;
      set_hedr_next_form_id(next, *lowered);
      plugin = std::move(next);
      result.next_form_id = *lowered;
    } else {
      result.next_form_id = plugin.header.next_form_id;
    }
    result.status = EslCompactionStatus::already_compatible;
    return result;
  }

  if (native_records > range.capacity) {
    result.status = EslCompactionStatus::capacity_exceeded;
    return result;
  }

  std::sort(moves.begin(), moves.end(), [](const Move& left, const Move& right) {
    return left.key.local().value() < right.key.local().value();
  });

  auto next = plugin;
  const auto original_flags = next.header.tes4_flags;
  next.header.tes4_flags = RecordFlags{original_flags.value() | RecordFlags::small_file};
  next.tes4.header.flags = next.header.tes4_flags;

  std::uint32_t candidate = range.first;
  std::size_t remapped = 0;
  for (const auto& move : moves) {
    while (candidate <= range.last && occupied.contains(candidate)) ++candidate;
    if (candidate > range.last) {
      result.status = EslCompactionStatus::capacity_exceeded;
      return result;
    }
    if (renumber_local_record(next, move.key, LocalFormId{candidate}) !=
        RecordRenumberStatus::renumbered) {
      result.blockers.push_back({move.key, move.signature, false});
      result.status = EslCompactionStatus::rekey_failed;
      return result;
    }
    occupied.insert(candidate);
    ++candidate;
    ++remapped;
  }
  while (candidate <= range.last && occupied.contains(candidate)) ++candidate;

  next.header.tes4_flags = original_flags;
  next.tes4.header.flags = original_flags;
  set_hedr_next_form_id(next, candidate);
  plugin = std::move(next);
  result.status = EslCompactionStatus::compacted;
  result.remapped = remapped;
  result.next_form_id = candidate;
  return result;
}

}  // namespace fo4
