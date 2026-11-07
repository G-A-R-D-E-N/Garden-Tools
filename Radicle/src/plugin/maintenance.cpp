#include "fo4/plugin/maintenance.hpp"

#include "child_groups.hpp"
#include "record_edit.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/placement.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_links.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fo4 {
namespace {

using detail::require_plugin_decodable;

constexpr Signature k_sig_navm = Signature::from_string("NAVM");

struct ItmIgnoredSubrecord {
  std::string_view record;
  std::string_view subrecord;
};

constexpr ItmIgnoredSubrecord k_itm_ignored_subrecords[] = {
    {"TACT", "PNAM"}, {"TACT", "FNAM"}, {"CSTY", "CSMD"}, {"DIAL", "TIFC"}, {"PROJ", "DATA"}, {"PERK", "PRKF"},
    {"MESG", "INAM"}, {"MATO", "DNAM"}, {"REFR", "XMBP"}, {"WRLD", "NNAM"},
};

bool is_itm_ignored(const Record& record, const Subrecord& subrecord) {
  const auto record_signature = record.header.signature.view();
  const auto subrecord_signature = subrecord.signature.view();
  for (const auto& ignored : k_itm_ignored_subrecords) {
    if (ignored.record == record_signature && ignored.subrecord == subrecord_signature) return true;
  }
  return false;
}

std::vector<const Subrecord*> comparable_subrecords(const Record& record) {
  std::vector<const Subrecord*> out;
  for (const auto& subrecord : record.subrecords) {
    if (!is_itm_ignored(record, subrecord)) out.push_back(&subrecord);
  }
  return out;
}

bool same_record(const Record& left, const Record& right) {
  if (left.header.signature != right.header.signature || left.header.flags != right.header.flags) return false;
  const auto left_subrecords = comparable_subrecords(left);
  const auto right_subrecords = comparable_subrecords(right);
  if (left_subrecords.size() != right_subrecords.size()) return false;
  for (std::size_t i = 0; i < left_subrecords.size(); ++i) {
    if (left_subrecords[i]->signature != right_subrecords[i]->signature ||
        left_subrecords[i]->data != right_subrecords[i]->data) {
      return false;
    }
  }
  return true;
}

std::optional<Record> remapped_into(const Record& record,
                                    const Plugin& source,
                                    const PluginFormIdContext& target) {
  auto normalized = record;
  try {
    remap_record_form_links(normalized, plugin_form_id_context(source), target,
                            plugin_type_lookup(source));
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return normalized;
}

struct ItmScan {
  std::span<const Plugin* const> load_order;
  std::size_t plugin_index;
  const Plugin& plugin;
  PluginFormIdContext context;
  std::vector<IdenticalToMaster> out;
  std::map<std::size_t, std::map<FormKey, RecordPlacement>> placements{};

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
           same_record(version, master);
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
                                                        std::size_t limit) {
  if (plugin_index >= load_order.size() || load_order[plugin_index] == nullptr) return {};
  const auto& plugin = *load_order[plugin_index];
  ItmScan scan{load_order, plugin_index, plugin, plugin_form_id_context(plugin), {}, {}};
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
    if (record.header.signature == k_sig_navm) ++counts.navmeshes;
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
