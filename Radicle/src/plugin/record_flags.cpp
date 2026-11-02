#include "fo4/plugin/record_flags.hpp"

#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_link_mixed.hpp"
#include "fo4/schema/form_links.hpp"
#include "record_edit.hpp"
#include "../forms/placed_reference_copy_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_name = Signature::from_string("NAME");
constexpr auto k_wrld = Signature::from_string("WRLD");
constexpr auto k_xesp = Signature::from_string("XESP");
constexpr auto k_xtel = Signature::from_string("XTEL");
constexpr RecordFlags::value_type k_kept_bits = RecordFlags::compressed | RecordFlags::partial_form;
constexpr std::string_view k_player_key = "000014:Fallout4.esm";
constexpr float k_undelete_z = -30000.0f;

using detail::erase_if_empty;
using detail::find_mutable_location;

bool has_type(const GroupHeader& header, GroupType type) {
  return header.group_type == static_cast<std::int32_t>(type);
}

bool is_master_of(const Plugin& plugin, const PluginKey& key) {
  return std::any_of(plugin.header.masters.begin(), plugin.header.masters.end(),
                     [&key](const MasterReference& master) { return master.key == key; });
}

bool is_injected(const env::Environment& load_order, const Plugin& plugin, const FormKey& record_key) {
  if (record_key.owner() == plugin.key) return false;
  const auto versions = load_order.versions(record_key);
  return std::none_of(versions.begin(), versions.end(), [&](const env::RecordRef& version) {
    return load_order.plugin(version.plugin).key == record_key.owner();
  });
}

const env::RecordRef* winning_master_content(const env::Environment& load_order,
                                             const Plugin& plugin,
                                             const FormKey& record_key) {
  const auto versions = load_order.versions(record_key);
  for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
    const auto& owner = load_order.plugin(it->plugin).key;
    if (owner == plugin.key || !is_master_of(plugin, owner)) continue;
    if (it->record->header.flags.is_deleted() || is_partial_form(*it->record) ||
        it->record->undecodable) {
      continue;
    }
    return &*it;
  }
  return nullptr;
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

