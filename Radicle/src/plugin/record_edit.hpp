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

inline void erase_if_empty(Group& parent, const GroupHeader& wanted) {
  for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
    auto* group = std::get_if<Group>(&*it);
    if (group == nullptr || group->header.group_type != wanted.group_type ||
        group->header.label != wanted.label) {
      continue;
    }
    if (group->children.empty()) parent.children.erase(it);
    return;
  }
}

struct MutableLocation {
  std::vector<Group*> path;
  std::size_t index{0};

  Record& record() const { return std::get<Record>(path.back()->children[index]); }
};

inline bool locate_in_group(Group& group, const FormKey& record_key, MutableLocation& found) {
  found.path.push_back(&group);
  for (std::size_t index = 0; index < group.children.size(); ++index) {
    if (auto* record = std::get_if<Record>(&group.children[index])) {
      if (record->form_key == record_key) {
        found.index = index;
        return true;
      }
    } else if (locate_in_group(std::get<Group>(group.children[index]), record_key, found)) {
      return true;
    }
  }
  found.path.pop_back();
  return false;
}

inline std::optional<MutableLocation> find_mutable_location(Plugin& plugin, const FormKey& record_key) {
  for (auto& group : plugin.groups) {
    MutableLocation found;
    if (locate_in_group(group, record_key, found)) return found;
  }
  return std::nullopt;
}

inline bool is_interior_cell(const Record& cell) {
  constexpr auto data = Signature::from_string("DATA");
  for (const auto& subrecord : cell.subrecords) {
    if (subrecord.signature != data) continue;
    return !subrecord.data.empty() && (std::to_integer<std::uint8_t>(subrecord.data[0]) & 1u) != 0;
  }
  return false;
}

}
