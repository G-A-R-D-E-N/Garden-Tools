#include "c_api_env_internal.hpp"
#include "c_api_forms_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/forms/scene.hpp"
#include "fo4/text/encoding.hpp"

#include <string>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;
using Status = fo4::forms::SceneAuthorStatus;

radicle_status status_result(Status status) {
  switch (status) {
    case Status::ok:
      return RADICLE_STATUS_OK;
    case Status::quest_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no QUST record with that form key is in the plugin");
    case Status::not_local:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "scenes must be created on a quest local to this plugin, not owned by a master");
    case Status::invalid_argument:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "scene argument is invalid: the editor id holds an embedded NUL byte");
    case Status::undecodable:
      return fail(RADICLE_STATUS_PARSE_ERROR, "the record is present but cannot be decoded");
    case Status::structure_invalid:
      return fail(RADICLE_STATUS_PARSE_ERROR,
                  "the quest group layout does not match what this write expects, or a form key does not resolve through the masters");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown scene authoring status");
}

bool valid_text(const char* text) {
  return text == nullptr || (fo4::text::is_valid_utf8(text) && text_representable(text));
}

std::string text_or_empty(const char* text) {
  return text == nullptr ? std::string{} : std::string(text);
}

radicle_status created(fo4::Plugin& plugin, const fo4::forms::SceneCreateResult& result,
                       radicle_form_key* out_key) {
  if (result.status != Status::ok) return status_result(result.status);
  *out_key = {plugin.key.filename().data(), result.key.local().value()};
  return RADICLE_STATUS_OK;
}

}

extern "C" {

radicle_status radicle_plugin_create_scene(radicle_plugin* plugin, radicle_form_key quest,
                                           const radicle_scene_spec* spec, radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  const auto quest_key = to_form_key(quest);
  if (plugin == nullptr || spec == nullptr || out_key == nullptr || !quest_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid quest key, spec and out_key are required");
  }
  if (!valid_text(spec->editor_id)) return bad_text();
  return guarded("unknown exception while creating a scene", [&] {
    fo4::forms::SceneSpec native;
    native.editor_id = text_or_empty(spec->editor_id);
    native.flags = spec->flags;
    native.index = spec->index;
    return created(plugin->value, fo4::forms::create_scene(plugin->value, *quest_key, native), out_key);
  });
}

}
