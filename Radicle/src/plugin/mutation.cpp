#include "fo4/plugin/mutation.hpp"

#include "child_groups.hpp"
#include "copy_hierarchy.hpp"
#include "record_edit.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

std::vector<std::byte> zstring(std::string_view value) {
  std::vector<std::byte> bytes;
  bytes.reserve(value.size() + 1);
  for (const char ch : value) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

PluginKind owner_kind(const Plugin& source, const PluginKey& owner) {
  if (owner == source.key) {
    return classify_plugin_kind(source.header.tes4_flags, source.key);
  }
  for (const auto& master : source.header.masters) {
    if (master.key == owner) return master.kind;
  }
  return owner.looks_light() ? PluginKind::light : PluginKind::full;
}

bool has_master(const Plugin& plugin, const PluginKey& owner) {
  for (const auto& master : plugin.header.masters) {
    if (master.key == owner) return true;
  }
  return false;
}

GroupHeader new_top_group_header(Signature signature) {
  GroupHeader header;
  const auto label = signature.bytes();
  for (std::size_t i = 0; i < label.size(); ++i) {
    header.label[i] = std::byte{static_cast<std::uint8_t>(label[i])};
  }
  header.group_type = 0;
  return header;
}

const Group* source_top_group_for_record(const Plugin& source,
                                         const Record* source_record) {
  for (const auto& group : source.groups) {
    if (group.header.group_type != 0) continue;
    for (const auto& child : group.children) {
      const auto* record = std::get_if<Record>(&child);
      if (record == source_record) return &group;
    }
  }
  return nullptr;
}

using detail::find_mutable_record;
using detail::require_plugin_decodable;

Record* find_top_level_record(Plugin& plugin, const FormKey& record_key) {
  for (auto& group : plugin.groups) {
    if (group.header.group_type != 0) continue;
    for (auto& child : group.children) {
      auto* record = std::get_if<Record>(&child);
      if (record != nullptr && record->form_key == record_key) {
        require_decodable(*record);
        return record;
      }
    }
  }
  return nullptr;
}

bool erase_record(Group& group, const FormKey& record_key, std::set<FormKey>& removed) {
  for (std::size_t i = 0; i < group.children.size(); ++i) {
    if (auto* sub = std::get_if<Group>(&group.children[i])) {
      if (!erase_record(*sub, record_key, removed)) continue;
      if (sub->children.empty()) {
        group.children.erase(group.children.begin() + static_cast<std::ptrdiff_t>(i));
      }
      return true;
    }
    const auto& record = std::get<Record>(group.children[i]);
    if (record.form_key != record_key) continue;
    removed.insert(record.form_key);
    auto stop = i + 1;
    if (const auto* child = record_child_group(group, i); child != nullptr) {
      visit_group_records(*child, [&](const Record& inner) { removed.insert(inner.form_key); });
      stop = i + 2;
    }
    group.children.erase(group.children.begin() + static_cast<std::ptrdiff_t>(i),
                         group.children.begin() + static_cast<std::ptrdiff_t>(stop));
    return true;
  }
  return false;
}

std::set<FormKey> erase_subtree(Plugin& plugin, const FormKey& record_key) {
  std::set<FormKey> removed;
  for (auto it = plugin.groups.begin(); it != plugin.groups.end(); ++it) {
    if (!erase_record(*it, record_key, removed)) continue;
    if (it->children.empty()) plugin.groups.erase(it);
    break;
  }
  return removed;
}

}  // namespace

Group* find_or_create_top_group(Plugin& plugin, Signature signature) {
  for (auto& group : plugin.groups) {
    if (group.header.group_type == 0 && group.header.top_label() == signature) {
      return &group;
    }
  }

  Group group;
  group.header = new_top_group_header(signature);
  plugin.groups.push_back(std::move(group));
  return &plugin.groups.back();
}

void set_hedr_next_form_id(Plugin& plugin, std::uint32_t next_form_id) {
  Subrecord* hedr = nullptr;
  for (auto& subrecord : plugin.tes4.subrecords) {
    if (subrecord.signature != sig_hedr) continue;
    if (hedr != nullptr) {
      throw FormIdError("TES4 contains duplicate HEDR subrecords");
    }
    hedr = &subrecord;
  }
  if (hedr == nullptr || hedr->data.size() < 12) {
    throw FormIdError("TES4 HEDR must be at least 12 bytes to update next form ID");
  }

  std::memcpy(hedr->data.data() + 8, &next_form_id, sizeof(next_form_id));
  plugin.header.next_form_id = next_form_id;
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
  plugin.tes4.decompressed_payload.clear();
  plugin.tes4.header.data_size = 0;
  plugin.tes4.uncompressed_size = 0;
}

