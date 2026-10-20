#include "fo4/patch/list_merge.hpp"

#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::patch {
namespace {

constexpr auto k_lvli = Signature::from_string("LVLI");
constexpr auto k_lvln = Signature::from_string("LVLN");
constexpr auto k_lvsp = Signature::from_string("LVSP");
constexpr auto k_flst = Signature::from_string("FLST");
constexpr std::string_view k_form_list_path = "FLST\\Items";

const Plugin* master_file_holder(const Plugin& plugin,
                                 std::span<const Plugin* const> versions,
                                 std::size_t index) {
  for (std::size_t back = index; back > 0; --back) {
    const auto& candidate = versions[back - 1]->key;
    for (const auto& master : plugin.header.masters) {
      if (master.key == candidate) return versions[back - 1];
    }
  }
  return nullptr;
}

bool same_leveled_item(const forms::LeveledListEntry& left,
                       const forms::LeveledListEntry& right) {
  return left.level == right.level && left.item == right.item;
}

std::vector<forms::LeveledListEntry> entries_of_item(
    std::span<const forms::LeveledListEntry> entries,
    const forms::LeveledListEntry& item) {
  std::vector<forms::LeveledListEntry> found;
  for (const auto& entry : entries) {
    if (same_leveled_item(entry, item)) found.push_back(entry);
  }
  return found;
}

std::vector<forms::LeveledListEntry> merge_leveled_entries(
    std::vector<forms::LeveledListEntry> target,
    std::span<const forms::LeveledListEntry> previous,
    std::span<const forms::LeveledListEntry> current) {
  const auto merge_item = [&](const forms::LeveledListEntry& item) {
    const auto before = entries_of_item(previous, item);
    const auto after = entries_of_item(current, item);
    if (std::is_permutation(before.begin(), before.end(), after.begin(), after.end())) return;
    std::vector<forms::LeveledListEntry> merged;
    bool placed = false;
    for (const auto& known : target) {
      if (!same_leveled_item(known, item)) {
        merged.push_back(known);
      } else if (!placed) {
        merged.insert(merged.end(), after.begin(), after.end());
        placed = true;
      }
    }
    if (!placed) merged.insert(merged.end(), after.begin(), after.end());
    target = std::move(merged);
  };
  for (const auto& entry : previous) merge_item(entry);
  for (const auto& entry : current) merge_item(entry);
  return target;
}

bool same_leveled_multiset(std::span<const forms::LeveledListEntry> left,
                           std::span<const forms::LeveledListEntry> right) {
  return std::is_permutation(left.begin(), left.end(), right.begin(), right.end());
}

bool holds_key(std::span<const FormKey> keys, const FormKey& wanted) {
  return std::find(keys.begin(), keys.end(), wanted) != keys.end();
}

std::vector<FormKey> merge_form_list(std::vector<FormKey> target,
                                     std::span<const FormKey> previous,
                                     std::span<const FormKey> current) {
  for (const auto& key : previous) {
    if (holds_key(current, key)) continue;
    std::erase(target, key);
  }
  for (const auto& key : current) {
    if (!holds_key(previous, key) && !holds_key(target, key)) target.push_back(key);
  }
  return target;
}

PluginKind owner_kind(const env::Environment& env, const PluginKey& owner,
                      std::span<const Plugin* const> sources) {
  const auto index = env.find_plugin(owner.filename());
  if (index.has_value()) return env.plugins()[*index].kind;
  for (const auto* source : sources) {
    for (const auto& master : source->header.masters) {
      if (master.key == owner) return master.kind;
    }
  }
  throw ListMergeError("list merge cannot type the owner " + std::string(owner.filename()) +
                       ": it is not loaded and no merged version carries it as a master");
}

FormLinkTypeLookup environment_type_lookup(const env::Environment& env) {
  return [&env](const FormKey& key) -> std::optional<Signature> {
    const auto* winner = env.winner(key);
    if (winner == nullptr || winner->record == nullptr) return std::nullopt;
    return winner->record->header.signature;
  };
}

bool has_master(const Plugin& plugin, const PluginKey& owner) {
  for (const auto& master : plugin.header.masters) {
    if (master.key == owner) return true;
  }
  return false;
}

std::set<PluginKey> entry_owners(std::span<const forms::LeveledListEntry> entries) {
  std::set<PluginKey> owners;
  for (const auto& entry : entries) {
    if (!entry.item.owner().empty()) owners.insert(entry.item.owner());
    if (entry.extra.has_value() && !entry.extra->owner.owner().empty()) {
      owners.insert(entry.extra->owner.owner());
    }
  }
  return owners;
}

std::set<PluginKey> entry_owners(std::span<const FormKey> keys) {
  std::set<PluginKey> owners;
  for (const auto& key : keys) {
    if (!key.owner().empty()) owners.insert(key.owner());
  }
  return owners;
}

std::vector<PluginKey> master_load_order(const env::Environment& env, const Plugin& patch) {
  std::vector<PluginKey> order;
  order.reserve(env.plugins().size() + patch.header.masters.size());
  for (const auto& info : env.plugins()) order.push_back(info.key);
  for (const auto& master : patch.header.masters) {
    if (std::find(order.begin(), order.end(), master.key) == order.end()) {
      order.push_back(master.key);
    }
  }
  return order;
}

void add_entry_masters(Plugin& patch,
                       const env::Environment& env,
                       std::span<const Plugin* const> sources,
                       const std::set<PluginKey>& owners,
                       const FormLinkTypeLookup& type_lookup) {
  std::vector<MasterDescriptor> masters;
  masters.reserve(patch.header.masters.size() + owners.size());
  for (const auto& master : patch.header.masters) masters.push_back({master.key, master.kind});

  bool added = false;
  for (const auto& owner : owners) {
    if (owner == patch.key || has_master(patch, owner)) continue;
    masters.push_back({owner, owner_kind(env, owner, sources)});
    added = true;
  }
  if (!added) return;

  patch = remap_plugin_masters(std::move(patch), std::move(masters), type_lookup);
  const auto order = master_load_order(env, patch);
  patch = sort_plugin_masters(std::move(patch), order, type_lookup);
}

std::vector<forms::LeveledListEntry> leveled_entries(const Plugin& plugin,
                                                     const FormKey& key) {
  return forms::decode_leveled_list(plugin, key).entries;
}

std::vector<FormKey> form_list_entries(const Plugin& plugin,
                                       const FormKey& key,
                                       const schema::FormLinkListDescriptor& descriptor) {
  auto values = schema::record_formlink_list_values(plugin, key, descriptor);
  if (!values.has_value()) {
    throw ListMergeError("form list record not found: " + key.to_string());
  }
  return std::move(*values);
}

void write_form_list(Plugin& patch,
                     const FormKey& key,
                     const schema::FormLinkListDescriptor& descriptor,
                     std::span<const FormKey> values) {
  const auto cleared = schema::clear_record_formlink_list(patch, key, descriptor);
  if (cleared != schema::FormLinkListMutationStatus::updated &&
      cleared != schema::FormLinkListMutationStatus::unchanged) {
    throw ListMergeError("form list cannot be rewritten: " + key.to_string());
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto status =
        schema::insert_record_formlink_list_element(patch, key, descriptor, index, values[index]);
    if (status != schema::FormLinkListMutationStatus::updated) {
      throw ListMergeError("form list element cannot be inserted: " + key.to_string());
    }
  }
}

}  // namespace

