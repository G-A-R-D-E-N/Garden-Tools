#include "fo4/plugin/record_flags.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_name = Signature::from_string("NAME");
constexpr RecordFlags::value_type k_kept_bits = RecordFlags::compressed | RecordFlags::partial_form;

struct MutableLocation {
  std::vector<Group*> path;
  std::size_t index{0};

  Record& record() const { return std::get<Record>(path.back()->children[index]); }
};

bool locate_in_group(Group& group, const FormKey& record_key, MutableLocation& found) {
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

std::optional<MutableLocation> find_mutable_location(Plugin& plugin, const FormKey& record_key) {
  for (auto& group : plugin.groups) {
    MutableLocation found;
    if (locate_in_group(group, record_key, found)) return found;
  }
  return std::nullopt;
}

bool has_type(const GroupHeader& header, GroupType type) {
  return header.group_type == static_cast<std::int32_t>(type);
}

bool is_cell_bucket(const GroupHeader& header) {
  return has_type(header, GroupType::cell_persistent_children) ||
         has_type(header, GroupType::cell_temporary_children) ||
         has_type(header, GroupType::cell_visible_distant_children);
}

const Subrecord* find_subrecord(const Record& record, Signature signature) {
  const auto found = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                  [signature](const Subrecord& subrecord) {
                                    return subrecord.signature == signature;
                                  });
  return found == record.subrecords.end() ? nullptr : &*found;
}

bool cell_is_interior(const Plugin& plugin, std::uint32_t label) {
  const auto cell_key = resolve_plugin_form_id(RawFormId{label}, plugin_form_id_context(plugin));
  const auto* cell = find_record(plugin, cell_key);
  if (cell == nullptr || cell->header.signature != k_cell) return false;
  require_decodable(*cell);
  const auto* data = find_subrecord(*cell, k_data);
  return data != nullptr && !data->data.empty() &&
         (std::to_integer<std::uint8_t>(data->data[0]) & 1u) != 0;
}

Group& ensure_bucket(Group& cell_children, const GroupHeader& wanted) {
  for (auto& child : cell_children.children) {
    auto* group = std::get_if<Group>(&child);
    if (group != nullptr && group->header.group_type == wanted.group_type &&
        group->header.label == wanted.label) {
      return *group;
    }
  }
  Group created;
  created.header = wanted;
  const auto later = std::find_if(
      cell_children.children.begin(), cell_children.children.end(), [&wanted](const GroupChild& child) {
        const auto* group = std::get_if<Group>(&child);
        return group != nullptr && group->header.group_type > wanted.group_type;
      });
  return std::get<Group>(*cell_children.children.insert(later, std::move(created)));
}

void erase_if_empty(Group& parent, const GroupHeader& wanted) {
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

void move_to_bucket(MutableLocation& located, GroupType bucket_type) {
  auto& old_bucket = *located.path.back();
  auto& cell_children = *located.path[located.path.size() - 2];
  const auto old_header = old_bucket.header;
  auto moved = std::move(std::get<Record>(old_bucket.children[located.index]));
  old_bucket.children.erase(old_bucket.children.begin() + static_cast<std::ptrdiff_t>(located.index));

  GroupHeader wanted = old_header;
  wanted.group_type = static_cast<std::int32_t>(bucket_type);
  wanted.group_size = 0;
  ensure_bucket(cell_children, wanted).children.emplace_back(std::move(moved));
  erase_if_empty(cell_children, old_header);
}

}  // namespace

RecordFlagStatus set_record_flags(Plugin& plugin, const FormKey& record_key, RecordFlags::value_type flags) {
  assign_record_form_keys(plugin);
  if (plugin.tes4.form_key == record_key) return RecordFlagStatus::header_record;
  auto located = find_mutable_location(plugin, record_key);
  if (!located) return RecordFlagStatus::record_missing;
  Record& record = located->record();
  require_decodable(record);

  const auto current = record.header.flags.value();
  const bool deleted = record.header.flags.is_deleted();
  if (((flags & RecordFlags::deleted) != 0) != deleted) return RecordFlagStatus::deleted_bit;

  auto next = (flags & ~(RecordFlags::master | k_kept_bits)) | (current & k_kept_bits);
  if (deleted) next = (next & ~RecordFlags::persistent) | (current & RecordFlags::persistent);
  if (next == current) return RecordFlagStatus::unchanged;

  const auto& path = located->path;
  const auto depth = path.size();
  const bool persistent_changed = ((next ^ current) & RecordFlags::persistent) != 0;
  const auto target = (next & RecordFlags::persistent) != 0 ? GroupType::cell_persistent_children
                                                             : GroupType::cell_temporary_children;
  const bool relocates = persistent_changed && depth >= 2 &&
                         is_placed_reference_signature(record.header.signature) &&
                         is_cell_bucket(path.back()->header) &&
                         has_type(path[depth - 2]->header, GroupType::cell_children) &&
                         !has_type(path.back()->header, target);
  if (relocates && !cell_is_interior(plugin, path[depth - 2]->header.label_as_u32())) {
    return RecordFlagStatus::exterior_relocation_unsupported;
  }

  detail::mark_record_modified(plugin, record);
  record.header.flags = RecordFlags{next};
  if (relocates) move_to_bucket(*located, target);
  return RecordFlagStatus::changed;
}

MarkDeletedStatus mark_override_deleted(Plugin& plugin, const FormKey& record_key) {
  assign_record_form_keys(plugin);
  if (plugin.tes4.form_key == record_key) return MarkDeletedStatus::header_record;
  auto located = find_mutable_location(plugin, record_key);
  if (!located) return MarkDeletedStatus::record_missing;
  Record& record = located->record();
  if (record.form_key.owner() == plugin.key) return MarkDeletedStatus::local_record;
  if (record.header.flags.is_deleted()) return MarkDeletedStatus::already_deleted;
  require_decodable(record);

  constexpr auto dropped = RecordFlags::compressed | RecordFlags::partial_form;
  record.header.flags = RecordFlags{(record.header.flags.value() | RecordFlags::deleted) & ~dropped};
  record.compressed = false;
  std::vector<Subrecord> kept;
  if (is_placed_reference_signature(record.header.signature)) {
    const auto name = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                   [](const Subrecord& subrecord) { return subrecord.signature == k_name; });
    if (name != record.subrecords.end()) kept.push_back(std::move(*name));
  }
  record.subrecords = std::move(kept);
  detail::mark_record_modified(plugin, record);
  return MarkDeletedStatus::marked;
}

}  // namespace fo4
