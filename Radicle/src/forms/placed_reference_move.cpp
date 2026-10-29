#include "fo4/forms/cell.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "../plugin/record_edit.hpp"
#include "placed_reference_copy_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr auto k_wrld = Signature::from_string("WRLD");

using fo4::detail::erase_if_empty;
using fo4::detail::find_mutable_location;
using fo4::detail::is_interior_cell;

}  // namespace

PlacedReferenceMoveStatus move_placed_reference(Plugin& plugin,
                                                const FormKey& reference,
                                                const PositionRotation& transform) {
  auto next = plugin;
  assign_record_form_keys(next);
  if (!set_placed_reference_transform(next, reference, transform)) {
    return PlacedReferenceMoveStatus::reference_missing;
  }
  auto located = find_mutable_location(next, reference);
  if (!located) return PlacedReferenceMoveStatus::reference_missing;

  const auto commit = [&](PlacedReferenceMoveStatus status) {
    next.original_range.clear();
    plugin = std::move(next);
    return status;
  };

  const auto depth = located->path.size();
  Group& bucket = *located->path.back();
  const auto& record = std::get<Record>(bucket.children[located->index]);
  const auto bucket_type = static_cast<GroupType>(bucket.header.group_type);
  if (bucket_type != GroupType::cell_persistent_children &&
      bucket_type != GroupType::cell_temporary_children &&
      bucket_type != GroupType::cell_visible_distant_children) {
    throw PlacedReferenceError("placed reference is not inside a CELL child bucket");
  }
  if (depth < 3 ||
      static_cast<GroupType>(located->path[depth - 2]->header.group_type) != GroupType::cell_children) {
    throw PlacedReferenceError("placed reference is not beneath a CELL child group");
  }
  Group& cell_children = *located->path[depth - 2];
  const auto owner_index = depth - 3;
  Group& owner = *located->path[owner_index];
  const auto* cell = detail::preceding_record(owner, cell_children);
  if (cell == nullptr || cell->header.signature != k_cell) {
    throw PlacedReferenceError("placed reference CELL record is missing from its group");
  }
  require_decodable(*cell);
  const auto cell_key = cell->form_key;
  const auto context = plugin_form_id_context(next);

  const auto wanted_bucket = record.header.flags.is_persistent()
                                 ? GroupType::cell_persistent_children
                                 : GroupType::cell_temporary_children;
  const bool bucket_matches = bucket_type == wanted_bucket;
  if (bucket_matches && wanted_bucket == GroupType::cell_persistent_children) {
    return commit(PlacedReferenceMoveStatus::moved);
  }
  if (is_interior_cell(*cell)) {
    if (bucket_matches) return commit(PlacedReferenceMoveStatus::moved);
    detail::move_reference_to_bucket(*located, wanted_bucket, context);
    return commit(PlacedReferenceMoveStatus::moved);
  }

  const auto owner_type = static_cast<GroupType>(owner.header.group_type);
  if (owner_type != GroupType::world_children && owner_type != GroupType::exterior_cell_subblock) {
    throw PlacedReferenceError("exterior placed reference CELL is not inside a worldspace");
  }
  const auto* worldspace = detail::worldspace_record(located->path, owner_index);
  if (worldspace == nullptr || worldspace->header.signature != k_wrld) {
    throw PlacedReferenceError("exterior placed reference CELL has no WRLD record");
  }

  FormKey target_key = cell_key;
  if (wanted_bucket == GroupType::cell_persistent_children) {
    if (owner_type != GroupType::world_children) {
      const auto* persistent = find_persistent_exterior_cell(next, worldspace->form_key);
      if (persistent == nullptr) return PlacedReferenceMoveStatus::target_cell_missing;
      target_key = persistent->form_key;
    }
  } else {
    if (!detail::exterior_grid_in_range(transform.x) ||
        !detail::exterior_grid_in_range(transform.y)) {
      return PlacedReferenceMoveStatus::target_cell_missing;
    }
    const auto* target = find_exterior_cell(next, worldspace->form_key,
                                            detail::grid_coordinate(transform.x),
                                            detail::grid_coordinate(transform.y));
    if (target == nullptr) return PlacedReferenceMoveStatus::target_cell_missing;
    target_key = target->form_key;
  }

  if (target_key == cell_key) {
    if (!bucket_matches) detail::move_reference_to_bucket(*located, wanted_bucket, context);
    return commit(PlacedReferenceMoveStatus::moved);
  }
  if (bucket_type == GroupType::cell_visible_distant_children) {
    throw PlacedReferenceError("visible-distant placed reference cannot move to another exterior CELL");
  }

  const auto target_label = encode_plugin_form_id(target_key, context).value();
  Record moved = std::move(std::get<Record>(bucket.children[located->index]));
  bucket.children.erase(bucket.children.begin() + static_cast<std::ptrdiff_t>(located->index));
  if (bucket.children.empty()) erase_if_empty(cell_children, bucket.header);
  if (cell_children.children.empty()) erase_if_empty(owner, cell_children.header);

  auto target_path = find_mutable_location(next, target_key);
  if (!target_path) throw PlacedReferenceError("placed reference target CELL disappeared during relocation");
  auto* target_children = detail::ensure_cell_children_group(
      *target_path->path.back(), detail::group_header(GroupType::cell_children, target_label),
      target_key, context, context);
  auto* target_bucket = detail::ensure_reference_bucket(
      *target_children, detail::group_header(wanted_bucket, target_label), context, context);
  target_bucket->children.emplace_back(std::move(moved));
  return commit(PlacedReferenceMoveStatus::relocated);
}

}  // namespace fo4::forms
