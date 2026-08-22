#include "fo4/plugin/mutation.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/scalar.hpp"

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
    return source.header.tes4_flags.is_small_file() ? PluginKind::light
                                                   : PluginKind::full;
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

GroupHeader source_top_group_header(const Plugin& source, Signature signature) {
  for (const auto& group : source.groups) {
    if (group.header.group_type == 0 && group.header.top_label() == signature) {
      return group.header;
    }
  }
  return new_top_group_header(signature);
}

void clear_group_originals(Plugin& plugin) {
  visit_groups(plugin, [](Group& group) { group.original_bytes.clear(); });
}

Record* find_mutable_record(Plugin& plugin, const FormKey& record_key) {
  Record* target = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (target == nullptr && record.header.signature != sig_tes4 &&
        record.form_key == record_key) {
      target = &record;
    }
  });
  return target;
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
  if (hedr == nullptr || hedr->data.size() != 12) {
    throw FormIdError("TES4 HEDR must be exactly 12 bytes to update nextFormID");
  }

  std::memcpy(hedr->data.data() + 8, &next_form_id, sizeof(next_form_id));
  plugin.header.next_form_id = next_form_id;
  plugin.tes4.original_bytes.clear();
  plugin.tes4.decompressed_payload.clear();
  plugin.tes4.header.data_size = 0;
  plugin.tes4.uncompressed_size = 0;
}

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

bool erase_record(Group& group, const FormKey& record_key) {
  for (auto it = group.children.begin(); it != group.children.end(); ++it) {
    if (auto* record = std::get_if<Record>(&*it)) {
      if (record->form_key != record_key) continue;
      group.children.erase(it);
      return true;
    }
    if (erase_record(std::get<Group>(*it), record_key)) return true;
  }
  return false;
}

}  // namespace

FormKey create_local_record(Plugin& plugin, Signature signature) {
  if (signature.packed() == 0 || signature == sig_tes4 || signature == sig_grup) {
    throw FormIdError("invalid major-record signature for local record creation");
  }
  if (plugin.header.tes4_flags.is_small_file()) {
    throw FormIdError(
        "light-plugin record creation is disabled until CheckValidIDForSmallFile is proven");
  }
  if (plugin.key.empty()) {
    throw FormIdError("plugin has no identity for local record creation");
  }

  auto next = plugin;
  assign_record_form_keys(next);

  auto candidate = next.header.next_form_id;
  if (candidate == 0 || candidate > k_full_local_mask) {
    throw FormIdError("HEDR nextFormID is outside the full-plugin local FormID range");
  }

  while (candidate <= k_full_local_mask) {
    const FormKey probe{next.key, LocalFormId{candidate}};
    if (find_record(next, probe) == nullptr) break;
    ++candidate;
  }
  if (candidate > k_full_local_mask) {
    throw FormIdError("no local FormID remains at or above HEDR nextFormID");
  }

  const FormKey key{next.key, LocalFormId{candidate}};
  Record record;
  record.header.signature = signature;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  record.header.form_version = next.header.form_version;
  record.form_key = key;

  auto* group = find_or_create_top_group(next, signature);
  group->children.emplace_back(std::move(record));

  set_hedr_next_form_id(next, candidate + 1u);
  next.original_bytes.clear();
  clear_group_originals(next);
  plugin = std::move(next);
  return key;
}

RecordDeleteStatus delete_local_record(Plugin& plugin, const FormKey& record_key) {
  if (record_key.owner() != plugin.key) return RecordDeleteStatus::not_local;

  auto next = plugin;
  assign_record_form_keys(next);
  if (find_record(next, record_key) == nullptr) return RecordDeleteStatus::record_missing;

  bool erased = false;
  for (auto& group : next.groups) {
    if (erase_record(group, record_key)) {
      erased = true;
      break;
    }
  }
  if (!erased) return RecordDeleteStatus::record_missing;

  next.original_bytes.clear();
  clear_group_originals(next);
  plugin = std::move(next);
  return RecordDeleteStatus::deleted;
}

