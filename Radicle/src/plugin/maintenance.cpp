#include "fo4/plugin/maintenance.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/placement.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_links.hpp"

#include <exception>
#include <map>
#include <set>
#include <utility>
#include <variant>

namespace fo4 {
namespace {

constexpr Signature k_sig_navm = Signature::from_string("NAVM");

bool same_record(const Record& left, const Record& right) {
  if (left.header.signature != right.header.signature || left.header.flags != right.header.flags ||
      left.subrecords.size() != right.subrecords.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.subrecords.size(); ++i) {
    if (left.subrecords[i].signature != right.subrecords[i].signature ||
        left.subrecords[i].data != right.subrecords[i].data) {
      return false;
    }
  }
  return true;
}

bool previous_normalizes_equal(const Record& current,
                               const Record& previous,
                               const Plugin& previous_plugin,
                               const PluginFormIdContext& previous_context,
                               const PluginFormIdContext& current_context) {
  auto normalized = previous;
  try {
    remap_record_form_links(normalized, previous_context, current_context, &previous_plugin);
  } catch (const std::exception&) {
    return false;
  }
  return same_record(current, normalized);
}

bool is_owned_child_group_type(std::int32_t type) {
  return type == static_cast<std::int32_t>(GroupType::world_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_children) ||
         type == static_cast<std::int32_t>(GroupType::topic_children);
}

const Group* owned_child_group(const Group& parent, std::size_t record_index) {
  if (record_index + 1 >= parent.children.size()) return nullptr;
  const auto* next = std::get_if<Group>(&parent.children[record_index + 1]);
  return next != nullptr && is_owned_child_group_type(next->header.group_type) ? next : nullptr;
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

  const Plugin* identical_previous(const Record& record) {
    if (record.form_key.owner().empty() || record.form_key.owner() == plugin.key) return nullptr;
    for (std::size_t index = plugin_index; index-- > 0;) {
      const auto* earlier = load_order[index];
      if (earlier == nullptr) continue;
      const auto* previous = find_record(*earlier, record.form_key);
      if (previous == nullptr) continue;
      if (placement_in(index, record.form_key) != placement_in(plugin_index, record.form_key)) return nullptr;
      return previous_normalizes_equal(record, *previous, *earlier, plugin_form_id_context(*earlier),
                                       context)
                 ? earlier
                 : nullptr;
    }
    return nullptr;
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
  for (auto& child : group.children) {
    if (auto* sub = std::get_if<Group>(&child)) {
      const auto count = remove_from(*sub, keys);
      removed += count;
      if (count != 0 && sub->children.empty()) continue;
    } else if (keys.contains(std::get<Record>(child).form_key)) {
      ++removed;
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

}  // namespace fo4
