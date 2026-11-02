#include "c_api_env_internal.hpp"
#include "fo4/forms/copy_as_new.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/writer.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status placed_error(const fo4::forms::PlacedReferenceError& error) {
  return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_create(const char* filename, radicle_plugin** out_plugin) {
  clear_error();
  if (filename == nullptr || *filename == '\0' || out_plugin == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "filename and out_plugin are required");
  }
  *out_plugin = nullptr;
  return guarded("unknown exception while creating plugin", [&] {
    auto plugin = std::make_unique<radicle_plugin>(radicle_plugin{fo4::make_empty_plugin(filename)});
    *out_plugin = plugin.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_copy_placed_ref_override(radicle_plugin* plugin,
                                                       const radicle_env* env,
                                                       const radicle_record* record,
                                                       int32_t overwrite,
                                                       radicle_placed_copy_status* out_status) {
  clear_error();
  if (plugin == nullptr || env == nullptr || record == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, record and out_status are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  const auto& ref = *to_ref(record);
  return guarded("unknown exception while copying placed reference", [&] {
    try {
      const auto status = fo4::forms::copy_placed_reference_as_override(
          plugin->value, env->value.plugin(ref.plugin), ref.record->form_key, overwrite != 0);
      switch (status) {
        case fo4::forms::PlacedReferenceCopyStatus::inserted:
          *out_status = RADICLE_PLACED_COPY_INSERTED;
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyStatus::replaced:
          *out_status = RADICLE_PLACED_COPY_REPLACED;
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyStatus::already_exists:
          *out_status = RADICLE_PLACED_COPY_ALREADY_EXISTS;
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyStatus::source_missing:
          break;
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_copy_placed_ref_new(radicle_plugin* plugin,
                                                  const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_form_key* out_key) {
  clear_error();
  if (plugin == nullptr || env == nullptr || record == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, record and out_key are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_key = {nullptr, 0};
  const auto& ref = *to_ref(record);
  return guarded("unknown exception while copying placed reference", [&] {
    try {
      const auto result = fo4::forms::copy_placed_reference_as_new(
          plugin->value, env->value.plugin(ref.plugin), ref.record->form_key);
      switch (result.status) {
        case fo4::forms::PlacedReferenceCopyNewStatus::created:
          *out_key = {plugin->value.key.filename().data(), result.new_reference.local().value()};
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyNewStatus::unsupported_signature:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a placed reference");
        case fo4::forms::PlacedReferenceCopyNewStatus::parent_cell_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin does not contain the reference's parent CELL");
        case fo4::forms::PlacedReferenceCopyNewStatus::source_missing:
          break;
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_copy_record_as_new(radicle_plugin* destination,
                                                 const radicle_plugin* source,
                                                 radicle_form_key record,
                                                 const char* editor_id,
                                                 const char* remove_prefix,
                                                 const char* remove_suffix,
                                                 const char* prefix,
                                                 const char* suffix,
                                                 radicle_form_key* out_key) {
  clear_error();
  const auto key = to_form_key(record);
  if (destination == nullptr || source == nullptr || !key || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "destination, source, a valid record and out_key are required");
  }
  *out_key = {nullptr, 0};
  return guarded("unknown exception while copying record", [&] {
    fo4::forms::RecordCopyNewOptions options;
    if (editor_id != nullptr) options.editor_id = editor_id;
    if (remove_prefix != nullptr) options.editor_id_remove_prefix = remove_prefix;
    if (remove_suffix != nullptr) options.editor_id_remove_suffix = remove_suffix;
    if (prefix != nullptr) options.editor_id_prefix = prefix;
    if (suffix != nullptr) options.editor_id_suffix = suffix;
    try {
      auto next = destination->value;
      const auto result = fo4::forms::copy_record_as_new(next, source->value, *key, options);
      switch (result.status) {
        case fo4::forms::RecordCopyNewStatus::created:
          destination->value = std::move(next);
          *out_key = {destination->value.key.filename().data(), result.new_record.local().value()};
          return RADICLE_STATUS_OK;
        case fo4::forms::RecordCopyNewStatus::unsupported_signature:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type is not copied as new");
        case fo4::forms::RecordCopyNewStatus::parent_cell_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin does not contain the reference's parent CELL");
        case fo4::forms::RecordCopyNewStatus::parent_dial_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin does not contain the INFO's parent DIAL");
        case fo4::forms::RecordCopyNewStatus::source_missing:
          break;
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    } catch (const fo4::forms::InfoCopyError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_copy_record_override(radicle_plugin* destination,
                                                   const radicle_plugin* source,
                                                   radicle_form_key record,
                                                   int32_t overwrite,
                                                   radicle_record_copy_status* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (destination == nullptr || source == nullptr || !key || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "destination, source, a valid record and out_status are required");
  }
  *out_status = RADICLE_RECORD_COPY_INSERTED;
  return guarded("unknown exception while copying record as an override", [&] {
    auto next = destination->value;
    const auto status = fo4::copy_record_as_override(next, source->value, *key, overwrite != 0);
    switch (status) {
      case fo4::RecordCopyStatus::inserted:
        *out_status = RADICLE_RECORD_COPY_INSERTED;
        break;
      case fo4::RecordCopyStatus::replaced:
        *out_status = RADICLE_RECORD_COPY_REPLACED;
        break;
      case fo4::RecordCopyStatus::already_exists:
        *out_status = RADICLE_RECORD_COPY_ALREADY_EXISTS;
        break;
      case fo4::RecordCopyStatus::source_missing:
        return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
      case fo4::RecordCopyStatus::source_containment_unsupported:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                    "source record's containment cannot be reproduced");
    }
    destination->value = std::move(next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_placed_transform(radicle_plugin* plugin,
                                                   radicle_form_key reference,
                                                   const float position[3],
                                                   const float rotation[3]) {
  clear_error();
  return guarded("unknown exception while setting transform", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key || position == nullptr || rotation == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid reference, position and rotation are required");
    }
    try {
      const fo4::forms::PositionRotation transform{position[0], position[1], position[2],
                                                   rotation[0], rotation[1], rotation[2]};
      if (!fo4::forms::set_placed_reference_transform(plugin->value, *key, transform)) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_move_placed_ref(radicle_plugin* plugin,
                                              radicle_form_key reference,
                                              const float position[3],
                                              const float rotation[3],
                                              radicle_placed_move_status* out_status) {
  clear_error();
  return guarded("unknown exception while moving placed reference", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key || position == nullptr || rotation == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid reference, position and rotation are required");
    }
    try {
      const fo4::forms::PositionRotation transform{position[0], position[1], position[2],
                                                   rotation[0], rotation[1], rotation[2]};
      const auto status = fo4::forms::move_placed_reference(plugin->value, *key, transform);
      if (status == fo4::forms::PlacedReferenceMoveStatus::reference_missing) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      }
      if (status == fo4::forms::PlacedReferenceMoveStatus::target_cell_missing) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no exterior CELL at the new position; create it first");
      }
      if (out_status != nullptr) {
        *out_status = status == fo4::forms::PlacedReferenceMoveStatus::moved ? RADICLE_PLACED_MOVE_MOVED
                                                                             : RADICLE_PLACED_MOVE_RELOCATED;
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_set_placed_scale(radicle_plugin* plugin,
                                               radicle_form_key reference,
                                               int32_t has_scale,
                                               float scale) {
  clear_error();
  return guarded("unknown exception while setting scale", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
    try {
      const auto value = has_scale != 0 ? std::optional<float>{scale} : std::nullopt;
      if (!fo4::forms::set_placed_reference_scale(plugin->value, *key, value)) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_set_record_scalar(radicle_plugin* plugin,
                                                radicle_form_key key,
                                                const char* path,
                                                const char* value) {
  return radicle_plugin_set_record_scalar_at(plugin, key, path, 0, value);
}

radicle_status radicle_plugin_sort_masters(radicle_plugin* plugin, const radicle_env* env) {
  clear_error();
  if (plugin == nullptr || env == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and env are required");
  return guarded("unknown exception while sorting masters", [&] {
    std::vector<fo4::PluginKey> order;
    for (const auto& info : env->value.plugins()) order.push_back(info.key);
    try {
      plugin->value = fo4::sort_plugin_masters(plugin->value, order);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