RecordFlagStatus relocate_reference(Plugin& plugin,
                                    const FormKey& record_key,
                                    RecordFlags::value_type flags,
                                    GroupType target,
                                    bool interior) {
  Plugin next = plugin;
  auto located = find_mutable_location(next, record_key);
  if (!located || located->path.size() < 3) {
    throw forms::PlacedReferenceError("placed reference is not beneath a CELL child GRUP");
  }
  const auto depth = located->path.size();
  const auto context = plugin_form_id_context(next);
  Group& bucket = *located->path.back();
  Group& cell_children = *located->path[depth - 2];
  Group& owner = *located->path[depth - 3];
  FormKey source = resolve_plugin_form_id(RawFormId{cell_children.header.label_as_u32()}, context);
  FormKey destination = source;
  const auto commit_flags_only = [&]() {
    detail::mark_record_modified(next, located->record());
    located->record().header.flags = RecordFlags{flags};
    plugin = std::move(next);
    return RecordFlagStatus::changed;
  };
  if (!interior) {
    const auto owner_type = static_cast<GroupType>(owner.header.group_type);
    const auto* cell = forms::detail::preceding_record(owner, cell_children);
    if (cell == nullptr || cell->header.signature != k_cell) {
      throw forms::PlacedReferenceError("placed reference CELL record is missing from its group");
    }
    require_decodable(*cell);
    const auto* data = find_subrecord(*cell, k_data);
    if (data == nullptr || data->data.empty()) return commit_flags_only();
    if (owner_type != GroupType::world_children && owner_type != GroupType::exterior_cell_subblock) {
      throw forms::PlacedReferenceError("exterior placed reference CELL is not inside a worldspace");
    }
    const auto* worldspace = forms::detail::worldspace_record(located->path, depth - 3);
    if (worldspace == nullptr || worldspace->header.signature != k_wrld) {
      throw forms::PlacedReferenceError("exterior placed reference CELL has no WRLD record");
    }
    const auto worldspace_key = worldspace->form_key;
    source = cell->form_key;
    destination = source;
    if (target == GroupType::cell_persistent_children) {
      if (owner_type != GroupType::world_children) {
        const auto* persistent = forms::find_persistent_exterior_cell(next, worldspace_key);
        if (persistent == nullptr) return RecordFlagStatus::target_cell_missing;
        destination = persistent->form_key;
      }
    } else {
      const auto position = forms::decode_placed_reference(next, located->record())->transform;
      if (!forms::detail::exterior_grid_in_range(position.x) ||
          !forms::detail::exterior_grid_in_range(position.y)) {
        return RecordFlagStatus::target_cell_missing;
      }
      const auto* grid = forms::find_exterior_cell(next, worldspace_key,
                                                   forms::detail::grid_coordinate(position.x),
                                                   forms::detail::grid_coordinate(position.y));
      if (grid == nullptr) return RecordFlagStatus::target_cell_missing;
      destination = grid->form_key;
    }
  }

  Record& record = located->record();
  detail::mark_record_modified(next, record);
  record.header.flags = RecordFlags{flags};
  if (destination == source) {
    if (!has_type(bucket.header, target)) {
      forms::detail::move_reference_to_bucket(*located, target, context);
    }
    plugin = std::move(next);
    return RecordFlagStatus::changed;
  }

  const auto bucket_header = bucket.header;
  const auto children_header = cell_children.header;
  Record moved = std::move(std::get<Record>(bucket.children[located->index]));
  bucket.children.erase(bucket.children.begin() + static_cast<std::ptrdiff_t>(located->index));
  if (bucket.children.empty()) erase_if_empty(cell_children, bucket_header);
  if (cell_children.children.empty()) erase_if_empty(owner, children_header);

  auto destination_location = find_mutable_location(next, destination);
  if (!destination_location) {
    throw forms::PlacedReferenceError("placed reference target CELL disappeared during relocation");
  }
  const auto label = encode_plugin_form_id(destination, context).value();
  auto* destination_children = forms::detail::ensure_cell_children_group(
      *destination_location->path.back(), forms::detail::group_header(GroupType::cell_children, label),
      destination, context, context);
  forms::detail::ensure_reference_bucket(*destination_children,
                                         forms::detail::group_header(target, label), context, context)
      ->children.emplace_back(std::move(moved));
  plugin = std::move(next);
  return RecordFlagStatus::changed;
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
  const bool in_cell_bucket = persistent_changed && depth >= 2 &&
                              is_placed_reference_signature(record.header.signature) &&
                              is_cell_bucket(path.back()->header) &&
                              has_type(path[depth - 2]->header, GroupType::cell_children);
  const bool interior = in_cell_bucket && cell_is_interior(plugin, path[depth - 2]->header.label_as_u32());
  const bool keeps_bucket = in_cell_bucket && has_type(path.back()->header, target) &&
                            (interior || target == GroupType::cell_persistent_children);
  if (!in_cell_bucket || keeps_bucket) {
    detail::mark_record_modified(plugin, record);
    record.header.flags = RecordFlags{next};
    return RecordFlagStatus::changed;
  }
  return relocate_reference(plugin, record_key, next, target, interior);
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

  auto flags = (record.header.flags.value() | RecordFlags::deleted) & ~RecordFlags::compressed;
  if (can_be_partial(record.header.signature)) flags &= ~RecordFlags::partial_form;
  record.header.flags = RecordFlags{flags};
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

UndeleteStatus undelete_and_disable_reference(Plugin& plugin,
                                             const env::Environment& load_order,
                                             const FormKey& record_key) {
  assign_record_form_keys(plugin);
  if (plugin.tes4.form_key == record_key) return UndeleteStatus::header_record;
  const auto located = find_mutable_location(plugin, record_key);
  if (!located) return UndeleteStatus::record_missing;
  const Record& source = located->record();
  if (!is_placed_reference_signature(source.header.signature)) return UndeleteStatus::not_a_reference;
  if (!source.header.flags.is_deleted()) return UndeleteStatus::not_deleted;
  require_decodable(source);
  if (is_injected(load_order, plugin, record_key)) return UndeleteStatus::injected_record;
  const bool complete = find_subrecord(source, k_name) != nullptr &&
                        find_subrecord(source, k_data) != nullptr;
  const env::RecordRef* master =
      complete ? nullptr : winning_master_content(load_order, plugin, record_key);
  if (!complete && master == nullptr) return UndeleteStatus::incomplete_reference;

  Plugin next = plugin;
  auto placed = find_mutable_location(next, record_key);
  if (!placed) return UndeleteStatus::record_missing;
  Record& record = placed->record();
  if (master != nullptr) {
    const auto& master_plugin = load_order.plugin(master->plugin);
    Record restored = *master->record;
    try {
      remap_record_form_links(restored, plugin_form_id_context(master_plugin),
                              plugin_form_id_context(next), plugin_type_lookup(master_plugin));
    } catch (const std::exception&) {
      return UndeleteStatus::incomplete_reference;
    }
    record.subrecords = std::move(restored.subrecords);
  }
  if (find_subrecord(record, k_name) == nullptr || find_subrecord(record, k_data) == nullptr) {
    return UndeleteStatus::incomplete_reference;
  }
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
  const auto player = FormKey::parse(k_player_key);
  const bool has_player_master =
      next.key == player.owner() || std::any_of(next.header.masters.begin(), next.header.masters.end(),
                                                [&](const MasterReference& held) { return held.key == player.owner(); });
  if (!has_player_master) {
    std::vector<MasterDescriptor> masters{{player.owner(), PluginKind::full}};
    for (const auto& held : next.header.masters) masters.push_back({held.key, held.kind});
    const auto own_types = plugin_type_lookup(plugin);
    const FormLinkTypeLookup types = [&](const FormKey& key) -> std::optional<Signature> {
      if (const auto own = own_types(key)) return own;
      const auto* winner = load_order.winner(key);
      if (winner == nullptr || winner->record == nullptr) return std::nullopt;
      return winner->record->header.signature;
    };
    try {
      next = remap_plugin_masters(std::move(next), std::move(masters), types);
    } catch (const std::exception&) {
      return UndeleteStatus::incomplete_reference;
    }
  }
  forms::set_placed_reference_enable_parent(next, record_key, player, 1);
  plugin = std::move(next);
  return UndeleteStatus::undeleted;
}

}  // namespace fo4
