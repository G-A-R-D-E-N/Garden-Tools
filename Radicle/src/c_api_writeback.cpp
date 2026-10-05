#include "c_api_env_internal.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/writer.hpp"

#include <memory>
#include <optional>
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

radicle_status radicle_plugin_set_placed_transform(radicle_plugin* plugin,
                                                   radicle_form_key reference,
                                                   const float position[3],
                                                   const float rotation[3]) {
  clear_error();
  const auto key = to_form_key(reference);
  if (plugin == nullptr || !key || position == nullptr || rotation == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid reference, position and rotation are required");
  }
  return guarded("unknown exception while setting transform", [&] {
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

radicle_status radicle_plugin_set_placed_scale(radicle_plugin* plugin,
                                               radicle_form_key reference,
                                               int32_t has_scale,
                                               float scale) {
  clear_error();
  const auto key = to_form_key(reference);
  if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
  return guarded("unknown exception while setting scale", [&] {
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
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || path == nullptr || value == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and value are required");
  }
  return guarded("unknown exception while setting scalar field", [&] {
    const auto field = fo4::schema::scalar_field_descriptor(path);
    if (!field) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown scalar field path");
    try {
      switch (fo4::set_record_scalar(plugin->value, *form_key, *field, value)) {
        case fo4::ScalarMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::ScalarMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "record has no such subrecord, or the field is outside its supported payload layout");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
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
