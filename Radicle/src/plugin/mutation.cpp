#include "fo4/plugin/mutation.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"

#include <cstddef>
#include <cstdint>
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

GroupHeader source_top_group_header(const Plugin& source, Signature signature) {
  for (const auto& group : source.groups) {
    if (group.header.group_type == 0 && group.header.top_label() == signature) {
      return group.header;
    }
  }

  GroupHeader header;
  const auto label = signature.bytes();
  for (std::size_t i = 0; i < label.size(); ++i) {
    header.label[i] = std::byte{static_cast<std::uint8_t>(label[i])};
  }
  header.group_type = 0;
  return header;
}

void clear_group_originals(Plugin& plugin) {
  visit_groups(plugin, [](Group& group) { group.original_bytes.clear(); });
}

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