bool allows_hardcoded_range(const Plugin& plugin) {
  return plugin.header.version >= 1.0f && !plugin.header.masters.empty();
}

std::pair<std::uint32_t, std::uint32_t> local_form_id_range(const Plugin& plugin) {
  if (classify_plugin_kind(plugin.header.tes4_flags, plugin.key) == PluginKind::light) {
    return {allows_hardcoded_range(plugin) ? k_first_light_local_id : k_first_full_local_id, k_light_local_mask};
  }
  return {k_first_full_local_id, k_full_local_mask};
}

std::optional<std::uint32_t> lowered_light_next_form_id(const Plugin& plugin) {
  const auto lowest = allows_hardcoded_range(plugin) ? k_first_light_local_id : k_first_full_local_id;
  auto lowered = lowest;
  for (const auto* record : collect_records(plugin)) {
    if (record->header.signature == sig_tes4 || record->form_key.owner() != plugin.key) continue;
    const auto local = record->form_key.local().value();
    if (local < lowest || local > k_light_local_mask) return std::nullopt;
    lowered = std::max(lowered, local + 1u);
  }
  const auto next = plugin.header.next_form_id & k_full_local_mask;
  if (next <= k_light_local_mask || next <= lowered) return std::nullopt;
  return lowered;
}

FormKey reserve_local_form_key(Plugin& plugin) {
  if (plugin.key.empty()) {
    throw FormIdError("plugin has no identity for local FormID reservation");
  }

  assign_record_form_keys(plugin);

  const auto [lowest, highest] = local_form_id_range(plugin);
  // A light plugin's HEDR nextFormID may legitimately sit below the full-plugin
  // floor, so clamp up to this plugin's own floor rather than rejecting it.
  auto candidate = std::max(plugin.header.next_form_id & k_full_local_mask, lowest);
  if (candidate > highest + 1u) {
    throw FormIdError("HEDR nextFormID is outside this plugin's local FormID range");
  }

  while (candidate <= highest) {
    const FormKey probe{plugin.key, LocalFormId{candidate}};
    if (find_record(plugin, probe) == nullptr) break;
    ++candidate;
  }
  if (candidate > highest) {
    throw FormIdError("no local FormID remains at or above HEDR nextFormID");
  }

  const FormKey key{plugin.key, LocalFormId{candidate}};
  set_hedr_next_form_id(plugin, candidate + 1u);
  return key;
}

FormKey create_local_record(Plugin& plugin, Signature signature) {
  return create_local_record(plugin, signature, {});
}

FormKey create_local_record(Plugin& plugin,
                            Signature signature,
                            std::vector<Subrecord> subrecords) {
  if (signature.packed() == 0 || signature == sig_tes4 || signature == sig_grup) {
    throw FormIdError("invalid major-record signature for local record creation");
  }

  const auto key = reserve_local_form_key(plugin);

  Record record;
  record.header.signature = signature;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(plugin));
  record.header.form_version = plugin.header.form_version;
  record.form_key = key;
  record.subrecords = std::move(subrecords);

  auto* group = find_or_create_top_group(plugin, signature);
  group->children.emplace_back(std::move(record));

  plugin.original_range.clear();
  plugin.original_structure_fingerprint = 0;
  return key;
}

RecordDeleteStatus delete_local_record(Plugin& plugin, const FormKey& record_key) {
  if (record_key.owner() != plugin.key) return RecordDeleteStatus::not_local;
  require_plugin_decodable(plugin, "delete");

  auto next = plugin;
  assign_record_form_keys(next);
  if (find_record(next, record_key) == nullptr) return RecordDeleteStatus::record_missing;

  // A same-key rekey is a strict no-op scan. It proves every supported mixed
  // FormLink surface is parseable before the tolerant collector is trusted.
  auto coverage_probe = next;
  rekey_form_links(coverage_probe, record_key, record_key);
  const auto links = collect_form_links(coverage_probe);

  const auto removed = erase_subtree(next, record_key);
  if (removed.empty()) return RecordDeleteStatus::record_missing;

  if (std::any_of(links.begin(), links.end(), [&](const auto& link) {
        return link.key == record_key ||
               (removed.contains(link.key) && !removed.contains(link.record));
      })) {
    return RecordDeleteStatus::referenced;
  }

  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
  return RecordDeleteStatus::deleted;
}