ListMergeResult build_list_merge_patch(const env::Environment& env,
                                       Signature signature,
                                       std::string_view patch_name) {
  const bool leveled = signature != k_flst;
  if (leveled && signature != k_lvli && signature != k_lvln && signature != k_lvsp) {
    throw ListMergeError("list merging covers LVLI, LVLN, LVSP and FLST, not " +
                         signature.to_string());
  }

  std::map<FormKey, std::vector<const Plugin*>> versions;
  for (std::size_t index = 0; index < env.plugins().size(); ++index) {
    const auto& plugin = env.plugin(index);
    for (const auto* record : collect_records(plugin)) {
      if (record->header.signature != signature || record->undecodable) continue;
      versions[record->form_key].push_back(&plugin);
    }
  }

  ListMergeResult result;
  result.patch = make_empty_plugin(patch_name);
  const auto type_lookup = environment_type_lookup(env);

  for (const auto& [key, list] : versions) {
    if (list.size() < 2) continue;
    const auto& winner = *list.back();

    std::vector<FormKey> merged_keys;
    std::vector<forms::LeveledListEntry> merged_entries;
    bool unchanged = false;

    if (leveled) {
      merged_entries = leveled_entries(*list.front(), key);
      for (std::size_t index = 1; index < list.size(); ++index) {
        const auto* holder = master_file_holder(*list[index], list, index);
        if (holder == nullptr) continue;
        const auto previous = leveled_entries(*holder, key);
        const auto current = leveled_entries(*list[index], key);
        merged_entries = merge_leveled_entries(std::move(merged_entries), previous, current);
      }
      unchanged = same_leveled_multiset(merged_entries, leveled_entries(winner, key));
    } else {
      const auto descriptor = schema::formlink_list_descriptor(signature, k_form_list_path);
      if (!descriptor.has_value()) {
        throw ListMergeError("no FormLink list row for " + std::string(k_form_list_path));
      }
      merged_keys = form_list_entries(*list.front(), key, *descriptor);
      for (std::size_t index = 1; index < list.size(); ++index) {
        const auto* holder = master_file_holder(*list[index], list, index);
        if (holder == nullptr) continue;
        const auto previous = form_list_entries(*holder, key, *descriptor);
        const auto current = form_list_entries(*list[index], key, *descriptor);
        merged_keys = merge_form_list(std::move(merged_keys), previous, current);
      }
      const auto winner_keys = form_list_entries(winner, key, *descriptor);
      unchanged = std::is_permutation(merged_keys.begin(), merged_keys.end(),
                                      winner_keys.begin(), winner_keys.end());
    }

    if (unchanged) {
      ++result.counts.unchanged;
      continue;
    }

    const auto copied = copy_record_as_override(result.patch, winner, key, false, type_lookup);
    if (copied != RecordCopyStatus::inserted && copied != RecordCopyStatus::replaced) {
      throw ListMergeError("the winning record cannot be copied into the patch: " +
                           key.to_string());
    }

    if (leveled) {
      add_entry_masters(result.patch, env, list, entry_owners(merged_entries), type_lookup);
      if (!forms::replace_leveled_list_entries(result.patch, key, merged_entries)) {
        throw ListMergeError("leveled list cannot be rewritten: " + key.to_string());
      }
    } else {
      const auto descriptor = schema::formlink_list_descriptor(signature, k_form_list_path);
      if (!descriptor.has_value()) {
        throw ListMergeError("no FormLink list row for " + std::string(k_form_list_path));
      }
      add_entry_masters(result.patch, env, list, entry_owners(merged_keys), type_lookup);
      write_form_list(result.patch, key, *descriptor, merged_keys);
    }
    ++result.counts.merged;
  }

  return result;
}

}  // namespace fo4::patch