bool set_record_editor_id(Plugin& plugin,
                          const FormKey& record_key,
                          std::string_view editor_id) {
  assign_record_form_keys(plugin);
  bool found = false;
  bool changed = false;

  visit_records(plugin, [&](Record& record) {
    if (found || record.header.signature == sig_tes4 || record.form_key != record_key) {
      return;
    }
    found = true;

    for (auto& subrecord : record.subrecords) {
      if (subrecord.signature != sig_edid) continue;
      const auto next = zstring(editor_id);
      if (subrecord.data == next) return;
      subrecord.data = next;
      record.original_bytes.clear();
      changed = true;
      return;
    }

    Subrecord edid;
    edid.signature = sig_edid;
    edid.data = zstring(editor_id);
    record.subrecords.insert(record.subrecords.begin(), std::move(edid));
    record.original_bytes.clear();
    changed = true;
  });

  if (!found) return false;
  if (!changed) return true;

  plugin.original_bytes.clear();
  clear_group_originals(plugin);
  return true;
}

ScalarMutationStatus set_record_scalar(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::ScalarFieldDescriptor& field,
    std::string_view value) {
  assign_record_form_keys(plugin);
  auto* target = find_mutable_record(plugin, record_key);
  if (target == nullptr) return ScalarMutationStatus::record_missing;

  if (target->header.signature != field.record) {
    throw schema::ScalarError(
        std::string(field.path) + " does not apply to " +
        target->header.signature.to_string() + " records");
  }

  Subrecord* found = nullptr;
  for (auto& subrecord : target->subrecords) {
    if (subrecord.signature != field.scalar.subrecord) continue;
    if (found != nullptr) {
      throw schema::ScalarError(
          "record contains duplicate scalar subrecords: " +
          field.scalar.subrecord.to_string());
    }
    found = &subrecord;
  }
  if (found == nullptr) return ScalarMutationStatus::subrecord_missing;

  // Existing bytes are read faithfully, but new values are validated by the
  // exact record-qualified field descriptor. Missing fields are not inserted
  // until schema/codegen also carries proven subrecord ordering.
  static_cast<void>(schema::decode_scalar(found->data, field.scalar));
  const auto encoded = schema::encode_scalar(value, field.scalar);
  if (found->data == encoded) return ScalarMutationStatus::unchanged;

  found->data = encoded;
  target->original_bytes.clear();
  target->decompressed_payload.clear();
  target->header.data_size = 0;
  target->uncompressed_size = 0;
  plugin.original_bytes.clear();
  clear_group_originals(plugin);
  return ScalarMutationStatus::updated;
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

RecordCopyStatus copy_record_as_override(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record_key,
                                         bool overwrite) {
  const auto* source_record = find_record(source, record_key);
  if (source_record == nullptr) return RecordCopyStatus::source_missing;

  const bool target_exists = find_record(destination, record_key) != nullptr;
  if (target_exists && !overwrite) return RecordCopyStatus::already_exists;

  auto copied_record = *source_record;
  auto next = destination;

  std::set<PluginKey> required_owners;
  if (!record_key.owner().empty()) required_owners.insert(record_key.owner());
  for (const auto& link : collect_form_links(source)) {
    if (link.record != record_key || link.key.owner().empty()) continue;
    required_owners.insert(link.key.owner());
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
    next = remap_plugin_masters(std::move(next), std::move(masters));
  }

  const auto source_context = plugin_form_id_context(source);
  const auto destination_context = plugin_form_id_context(next);
  copied_record.header.form_id = encode_plugin_form_id(record_key, destination_context);
  remap_record_form_links(copied_record, source_context, destination_context);
  copied_record.original_bytes.clear();
  copied_record.decompressed_payload.clear();
  copied_record.header.data_size = 0;
  copied_record.uncompressed_size = 0;

  bool replaced = false;
  if (target_exists) {
    visit_records(next, [&](Record& record) {
      if (replaced || record.header.signature == sig_tes4 || record.form_key != record_key) {
        return;
      }
      record = copied_record;
      replaced = true;
    });
  }

  if (!replaced) {
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
      group.header = source_top_group_header(source, copied_record.header.signature);
      group.original_bytes.clear();
      next.groups.push_back(std::move(group));
      target_group = &next.groups.back();
    }
    target_group->children.emplace_back(std::move(copied_record));
  }

  next.original_bytes.clear();
  clear_group_originals(next);
  destination = std::move(next);
  return replaced ? RecordCopyStatus::replaced : RecordCopyStatus::inserted;
}

}  // namespace fo4
