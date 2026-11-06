#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "fo4/c_api_cell_edit.h"
#include "fo4/forms/cell_edit.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

template <class T>
std::optional<T> optional_of(int32_t has, const T& value) {
  if (has == 0) return std::nullopt;
  return value;
}

std::optional<std::string> optional_text(const char* text) {
  if (text == nullptr) return std::nullopt;
  return std::string(text);
}

bool link_key(radicle_form_key key, std::optional<fo4::FormKey>& out) {
  out.reset();
  if (key.plugin == nullptr) return true;
  out = to_form_key(key);
  return out.has_value();
}

bool optional_link(int32_t has, radicle_form_key key, std::optional<std::optional<fo4::FormKey>>& out) {
  if (has == 0) return true;
  std::optional<fo4::FormKey> link;
  if (!link_key(key, link)) return false;
  out.emplace(link);
  return true;
}

fo4::forms::PositionRotation to_position(const float* position, const float* rotation) {
  return {position[0], position[1], position[2], rotation[0], rotation[1], rotation[2]};
}

std::array<std::uint8_t, 3> to_rgb(radicle_byte_color color) {
  return {color.red, color.green, color.blue};
}

bool to_native_placed(const radicle_placed_edit& in, fo4::forms::PlacedReferenceEdit& out) {
  if (in.has_transform != 0) out.transform = to_position(in.position, in.rotation);
  if (in.has_scale != 0) out.scale.emplace(optional_of(in.has_scale_value, in.scale));
  if (in.has_editor_id != 0) {
    if (in.editor_id == nullptr) return false;
    out.editor_id = std::string(in.editor_id);
  }
  if (in.has_record_flags != 0) out.record_flags = in.record_flags;
  if (in.has_comments != 0) out.comments.emplace(optional_text(in.comments));
  if (!optional_link(in.has_location_reference, in.location_reference, out.location_reference)) return false;
  if (in.has_ownership != 0) {
    fo4::forms::OwnershipEdit ownership;
    if (!link_key(in.owner, ownership.owner)) return false;
    ownership.no_crime = in.no_crime != 0;
    ownership.faction_rank = optional_of(in.has_faction_rank, in.faction_rank);
    out.ownership = ownership;
  }
  if (in.has_enable_parent != 0) {
    fo4::forms::EnableParentEdit enable_parent;
    if (!link_key(in.enable_parent, enable_parent.parent)) return false;
    enable_parent.flags = in.enable_parent_flags;
    out.enable_parent = enable_parent;
  }
  if (in.has_linked_references != 0) {
    if (in.linked_references == nullptr && in.linked_reference_count != 0) return false;
    std::vector<fo4::forms::LinkedReferenceEdit> links;
    for (size_t i = 0; i < in.linked_reference_count; ++i) {
      fo4::forms::LinkedReferenceEdit link;
      if (!link_key(in.linked_references[i].keyword, link.keyword)) return false;
      const auto reference = to_form_key(in.linked_references[i].reference);
      if (!reference) return false;
      link.reference = *reference;
      links.push_back(link);
    }
    out.linked_references = std::move(links);
  }
  if (in.has_teleport != 0) {
    const auto door = to_form_key(in.teleport_door);
    if (!door) return false;
    out.teleport = fo4::forms::TeleportEdit{*door, to_position(in.teleport_position, in.teleport_rotation)};
  }
  if (in.has_primitive != 0) {
    fo4::forms::PrimitiveEdit primitive;
    for (size_t i = 0; i < 3; ++i) primitive.bounds[i] = in.primitive_bounds[i];
    primitive.type = in.primitive_type;
    out.primitive = primitive;
  }
  return true;
}

fo4::forms::CellLightingEdit to_native_lighting(const radicle_cell_lighting_edit& in) {
  fo4::forms::CellLightingEdit out;
  out.ambient = optional_of(in.has_ambient, to_rgb(in.ambient));
  out.directional = optional_of(in.has_directional, to_rgb(in.directional));
  out.fog_near_color = optional_of(in.has_fog_near_color, to_rgb(in.fog_near_color));
  out.fog_near = optional_of(in.has_fog_near, in.fog_near);
  out.fog_far = optional_of(in.has_fog_far, in.fog_far);
  out.directional_rotation_xy = optional_of(in.has_directional_rotation_xy, in.directional_rotation_xy);
  out.directional_rotation_z = optional_of(in.has_directional_rotation_z, in.directional_rotation_z);
  out.directional_fade = optional_of(in.has_directional_fade, in.directional_fade);
  out.fog_clip_distance = optional_of(in.has_fog_clip_distance, in.fog_clip_distance);
  out.fog_power = optional_of(in.has_fog_power, in.fog_power);
  return out;
}

