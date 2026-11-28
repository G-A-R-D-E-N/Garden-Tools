#include "fo4/plugin/itpo.hpp"

#include "child_groups.hpp"
#include "identical_record.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/placement.hpp"
#include "fo4/plugin/reader.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

using identical::remapped_into;
using identical::same_record;

struct ItpoScan {
  std::span<const Plugin* const> load_order;
  std::size_t plugin_index;
  const Plugin& plugin;
  PluginFormIdContext context;
  std::vector<IdenticalToPreviousOverride> out;
  std::map<std::size_t, std::map<FormKey, RecordPlacement>> placements{};
  std::map<std::size_t, RecordIndex> indexes{};
  bool exact{false};

  const RecordPlacement& placement_in(std::size_t index, const FormKey& key) {
    auto [it, inserted] = placements.try_emplace(index);
    if (inserted) it->second = record_placements(*load_order[index]);
    return it->second[key];
  }

  const RecordIndex& index_in(std::size_t index) {
    return indexes.try_emplace(index, *load_order[index]).first->second;
  }

  std::optional<std::size_t> owner_index(const FormKey& key) const {
    for (std::size_t index = 0; index < plugin_index; ++index) {
      if (load_order[index] != nullptr && load_order[index]->key == key.owner()) return index;
    }
    return std::nullopt;
  }

  std::optional<std::size_t> previous_holder(const FormKey& key, std::size_t owner) {
    for (std::size_t index = plugin_index; index > owner + 1; --index) {
      const auto* holder = load_order[index - 1];
      if (holder == nullptr || index_in(index - 1).find(key) == nullptr) continue;
      const bool is_master = std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                                         [&](const MasterReference& master) { return master.key == holder->key; });
      return is_master ? std::optional<std::size_t>{index - 1} : std::nullopt;
    }
    return std::nullopt;
  }

  bool same_version(const Record& version, std::size_t previous_index, const Record& previous) {
    return placement_in(plugin_index, version.form_key) == placement_in(previous_index, version.form_key) &&
           same_record(version, previous, exact);
  }

  const Plugin* identical_previous(const Record& record) {
    if (record.undecodable || record.form_key.owner().empty() || record.form_key.owner() == plugin.key) {
      return nullptr;
    }
    const auto owner = owner_index(record.form_key);
    if (!owner) return nullptr;
    const auto holder = previous_holder(record.form_key, *owner);
    if (!holder) return nullptr;
    const auto* previous = index_in(*holder).find(record.form_key);
    if (previous == nullptr || previous->undecodable) return nullptr;
    const auto previous_here = remapped_into(*previous, *load_order[*holder], context);
    if (!previous_here || !same_version(record, *holder, *previous_here)) return nullptr;
    return load_order[*holder];
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

}

std::vector<IdenticalToPreviousOverride> find_identical_to_previous_override(
    std::span<const Plugin* const> load_order,
    std::size_t plugin_index,
    std::size_t limit,
    bool exact_only) {
  if (plugin_index >= load_order.size() || load_order[plugin_index] == nullptr) return {};
  const auto& plugin = *load_order[plugin_index];
  ItpoScan scan{load_order, plugin_index, plugin, plugin_form_id_context(plugin), {}, {}, {}, exact_only};
  for (const auto& group : plugin.groups) scan.scan(group);
  if (limit != 0 && scan.out.size() > limit) scan.out.resize(limit);
  return std::move(scan.out);
}

std::size_t remove_identical_to_previous_override(Plugin& plugin,
                                                  std::span<const Plugin* const> load_order,
                                                  std::size_t plugin_index) {
  if (plugin_index >= load_order.size()) return 0;
  std::vector<const Plugin*> order(load_order.begin(), load_order.end());
  order[plugin_index] = &plugin;
  std::vector<FormKey> keys;
  for (const auto& item : find_identical_to_previous_override(order, plugin_index, 0, true)) {
    keys.push_back(item.key);
  }
  return remove_records(plugin, keys);
}

}
