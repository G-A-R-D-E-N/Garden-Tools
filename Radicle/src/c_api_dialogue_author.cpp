#include "c_api_env_internal.hpp"
#include "c_api_forms_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/forms/dialogue.hpp"
#include "fo4/text/encoding.hpp"

#include <optional>
#include <string>
#include <utility>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;
using Status = fo4::forms::DialogueAuthorStatus;

radicle_status status_result(Status status) {
  switch (status) {
    case Status::ok:
      return RADICLE_STATUS_OK;
    case Status::quest_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no QUST record with that form key is in the plugin");
    case Status::branch_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no DLBR record with that form key is in the plugin");
    case Status::topic_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no DIAL record with that form key is in the plugin");
    case Status::info_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "no INFO record with that form key is in the plugin");
    case Status::not_local:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "dialogue records must be local to this plugin, not owned by a master");
    case Status::invalid_argument:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "dialogue argument is invalid: a string holds an embedded NUL byte, or a subtype name is not 4 characters");
    case Status::localized_unsupported:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "the plugin is localized, so topic names and response text would need string ids this write does not produce");
    case Status::undecodable:
      return fail(RADICLE_STATUS_PARSE_ERROR, "the record is present but cannot be decoded");
    case Status::structure_invalid:
      return fail(RADICLE_STATUS_PARSE_ERROR,
                  "the dialogue group layout does not match what this write expects, or a form key does not resolve through the masters");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown dialogue authoring status");
}

bool valid_text(const char* text) {
  return text == nullptr || (fo4::text::is_valid_utf8(text) && text_representable(text));
}

std::string text_or_empty(const char* text) {
  return text == nullptr ? std::string{} : std::string(text);
}

std::optional<std::string> subtype_bytes(const char* text) {
  if (text == nullptr) return std::string{};
  if (!fo4::text::is_valid_utf8(text)) return std::nullopt;
  return fo4::text::utf8_to_windows1252(text);
}

bool optional_key(int32_t present, radicle_form_key key, std::optional<fo4::FormKey>& out) {
  if (present == 0) return true;
  out = to_form_key(key);
  return out.has_value();
}

radicle_status native_info(const radicle_dialogue_info_spec& spec, fo4::forms::DialogueInfoSpec& out) {
  if ((spec.responses == nullptr && spec.response_count != 0) ||
      (spec.conditions == nullptr && spec.condition_count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "response and condition arrays are required when their counts are not zero");
  }
  out.flags = spec.flags;
  out.reset_hours = spec.reset_time_encoded;
  for (size_t i = 0; i < spec.response_count; ++i) {
    const auto& row = spec.responses[i];
    if (!valid_text(row.text) || !valid_text(row.script_notes)) return bad_text();
    fo4::forms::DialogueResponseSpec response;
    response.text = text_or_empty(row.text);
    response.script_notes = text_or_empty(row.script_notes);
    if (!optional_key(row.has_emotion, row.emotion, response.emotion) ||
        !optional_key(row.has_sound, row.sound, response.sound)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "response emotion or sound is not a valid form key");
    }
    response.number = row.number;
    response.interrupt_percent = row.interrupt_percent;
    response.camera_target_alias = row.camera_target_alias;
    response.camera_location_alias = row.camera_location_alias;
    out.responses.push_back(std::move(response));
  }
  for (size_t i = 0; i < spec.condition_count; ++i) {
    fo4::CtdaCondition condition;
    if (!to_ctda(spec.conditions[i], condition)) return bad_text();
    out.conditions.push_back(std::move(condition));
  }
  return RADICLE_STATUS_OK;
}

radicle_status created(fo4::Plugin& plugin, const fo4::forms::DialogueCreateResult& result,
                       radicle_form_key* out_key) {
  if (result.status != Status::ok) return status_result(result.status);
  *out_key = {plugin.key.filename().data(), result.key.local().value()};
  return RADICLE_STATUS_OK;
}

}