bool to_native_cell(const radicle_cell_edit& in, fo4::forms::CellEdit& out) {
  out.flags = optional_of(in.has_flags, in.flags);
  if (in.has_lighting != 0) out.lighting = to_native_lighting(in.lighting);
  if (!optional_link(in.has_lighting_template, in.lighting_template, out.lighting_template)) return false;
  out.water_height = optional_of(in.has_water_height, in.water_height);
  if (!optional_link(in.has_water, in.water, out.water)) return false;
  if (!optional_link(in.has_image_space, in.image_space, out.image_space)) return false;
  if (!optional_link(in.has_encounter_zone, in.encounter_zone, out.encounter_zone)) return false;
  if (!optional_link(in.has_location, in.location, out.location)) return false;
  if (!optional_link(in.has_acoustic_space, in.acoustic_space, out.acoustic_space)) return false;
  if (!optional_link(in.has_music_type, in.music_type, out.music_type)) return false;
  if (!optional_link(in.has_sky_weather_region, in.sky_weather_region, out.sky_weather_region)) return false;
  if (!optional_link(in.has_god_rays, in.god_rays, out.god_rays)) return false;
  if (!optional_link(in.has_lock_list, in.lock_list, out.lock_list)) return false;
  if (!optional_link(in.has_owner, in.owner, out.owner)) return false;
  out.owner_rank = optional_of(in.has_owner_rank, in.owner_rank);
  if (in.has_regions != 0) {
    if (in.regions == nullptr && in.region_count != 0) return false;
    std::vector<fo4::FormKey> regions;
    for (size_t i = 0; i < in.region_count; ++i) {
      const auto key = to_form_key(in.regions[i]);
      if (!key) return false;
      regions.push_back(*key);
    }
    out.regions = std::move(regions);
  }
  return true;
}

const char* borrowed_plugin_name(const radicle_env& env, const std::string& filename) {
  const auto index = env.value.find_plugin(filename);
  if (!index) return nullptr;
  return env.value.plugin(*index).key.filename().data();
}

radicle_status refused(radicle_status code, const std::string& message, const char* fallback) {
  return fail(code, message.empty() ? fallback : message.c_str());
}

radicle_status placed_edit_outcome(fo4::forms::PlacedReferenceEditStatus status, const std::string& message) {
  switch (status) {
    case fo4::forms::PlacedReferenceEditStatus::edited:
      return RADICLE_STATUS_OK;
    case fo4::forms::PlacedReferenceEditStatus::reference_missing:
      return refused(RADICLE_STATUS_NOT_FOUND, message, "no placed reference with that key");
    case fo4::forms::PlacedReferenceEditStatus::reference_deleted:
      return refused(RADICLE_STATUS_NOT_FOUND, message, "the placed reference is deleted");
    case fo4::forms::PlacedReferenceEditStatus::target_cell_missing:
      return refused(RADICLE_STATUS_NOT_FOUND, message, "the target cell is missing from the patch");
    case fo4::forms::PlacedReferenceEditStatus::field_absent:
      return refused(RADICLE_STATUS_INVALID_ARGUMENT, message, "the placed reference has no such field");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown placed reference edit status");
}

radicle_status duplicate_outcome(fo4::forms::DuplicateStatus status) {
  switch (status) {
    case fo4::forms::DuplicateStatus::duplicated:
      return RADICLE_STATUS_OK;
    case fo4::forms::DuplicateStatus::reference_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no placed reference with that key");
    case fo4::forms::DuplicateStatus::reference_deleted:
      return fail(RADICLE_STATUS_NOT_FOUND, "the placed reference is deleted");
    case fo4::forms::DuplicateStatus::parent_cell_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "the parent CELL is missing or deleted");
    case fo4::forms::DuplicateStatus::unsupported_signature:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the reference signature cannot be duplicated");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown duplicate status");
}

radicle_status delete_outcome(fo4::forms::DeleteStatus status) {
  switch (status) {
    case fo4::forms::DeleteStatus::deleted:
    case fo4::forms::DeleteStatus::marked_deleted:
      return RADICLE_STATUS_OK;
    case fo4::forms::DeleteStatus::referenced:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the record is still referenced by another record");
    case fo4::forms::DeleteStatus::reference_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no placed reference with that key");
    case fo4::forms::DeleteStatus::already_deleted:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the placed reference is already deleted in the patch");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown delete status");
}

radicle_status cell_override_outcome(fo4::forms::CellOverrideStatus status) {
  switch (status) {
    case fo4::forms::CellOverrideStatus::inserted:
    case fo4::forms::CellOverrideStatus::already_exists:
      return RADICLE_STATUS_OK;
    case fo4::forms::CellOverrideStatus::source_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "the source plugin has no record with that key");
    case fo4::forms::CellOverrideStatus::not_a_cell:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the key does not name a CELL");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown cell override status");
}

