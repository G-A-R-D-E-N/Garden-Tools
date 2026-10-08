#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

namespace fo4::detail {

inline Record* find_mutable_record_in(Group& group, const FormKey& record_key) {
  for (auto& child : group.children) {
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->form_key == record_key) return record;
    } else if (auto* found = find_mutable_record_in(std::get<Group>(child), record_key)) {
      return found;
    }
  }
  return nullptr;
}

inline Record* find_mutable_record(Plugin& plugin, const FormKey& record_key) {
  for (auto& group : plugin.groups) {
    if (auto* found = find_mutable_record_in(group, record_key)) return found;
  }
  return nullptr;
}

inline Record* locate_mutable_record(Plugin& plugin, const FormKey& record_key) {
  if (auto* found = find_mutable_record(plugin, record_key)) return found;
  assign_record_form_keys(plugin);
  return find_mutable_record(plugin, record_key);
}

inline void mark_record_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

}