std::vector<FormKey> records_removed_by_delete(const Plugin& plugin, const FormKey& record_key) {
  if (record_key.owner() != plugin.key) return {};
  auto next = plugin;
  assign_record_form_keys(next);
  const auto removed = erase_subtree(next, record_key);
  return std::vector<FormKey>(removed.begin(), removed.end());
}

bool has_form_id_labelled_child_group(const Plugin& plugin, const FormKey& record_key) {
  const auto context = plugin_form_id_context(plugin);
  bool found = false;
  visit_groups(plugin, [&](const Group& group) {
    if (found || !group_label_is_form_id(group.header.group_type)) return;
    const RawFormId raw{group.header.label_as_u32()};
    if (raw.is_null()) return;
    found = resolve_plugin_form_id(raw, context) == record_key;
  });
  return found;
}

namespace {

void relabel_form_id_child_groups(Group& group,
                                  const PluginFormIdContext& context,
                                  const FormKey& from,
                                  const RawFormId to) {
  for (auto& child : group.children) {
    auto* nested = std::get_if<Group>(&child);
    if (nested == nullptr) continue;
    if (group_label_is_form_id(nested->header.group_type)) {
      const RawFormId raw{nested->header.label_as_u32()};
      if (!raw.is_null() && resolve_plugin_form_id(raw, context) == from) {
        nested->header.set_label_as_u32(to.value());
      }
    }
    relabel_form_id_child_groups(*nested, context, from, to);
  }
}

void relabel_record_child_groups(Plugin& plugin, const FormKey& from, const FormKey& to) {
  const auto context = plugin_form_id_context(plugin);
  const auto encoded = encode_plugin_form_id(to, context);
  for (auto& group : plugin.groups) {
    relabel_form_id_child_groups(group, context, from, encoded);
  }
}

}  // namespace

