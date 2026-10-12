#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <string>
#include <string_view>

namespace fo4::detail {

inline Record* find_mutable_record_in(Group& group, const FormKey& record_key) {
  for (auto& child : group.children) {
    if (auto* record = std::get_if<Record>(&child)) {
      if (record->form_key == record_key && record->header.signature != sig_tes4) {
        require_decodable(*record);
        return record;
      }
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

inline void require_plugin_decodable(const Plugin& plugin, std::string_view operation) {
  std::size_t count = 0;
  const Record* first = nullptr;
  visit_records(plugin, [&](const Record& record) {
    if (!record.undecodable) return;
    if (first == nullptr) first = &record;
    ++count;
  });
  if (first == nullptr) return;
  throw std::invalid_argument("cannot " + std::string(operation) + ": plugin has " +
                              std::to_string(count) + " undecodable record(s), first is " +
                              first->form_key.to_string() + "; edits are refused");
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