radicle_status cell_edit_outcome(fo4::forms::CellEditStatus status) {
  switch (status) {
    case fo4::forms::CellEditStatus::edited:
      return RADICLE_STATUS_OK;
    case fo4::forms::CellEditStatus::cell_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no CELL with that key");
    case fo4::forms::CellEditStatus::cell_deleted:
      return fail(RADICLE_STATUS_NOT_FOUND, "the winning CELL is deleted");
    case fo4::forms::CellEditStatus::not_a_cell:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the key does not name a CELL");
    case fo4::forms::CellEditStatus::target_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "a link target is not in the environment");
    case fo4::forms::CellEditStatus::target_type_not_allowed:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a link target has a record type this field does not accept");
    case fo4::forms::CellEditStatus::target_not_master:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a link target must come from a master of the patch");
    case fo4::forms::CellEditStatus::interior_flag_locked:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the interior flag of this CELL cannot change");
    case fo4::forms::CellEditStatus::invalid_value:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a CELL edit value is out of range");
    case fo4::forms::CellEditStatus::malformed:
      return fail(RADICLE_STATUS_PARSE_ERROR, "the CELL record is malformed");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown CELL edit status");
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_edit_placed_reference(radicle_plugin* patch,
                                                    const radicle_env* env,
                                                    radicle_form_key reference,
                                                    const radicle_placed_edit* edit,
                                                    radicle_placed_edit_result* out_result) {
  clear_error();
  if (patch == nullptr || env == nullptr || edit == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "patch, env, edit and out_result are required");
  }
  return guarded("unknown exception while editing a placed reference", [&] {
    const auto key = to_form_key(reference);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid reference key is required");
    fo4::forms::PlacedReferenceEdit native;
    if (!to_native_placed(*edit, native)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a placed reference edit field is invalid");
    }
    const auto result = fo4::forms::edit_placed_reference(patch->value, env->value, *key, native);
    out_result->status = static_cast<radicle_placed_edit_status>(result.status);
    out_result->copied_from = borrowed_plugin_name(*env, result.copied_from);
    out_result->relocated = result.relocated ? 1 : 0;
    return placed_edit_outcome(result.status, result.message);
  });
}

radicle_status radicle_plugin_duplicate_placed_reference(radicle_plugin* patch,
                                                         const radicle_env* env,
                                                         radicle_form_key reference,
                                                         const radicle_duplicate_options* options,
                                                         radicle_duplicate_status* out_status,
                                                         radicle_form_key* out_new) {
  clear_error();
  if (out_new != nullptr) *out_new = {nullptr, 0};
  if (patch == nullptr || env == nullptr || out_status == nullptr || out_new == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "patch, env, out_status and out_new are required");
  }
  return guarded("unknown exception while duplicating a placed reference", [&] {
    const auto key = to_form_key(reference);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid reference key is required");
    fo4::forms::DuplicateOptions native;
    if (options != nullptr) {
      native.offset = to_position(options->offset_position, options->offset_rotation);
      native.keep_editor_id = options->keep_editor_id != 0;
    }
    const auto result = fo4::forms::duplicate_placed_reference(patch->value, env->value, *key, native);
    *out_status = static_cast<radicle_duplicate_status>(result.status);
    if (result.status != fo4::forms::DuplicateStatus::duplicated) return duplicate_outcome(result.status);
    *out_new = to_c_key(*env, result.new_reference);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_delete_placed_reference(radicle_plugin* patch,
                                                      const radicle_env* env,
                                                      radicle_form_key reference,
                                                      radicle_delete_status* out_status) {
  clear_error();
  if (patch == nullptr || env == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "patch, env and out_status are required");
  }
  return guarded("unknown exception while deleting a placed reference", [&] {
    const auto key = to_form_key(reference);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid reference key is required");
    const auto result = fo4::forms::delete_placed_reference(patch->value, env->value, *key);
    *out_status = static_cast<radicle_delete_status>(result.status);
    return delete_outcome(result.status);
  });
}

radicle_status radicle_plugin_copy_cell_override(radicle_plugin* patch,
                                                 const radicle_plugin* source,
                                                 radicle_form_key cell,
                                                 radicle_cell_override_status* out_status) {
  clear_error();
  if (patch == nullptr || source == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "patch, source and out_status are required");
  }
  return guarded("unknown exception while copying a CELL override", [&] {
    const auto key = to_form_key(cell);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid CELL key is required");
    if (patch == source) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source must be a different plugin from patch");
    const auto status = fo4::forms::copy_cell_as_override(patch->value, source->value, *key);
    *out_status = static_cast<radicle_cell_override_status>(status);
    return cell_override_outcome(status);
  });
}

radicle_status radicle_plugin_edit_cell(radicle_plugin* patch,
                                        const radicle_env* env,
                                        radicle_form_key cell,
                                        const radicle_cell_edit* edit,
                                        radicle_cell_edit_status* out_status) {
  clear_error();
  if (patch == nullptr || env == nullptr || edit == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "patch, env, edit and out_status are required");
  }
  return guarded("unknown exception while editing a CELL", [&] {
    const auto key = to_form_key(cell);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid CELL key is required");
    fo4::forms::CellEdit native;
    if (!to_native_cell(*edit, native)) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a CELL edit field is invalid");
    const auto result = fo4::forms::edit_cell(patch->value, env->value, *key, native);
    *out_status = static_cast<radicle_cell_edit_status>(result.status);
    return cell_edit_outcome(result.status);
  });
}

}  // extern "C"