RecordRenumberStatus renumber_local_record(Plugin& plugin,
                                           const FormKey& record_key,
                                           LocalFormId new_local) {
  if (record_key.owner() != plugin.key) return RecordRenumberStatus::not_local;
  require_plugin_decodable(plugin, "renumber");
  const auto [lowest, highest] = local_form_id_range(plugin);
  if (new_local.value() < lowest || new_local.value() > highest) {
    throw FormIdError("new local FormID is outside this plugin's local FormID range");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  auto* target = find_top_level_record(next, record_key);
  if (target == nullptr) {
    if (find_record(next, record_key) != nullptr) {
      throw FormIdError(
          "nested record renumbering is disabled until parent GRUP label rewriting is proven");
    }
    return RecordRenumberStatus::record_missing;
  }

  const FormKey new_key{next.key, new_local};
  if (new_key == record_key) return RecordRenumberStatus::unchanged;
  if (find_record(next, new_key) != nullptr) return RecordRenumberStatus::target_exists;

  rekey_form_links(next, record_key, new_key);
  relabel_record_child_groups(next, record_key, new_key);
  target = find_top_level_record(next, record_key);
  if (target == nullptr) {
    throw FormIdError("record disappeared during transactional FormLink rekey");
  }

  target->form_key = new_key;
  target->header.form_id = encode_plugin_form_id(new_key, plugin_form_id_context(next));
  target->original_range.clear();
  target->decompressed_payload.clear();
  target->header.data_size = 0;
  target->uncompressed_size = 0;

  if (new_local.value() >= (next.header.next_form_id & k_full_local_mask)) {
    set_hedr_next_form_id(next, new_local.value() + 1u);
  }
  next.original_range.clear();
  plugin = std::move(next);
  return RecordRenumberStatus::renumbered;
}

RecordRenumberStatus inject_local_record(Plugin& plugin,
                                         const FormKey& record_key,
                                         const FormKey& new_key) {
  if (record_key.owner() != plugin.key) return RecordRenumberStatus::not_local;
  require_plugin_decodable(plugin, "inject");
  const auto context = plugin_form_id_context(plugin);
  const auto is_master = std::any_of(context.masters.begin(), context.masters.end(),
                                     [&](const MasterDescriptor& master) {
                                       return master.key == new_key.owner();
                                     });
  if (!is_master) throw FormIdError("injected owner is not a master of this plugin");

  auto next = plugin;
  assign_record_form_keys(next);
  auto* target = find_top_level_record(next, record_key);
  if (target == nullptr) {
    if (find_record(next, record_key) != nullptr) {
      throw FormIdError("nested record injection is not supported");
    }
    return RecordRenumberStatus::record_missing;
  }

  if (find_record(next, new_key) != nullptr) return RecordRenumberStatus::target_exists;

  rekey_form_links(next, record_key, new_key);
  relabel_record_child_groups(next, record_key, new_key);
  target = find_top_level_record(next, record_key);
  if (target == nullptr) {
    throw FormIdError("record disappeared during FormLink rekey");
  }

  target->form_key = new_key;
  target->header.form_id = encode_plugin_form_id(new_key, plugin_form_id_context(next));
  target->original_range.clear();
  target->decompressed_payload.clear();
  target->header.data_size = 0;
  target->uncompressed_size = 0;

  next.original_range.clear();
  plugin = std::move(next);
  return RecordRenumberStatus::renumbered;
}

ReferenceRepointResult repoint_record_references(std::span<Plugin*> plugins,
                                                 const FormKey& from,
                                                 const FormKey& to) {
  ReferenceRepointResult result;
  if (from.owner().empty() || from.owner() != to.owner()) {
    result.status = ReferenceRepointStatus::mismatched_owners;
    return result;
  }
  if (from == to) return result;

  std::vector<std::size_t> touched;
  for (std::size_t index = 0; index < plugins.size(); ++index) {
    const auto* plugin = plugins[index];
    if (plugin == nullptr) continue;
    std::size_t found = 0;
    for (const auto& link : collect_form_links(*plugin)) {
      if (link.key == from) ++found;
    }
    if (found == 0) continue;
    result.references += found;
    touched.push_back(index);
  }
  if (touched.empty()) return result;

  std::vector<Plugin> next;
  next.reserve(touched.size());
  for (const auto index : touched) {
    next.push_back(*plugins[index]);
    rekey_form_links(next.back(), from, to);
  }

  for (std::size_t slot = 0; slot < touched.size(); ++slot) {
    result.plugins.push_back(plugins[touched[slot]]->key);
    *plugins[touched[slot]] = std::move(next[slot]);
  }
  result.status = ReferenceRepointStatus::repointed;
  return result;
}

std::string stored_inline_text(std::string_view value, std::string_view field) {
  const auto stored = text::utf8_to_windows1252(value);
  if (!stored) {
    throw FormIdError(std::string(field) + " is not valid UTF-8 text representable in Windows-1252");
  }
  return *stored;
}

std::string stored_editor_id(std::string_view editor_id) {
  return stored_inline_text(editor_id, "editor ID");
}

bool set_record_editor_id(Plugin& plugin,
                          const FormKey& record_key,
                          std::string_view editor_id) {
  if (editor_id.find('\0') != std::string_view::npos) {
    throw FormIdError("editor ID contains an embedded NUL");
  }
  const auto stored = stored_editor_id(editor_id);
  assign_record_form_keys(plugin);
  bool found = false;
  bool changed = false;

  visit_records(plugin, [&](Record& record) {
    if (found || record.header.signature == sig_tes4 || record.form_key != record_key) {
      return;
    }
    found = true;
    require_decodable(record);

    for (auto& subrecord : record.subrecords) {
      if (subrecord.signature != sig_edid) continue;
      const auto next = zstring(stored);
      if (subrecord.data == next) return;
      subrecord.data = next;
      record.original_range.clear();
      changed = true;
      return;
    }

    Subrecord edid;
    edid.signature = sig_edid;
    edid.data = zstring(stored);
    record.subrecords.insert(record.subrecords.begin(), std::move(edid));
    record.original_range.clear();
    changed = true;
  });

  if (!found) return false;
  if (!changed) return true;

  plugin.original_range.clear();
  return true;
}

ScalarMutationStatus set_record_scalar(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::ScalarFieldDescriptor& field,
    std::string_view value) {
  return set_record_scalar(plugin, record_key, field, 0, value);
}

ScalarMutationStatus set_record_scalar(Plugin& plugin,
                                       const FormKey& record_key,
                                       Signature subrecord_signature,
                                       std::string_view value) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return ScalarMutationStatus::record_missing;

  const auto fields = schema::scalar_fields(target->header.signature);
  const auto match = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
    return field.scalar.subrecord == subrecord_signature;
  });
  if (match == fields.end()) {
    throw schema::ScalarError(
        "subrecord is not registered for this record type in the scalar schema: " +
        subrecord_signature.to_string());
  }
  return set_record_scalar(plugin, record_key, *match, value);
}

