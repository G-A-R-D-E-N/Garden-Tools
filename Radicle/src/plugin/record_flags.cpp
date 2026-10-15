#include "fo4/plugin/record_flags.hpp"

#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "record_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_name = Signature::from_string("NAME");
constexpr auto k_xesp = Signature::from_string("XESP");
constexpr auto k_xtel = Signature::from_string("XTEL");
constexpr RecordFlags::value_type k_kept_bits = RecordFlags::compressed | RecordFlags::partial_form;
constexpr std::uint32_t k_player_form = 0x14;
constexpr float k_undelete_z = -30000.0f;

using detail::MutableLocation;
using detail::erase_if_empty;
using detail::find_mutable_location;

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
  return detail::is_interior_cell(*cell);
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

UndeleteStatus undelete_and_disable_reference(Plugin& plugin, const FormKey& record_key) {
  assign_record_form_keys(plugin);
  if (plugin.tes4.form_key == record_key) return UndeleteStatus::header_record;
  const auto located = find_mutable_location(plugin, record_key);
  if (!located) return UndeleteStatus::record_missing;
  const Record& source = located->record();
  if (!is_placed_reference_signature(source.header.signature)) return UndeleteStatus::not_a_reference;
  if (!source.header.flags.is_deleted()) return UndeleteStatus::not_deleted;
  require_decodable(source);
  if (find_subrecord(source, k_name) == nullptr || find_subrecord(source, k_data) == nullptr) {
    return UndeleteStatus::incomplete_reference;
  }

  Plugin next = plugin;
  auto placed = find_mutable_location(next, record_key);
  if (!placed) return UndeleteStatus::record_missing;
  Record& record = placed->record();
  const bool persistent = record.header.flags.is_persistent();
  detail::mark_record_modified(next, record);
  record.header.flags =
      RecordFlags{(record.header.flags.value() & ~RecordFlags::deleted) | RecordFlags::initially_disabled};
  std::erase_if(record.subrecords,
                [](const Subrecord& sub) { return sub.signature == k_xesp || sub.signature == k_xtel; });
  if (!persistent) {
    auto transform = forms::decode_placed_reference(next, record)->transform;
    transform.z = k_undelete_z;
    forms::set_placed_reference_transform(next, record_key, transform);
  }
  forms::set_placed_reference_enable_parent(
      next, record_key, resolve_plugin_form_id(RawFormId{k_player_form}, plugin_form_id_context(next)), 1);
  plugin = std::move(next);
  return UndeleteStatus::undeleted;
}

}  // namespace fo4
