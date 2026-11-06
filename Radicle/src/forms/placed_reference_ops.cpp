#include "fo4/forms/cell_edit.hpp"

#include "fo4/forms/cell.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_flags.hpp"
#include "placed_reference_copy_internal.hpp"

#include <optional>
#include <string>
#include <utility>

namespace fo4::forms {
namespace {

void require_applied(bool applied) {
  if (!applied) throw PlacedReferenceError("placed reference disappeared during edit");
}

PlacedReferenceEditStatus present_status(PresentFieldEdit state) {
  if (state == PresentFieldEdit::updated) return PlacedReferenceEditStatus::edited;
  return state == PresentFieldEdit::record_missing ? PlacedReferenceEditStatus::reference_missing
                                                   : PlacedReferenceEditStatus::field_absent;
}

void ensure_move_target(Plugin& next,
                        const fo4::env::Environment& env,
                        const fo4::env::RecordRef& winner,
                        const PositionRotation& transform) {
  const auto world = env.world_of(winner);
  if (!world.has_value()) return;
  if (winner.record->header.flags.is_persistent()) {
    const auto* top = env.worldspace_top_cell(*world);
    if (top == nullptr) return;
    const auto metadata = env.metadata(*top);
    if (!metadata.deleted) copy_cell_as_override(next, env.plugin(top->plugin), metadata.form_key);
    return;
  }
  if (!detail::exterior_grid_in_range(transform.x) || !detail::exterior_grid_in_range(transform.y)) {
    return;
  }
  const auto x = detail::grid_coordinate(transform.x);
  const auto y = detail::grid_coordinate(transform.y);
  for (const auto* cell : env.worldspace_cells(*world)) {
    const auto grid = cell_grid(*cell->record);
    if (!grid.has_value() || grid->first != x || grid->second != y) continue;
    const auto metadata = env.metadata(*cell);
    if (!metadata.deleted) copy_cell_as_override(next, env.plugin(cell->plugin), metadata.form_key);
    return;
  }
}

}  // namespace

EnsureOverrideResult ensure_reference_override(Plugin& patch,
                                               const fo4::env::Environment& env,
                                               const FormKey& reference) {
  if (find_record(patch, reference) != nullptr) {
    return {EnsureOverrideStatus::present, {}, {}};
  }
  const auto* winner = env.winner(reference);
  if (winner == nullptr) {
    return {EnsureOverrideStatus::reference_missing, {},
            "Placed reference is not present in the loaded environment: " + reference.to_string()};
  }
  const auto source_name = std::string(env.plugin(winner->plugin).key.filename());
  if (winner->record->header.flags.is_deleted()) {
    return {EnsureOverrideStatus::reference_deleted, {},
            "The winning version of " + reference.to_string() + " is deleted in " + source_name};
  }
  const auto copied = copy_placed_reference_as_override(patch, env.plugin(winner->plugin), reference, false);
  if (copied == PlacedReferenceCopyStatus::source_missing) {
    return {EnsureOverrideStatus::reference_missing, {},
            "Winning source no longer contains placed reference " + reference.to_string()};
  }
  if (copied == PlacedReferenceCopyStatus::already_exists) {
    return {EnsureOverrideStatus::present, {}, {}};
  }
  return {EnsureOverrideStatus::copied, source_name, {}};
}

PlacedReferenceEditResult edit_placed_reference(Plugin& patch,
                                                const fo4::env::Environment& env,
                                                const FormKey& reference,
                                                const PlacedReferenceEdit& edit) {
  auto next = patch;
  const auto ensured = ensure_reference_override(next, env, reference);
  if (ensured.status == EnsureOverrideStatus::reference_missing) {
    return {PlacedReferenceEditStatus::reference_missing, {}, false, ensured.message};
  }
  if (ensured.status == EnsureOverrideStatus::reference_deleted) {
    return {PlacedReferenceEditStatus::reference_deleted, {}, false, ensured.message};
  }
  const auto& copied_from = ensured.copied_from;
  const auto failed = [&](PlacedReferenceEditStatus status) {
    return PlacedReferenceEditResult{status, copied_from, false, {}};
  };

  bool relocated = false;
  if (edit.transform.has_value()) {
    auto moved = move_placed_reference(next, reference, *edit.transform);
    if (moved == PlacedReferenceMoveStatus::target_cell_missing) {
      if (const auto* winner = env.winner(reference); winner != nullptr) {
        ensure_move_target(next, env, *winner, *edit.transform);
      }
      moved = move_placed_reference(next, reference, *edit.transform);
    }
    if (moved == PlacedReferenceMoveStatus::reference_missing) {
      return failed(PlacedReferenceEditStatus::reference_missing);
    }
    if (moved == PlacedReferenceMoveStatus::target_cell_missing) {
      return failed(PlacedReferenceEditStatus::target_cell_missing);
    }
    relocated = moved == PlacedReferenceMoveStatus::relocated;
  }
  if (edit.scale.has_value()) {
    require_applied(set_placed_reference_scale(next, reference, *edit.scale));
  }
  if (edit.editor_id.has_value()) {
    require_applied(set_placed_reference_editor_id(next, reference, *edit.editor_id));
  }
  if (edit.record_flags.has_value()) {
    require_applied(set_placed_reference_record_flags(next, reference, *edit.record_flags));
  }
  if (edit.comments.has_value()) {
    require_applied(set_placed_reference_comments(next, reference, *edit.comments));
  }
  if (edit.location_reference.has_value()) {
    require_applied(set_placed_reference_location_reference(next, reference, *edit.location_reference));
  }
  if (edit.ownership.has_value()) {
    const auto& ownership = *edit.ownership;
    require_applied(set_placed_reference_ownership(next, reference, ownership.owner,
                                                   ownership.no_crime, ownership.faction_rank));
  }
  if (edit.enable_parent.has_value()) {
    const auto& enable_parent = *edit.enable_parent;
    require_applied(set_placed_reference_enable_parent(next, reference, enable_parent.parent,
                                                       enable_parent.flags));
  }
  if (edit.linked_references.has_value()) {
    require_applied(set_placed_reference_linked_references(next, reference, *edit.linked_references));
  }
  if (edit.teleport.has_value()) {
    const auto state = set_placed_reference_teleport(next, reference, edit.teleport->door,
                                                     edit.teleport->destination);
    if (state != PresentFieldEdit::updated) return failed(present_status(state));
  }
  if (edit.primitive.has_value()) {
    const auto state = set_placed_reference_primitive(next, reference, edit.primitive->bounds,
                                                      edit.primitive->type);
    if (state != PresentFieldEdit::updated) return failed(present_status(state));
  }

  next.original_range.clear();
  patch = std::move(next);
  return {PlacedReferenceEditStatus::edited, copied_from, relocated, {}};
}

DuplicateResult duplicate_placed_reference(Plugin& patch,
                                           const fo4::env::Environment& env,
                                           const FormKey& reference,
                                           const DuplicateOptions& options) {
  const auto* winner = env.winner(reference);
  if (winner == nullptr) return {DuplicateStatus::reference_missing, {}};
  if (winner->record->header.flags.is_deleted()) return {DuplicateStatus::reference_deleted, {}};
  const auto cell = env.cell_of(*winner);
  if (!cell.has_value()) return {DuplicateStatus::parent_cell_missing, {}};
  const auto* cell_winner = env.winner(*cell);
  if (cell_winner == nullptr || cell_winner->record->header.flags.is_deleted()) {
    return {DuplicateStatus::parent_cell_missing, {}};
  }

  const auto patch_copy = find_record(patch, reference) != nullptr ? std::optional<Plugin>{patch} : std::nullopt;
  const auto& source = patch_copy ? *patch_copy : env.plugin(winner->plugin);
  auto next = patch;
  copy_cell_as_override(next, env.plugin(cell_winner->plugin), *cell);
  const auto copied = copy_placed_reference_as_new(next, source, reference);
  if (copied.status == PlacedReferenceCopyNewStatus::source_missing) {
    return {DuplicateStatus::reference_missing, {}};
  }
  if (copied.status == PlacedReferenceCopyNewStatus::unsupported_signature) {
    return {DuplicateStatus::unsupported_signature, {}};
  }
  if (copied.status == PlacedReferenceCopyNewStatus::parent_cell_missing) {
    return {DuplicateStatus::parent_cell_missing, {}};
  }

  const auto new_key = copied.new_reference;
  if (!options.keep_editor_id) require_applied(set_placed_reference_editor_id(next, new_key, ""));
  const auto view = decode_placed_reference(next, *find_record(next, new_key));
  if (!view.has_value()) throw PlacedReferenceError("duplicated placed reference is malformed");
  auto transform = view->transform;
  transform.x += options.offset.x;
  transform.y += options.offset.y;
  transform.z += options.offset.z;
  require_applied(set_placed_reference_transform(next, new_key, transform));

  next.original_range.clear();
  patch = std::move(next);
  return {DuplicateStatus::duplicated, new_key};
}

DeleteResult delete_placed_reference(Plugin& patch,
                                     const fo4::env::Environment& env,
                                     const FormKey& reference) {
  if (reference.owner() == patch.key) {
    const auto removed = delete_local_record(patch, reference);
    if (removed == RecordDeleteStatus::deleted) return {DeleteStatus::deleted};
    if (removed == RecordDeleteStatus::referenced) return {DeleteStatus::referenced};
    return {DeleteStatus::reference_missing};
  }

  auto next = patch;
  const auto ensured = ensure_reference_override(next, env, reference);
  if (ensured.status == EnsureOverrideStatus::reference_missing) return {DeleteStatus::reference_missing};
  if (ensured.status == EnsureOverrideStatus::reference_deleted) return {DeleteStatus::already_deleted};
  const auto marked = mark_override_deleted(next, reference);
  if (marked == MarkDeletedStatus::already_deleted) return {DeleteStatus::already_deleted};
  if (marked != MarkDeletedStatus::marked) return {DeleteStatus::reference_missing};
  next.original_range.clear();
  patch = std::move(next);
  return {DeleteStatus::marked_deleted};
}

}  // namespace fo4::forms
