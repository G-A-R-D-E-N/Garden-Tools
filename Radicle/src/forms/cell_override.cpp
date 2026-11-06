#include "fo4/forms/cell_edit.hpp"

#include "fo4/forms/cell.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_wrld = Signature::from_string("WRLD");

}  // namespace

CellOverrideStatus copy_cell_as_override(Plugin& patch, const Plugin& source, const FormKey& cell) {
  const auto located = detail::locate_record(source, cell);
  if (located.record == nullptr) return CellOverrideStatus::source_missing;
  if (located.record->header.signature != k_cell) return CellOverrideStatus::not_a_cell;

  const auto source_context = plugin_form_id_context(source);
  auto next = patch;
  assign_record_form_keys(next);
  if (find_record(next, cell) != nullptr) return CellOverrideStatus::already_exists;

  std::optional<FormKey> world;
  detail::RecordLocation source_world;
  for (std::size_t index = 0; index < located.path.size(); ++index) {
    if (located.path[index]->header.group_type !=
        static_cast<std::int32_t>(GroupType::world_children)) {
      continue;
    }
    const RawFormId world_raw{located.path[index]->header.label_as_u32()};
    if (world_raw.is_null()) throw CellError("null worldspace label");
    world = resolve_plugin_form_id(world_raw, source_context);
    source_world = detail::locate_record(source, *world);
    if (source_world.record == nullptr || source_world.record->header.signature != k_wrld) {
      throw CellError("source worldspace record missing");
    }
    if (source_world.path.size() != index || source_world.path.front() != located.path.front()) {
      throw CellError("source worldspace containment mismatch");
    }
    break;
  }

  std::set<PluginKey> required_owners;
  if (!cell.owner().empty()) required_owners.insert(cell.owner());
  if (world.has_value() && !world->owner().empty()) required_owners.insert(world->owner());
  for (const auto* group : located.path) {
    detail::collect_group_label_owner(*group, source_context, required_owners);
  }
  const auto links = collect_form_links_detailed(source);
  for (const auto& link : links.links) {
    const bool relevant = link.record == cell || (world.has_value() && link.record == *world);
    if (!relevant || link.key.owner().empty()) continue;
    required_owners.insert(link.key.owner());
  }
  detail::add_missing_masters(next, source, required_owners);

  const auto destination_context = plugin_form_id_context(next);
  if (world.has_value()) {
    detail::validate_destination_record_path(next, *world, k_wrld, source_world.path,
                                             source_context, destination_context, "WRLD");
    auto* world_parent = detail::ensure_group_path(next, source_world.path, source_context,
                                                   destination_context);
    if (find_record(next, *world) == nullptr) {
      world_parent->children.emplace_back(detail::copy_record_for_destination(
          *source_world.record, *world, source_context, destination_context));
    }
  }

  auto* cell_parent = detail::ensure_group_path(next, located.path, source_context,
                                                destination_context);
  cell_parent->children.emplace_back(detail::copy_record_for_destination(
      *located.record, cell, source_context, destination_context));

  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  patch = std::move(next);
  return CellOverrideStatus::inserted;
}

}  // namespace fo4::forms