FormLinkFieldMutationStatus set_record_formlink_field(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::FormLinkFieldDescriptor& field,
    const FormKey& target) {
  return set_record_formlink_field(plugin, record_key, field, 0, target);
}

RecordCopyStatus copy_record_as_override(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record_key,
                                         bool overwrite,
                                         FormLinkTypeLookup type_lookup) {
  const auto* source_record = find_record(source, record_key);
  if (source_record == nullptr) return RecordCopyStatus::source_missing;
  require_decodable(*source_record);

  const bool target_exists = find_record(destination, record_key) != nullptr;
  if (target_exists && !overwrite) return RecordCopyStatus::already_exists;

  const auto* source_top_group =
      target_exists ? nullptr : source_top_group_for_record(source, source_record);
  std::optional<NestedCopyPlan> nested_plan;
  if (!target_exists && source_top_group == nullptr) {
    nested_plan = plan_nested_copy(source, source_record);
    if (!nested_plan) return RecordCopyStatus::source_containment_unsupported;
  }

  auto copied_record = *source_record;
  auto next = destination;

  std::vector<FormKey> required_keys{record_key};
  if (nested_plan) {
    required_keys.push_back(nested_plan->cell->form_key);
    if (nested_plan->world != nullptr) required_keys.push_back(nested_plan->world->form_key);
  }
  const auto links = collect_form_links(source);
  std::set<PluginKey> required_owners;
  for (const auto& key : required_keys) {
    if (!key.owner().empty()) required_owners.insert(key.owner());
    for (const auto& link : links) {
      if (link.record != key || link.key.owner().empty()) continue;
      required_owners.insert(link.key.owner());
    }
  }

  std::vector<MasterDescriptor> masters;
  masters.reserve(next.header.masters.size() + required_owners.size());
  for (const auto& master : next.header.masters) {
    masters.push_back({master.key, master.kind});
  }

  bool added_master = false;
  for (const auto& owner : required_owners) {
    if (owner == next.key || has_master(next, owner)) continue;
    masters.push_back({owner, owner_kind(source, owner)});
    added_master = true;
  }

  if (added_master) {
    next = remap_plugin_masters(std::move(next), std::move(masters), type_lookup);
  }

  const auto source_context = plugin_form_id_context(source);
  const auto destination_context = plugin_form_id_context(next);
  copied_record.header.form_id = encode_plugin_form_id(record_key, destination_context);
  remap_record_form_links(copied_record, source_context, destination_context, type_lookup);
  copied_record.original_range.clear();
  copied_record.decompressed_payload.clear();
  copied_record.header.data_size = 0;
  copied_record.uncompressed_size = 0;

  bool replaced = false;
  if (target_exists) {
    visit_records(next, [&](Record& record) {
      if (replaced || record.header.signature == sig_tes4 || record.form_key != record_key) {
        return;
      }
      require_decodable(record);
      record = copied_record;
      replaced = true;
    });
  }

  if (nested_plan) {
    if (!insert_nested_copy(next, source, *nested_plan, std::move(copied_record))) {
      return RecordCopyStatus::source_containment_unsupported;
    }
  } else if (!replaced) {
    Group* target_group = nullptr;
    for (auto& group : next.groups) {
      if (group.header.group_type == 0 &&
          group.header.top_label() == copied_record.header.signature) {
        target_group = &group;
        break;
      }
    }

    if (target_group == nullptr) {
      Group group;
      group.header = source_top_group->header;
      next.groups.push_back(std::move(group));
      target_group = &next.groups.back();
    }
    target_group->children.emplace_back(std::move(copied_record));
  }

  next.original_range.clear();
  if (!replaced || nested_plan) next.original_structure_fingerprint = 0;
  destination = std::move(next);
  return replaced ? RecordCopyStatus::replaced : RecordCopyStatus::inserted;
}

}  // namespace fo4
