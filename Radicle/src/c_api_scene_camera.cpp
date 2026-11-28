#include "c_api_env_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/plugin/scene_writer.hpp"

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

extern "C" {

radicle_status radicle_plugin_read_scene_action_camera(const radicle_plugin* plugin, radicle_form_key key,
    size_t action, uint8_t* has_value, float* fov, float* rate) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (!plugin || !form_key || !has_value || !fov || !rate)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, scene and output pointers are required");
  *has_value = 0; *fov = 0; *rate = 0;
  return guarded("unknown exception while reading dialogue camera", [&] {
    try {
      const auto camera = fo4::read_scene_action_camera(plugin->value, *form_key, action);
      if (!camera) return fail(RADICLE_STATUS_NOT_FOUND, "scene or dialogue action not found");
      if (camera->value) { *has_value = 1; *fov = camera->value->fov; *rate = camera->value->rate; }
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_scene_action_camera(radicle_plugin* plugin, radicle_form_key key,
    size_t action, uint8_t has_value, float fov, float rate) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (!plugin || !form_key || has_value > 1)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, scene and valid camera presence are required");
  return guarded("unknown exception while setting dialogue camera", [&] {
    try {
      const auto value = has_value ? std::optional<fo4::SceneActionCamera>{fo4::SceneActionCamera{fov, rate}} : std::nullopt;
      switch (fo4::set_scene_action_camera(plugin->value, *form_key, action, value)) {
        case fo4::FormLinkFieldMutationStatus::record_missing:
        case fo4::FormLinkFieldMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "scene or supported dialogue action not found");
        case fo4::FormLinkFieldMutationStatus::record_mismatch:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a scene");
        default: return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}