extern "C" {

radicle_status radicle_plugin_create_dialogue_branch(radicle_plugin* plugin, radicle_form_key quest,
                                                     const radicle_dialogue_branch_spec* spec,
                                                     radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  const auto quest_key = to_form_key(quest);
  if (plugin == nullptr || spec == nullptr || out_key == nullptr || !quest_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid quest key, spec and out_key are required");
  }
  if (!valid_text(spec->editor_id)) return bad_text();
  return guarded("unknown exception while creating a dialogue branch", [&] {
    fo4::forms::DialogueBranchSpec native;
    native.editor_id = text_or_empty(spec->editor_id);
    native.category = spec->category;
    native.flags = spec->flags;
    return created(plugin->value, fo4::forms::create_dialogue_branch(plugin->value, *quest_key, native), out_key);
  });
}

radicle_status radicle_plugin_create_dialogue_topic(radicle_plugin* plugin, radicle_form_key quest,
                                                    const radicle_dialogue_topic_spec* spec,
                                                    radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  const auto quest_key = to_form_key(quest);
  if (plugin == nullptr || spec == nullptr || out_key == nullptr || !quest_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid quest key, spec and out_key are required");
  }
  if (!valid_text(spec->editor_id) || !valid_text(spec->name)) return bad_text();
  const auto subtype = subtype_bytes(spec->subtype_name);
  if (!subtype) return bad_text();
  return guarded("unknown exception while creating a dialogue topic", [&] {
    fo4::forms::DialogueTopicSpec native;
    native.editor_id = text_or_empty(spec->editor_id);
    native.name = text_or_empty(spec->name);
    native.priority = spec->priority;
    native.topic_flags = spec->topic_flags;
    native.category = spec->category;
    native.subtype = spec->subtype;
    native.subtype_name = *subtype;
    if (!optional_key(spec->has_branch, spec->branch, native.branch) ||
        !optional_key(spec->has_keyword, spec->keyword, native.keyword)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "topic branch or keyword is not a valid form key");
    }
    return created(plugin->value, fo4::forms::create_dialogue_topic(plugin->value, *quest_key, native), out_key);
  });
}

radicle_status radicle_plugin_set_dialogue_branch_start(radicle_plugin* plugin, radicle_form_key branch,
                                                        radicle_form_key topic) {
  clear_error();
  const auto branch_key = to_form_key(branch);
  const auto topic_key = to_form_key(topic);
  if (plugin == nullptr || !branch_key || !topic_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid branch key and a valid topic key are required");
  }
  return guarded("unknown exception while setting a dialogue branch start", [&] {
    return status_result(fo4::forms::set_dialogue_branch_start(plugin->value, *branch_key, *topic_key));
  });
}

radicle_status radicle_plugin_create_dialogue_info(radicle_plugin* plugin, radicle_form_key topic,
                                                   const radicle_dialogue_info_spec* spec,
                                                   radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  const auto topic_key = to_form_key(topic);
  if (plugin == nullptr || spec == nullptr || out_key == nullptr || !topic_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid topic key, spec and out_key are required");
  }
  return guarded("unknown exception while creating a dialogue INFO", [&] {
    fo4::forms::DialogueInfoSpec native;
    if (const auto status = native_info(*spec, native); status != RADICLE_STATUS_OK) return status;
    return created(plugin->value, fo4::forms::create_dialogue_info(plugin->value, *topic_key, native), out_key);
  });
}

radicle_status radicle_plugin_update_dialogue_info(radicle_plugin* plugin, radicle_form_key info,
                                                   const radicle_dialogue_info_spec* spec) {
  clear_error();
  const auto info_key = to_form_key(info);
  if (plugin == nullptr || spec == nullptr || !info_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid INFO key and spec are required");
  }
  return guarded("unknown exception while updating a dialogue INFO", [&] {
    fo4::forms::DialogueInfoSpec native;
    if (const auto status = native_info(*spec, native); status != RADICLE_STATUS_OK) return status;
    return status_result(fo4::forms::update_dialogue_info(plugin->value, *info_key, native));
  });
}

radicle_status radicle_plugin_remove_dialogue_info(radicle_plugin* plugin, radicle_form_key info) {
  clear_error();
  const auto info_key = to_form_key(info);
  if (plugin == nullptr || !info_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid INFO key are required");
  }
  return guarded("unknown exception while removing a dialogue INFO", [&] {
    return status_result(fo4::forms::remove_dialogue_info(plugin->value, *info_key));
  });
}

}
