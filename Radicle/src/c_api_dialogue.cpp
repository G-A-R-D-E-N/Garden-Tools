#include "c_api_env_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/environment/dialogue.hpp"
#include "fo4/plugin/scene_writer.hpp"
#include "fo4/text/encoding.hpp"

#include <deque>
#include <optional>
#include <memory>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using Conditions = std::vector<radicle_dialogue_condition>;

std::optional<fo4::ScenePart> to_scene_part(radicle_scene_part part) {
  switch (part) {
    case RADICLE_SCENE_PART_PHASE: return fo4::ScenePart::phase;
    case RADICLE_SCENE_PART_ACTOR: return fo4::ScenePart::actor;
    case RADICLE_SCENE_PART_ACTION: return fo4::ScenePart::action;
  }
  return std::nullopt;
}

struct InfoHolder {
  radicle_dialogue_info view{};
  std::vector<radicle_dialogue_response> responses;
  Conditions conditions;
  std::deque<std::string> strings;
};

struct SceneHolder {
  radicle_scene_info view{};
  std::vector<radicle_scene_phase> phases;
  std::vector<radicle_scene_actor> actors;
  std::vector<radicle_scene_action> actions;
  std::vector<Conditions> starts, completions;
  std::vector<std::vector<radicle_scene_payload>> payloads;
  Conditions conditions;
  std::deque<std::string> strings;
};

radicle_dialogue_text text(const radicle_env& env, const fo4::env::RecordName& name) {
  return {static_cast<radicle_name_kind>(name.kind),
          name.kind == fo4::env::NameKind::inline_text ? stored_text(env, name.text) : nullptr, name.string_id};
}

const char* condition_text(std::deque<std::string>& strings, const std::optional<std::string>& value) {
  if (!value) return nullptr;
  strings.push_back(fo4::text::windows1252_to_utf8(*value));
  return strings.back().c_str();
}

Conditions conditions(std::deque<std::string>& strings, const std::vector<fo4::CtdaCondition>& source) {
  Conditions out;
  for (const auto& c : source) out.push_back({c.flags_byte, c.comparison_raw, c.function, c.unknown2,
      c.param1, c.param2, c.run_on, c.reference, c.unknown3,
      condition_text(strings, c.parameter1_string), condition_text(strings, c.parameter2_string)});
  return out;
}

radicle_status invalid() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required"); }

}

extern "C" {

radicle_status radicle_dialogue_topic_get_info(const radicle_env* env, const radicle_record* record,
                                              radicle_dialogue_topic_info* out_info) {
  clear_error();
  if (!env || !record || !out_info) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while decoding DIAL", [&] {
    const auto t = fo4::env::decode_dialogue_topic(env->value, *to_ref(record)); radicle_dialogue_topic_info v{};
    if (t.data) { v.has_data = 1; v.flags = t.data->flags; v.category = t.data->category; v.subtype = t.data->subtype; }
    set_optional(t.priority, v.has_priority, v.priority); set_optional(t.info_count, v.has_info_count, v.info_count);
    if (t.subtype_name) { v.has_subtype_name = 1; std::copy(t.subtype_name->begin(), t.subtype_name->end(), v.subtype_name); }
    v.branch = to_c_key(*env, t.branch); v.quest = to_c_key(*env, t.quest); v.keyword = to_c_key(*env, t.keyword);
    *out_info = v; return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_dialogue_info_decode(const radicle_env* env, const radicle_record* record,
                                            const radicle_dialogue_info** out_info) {
  clear_error();
  if (!env || !record || !out_info) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_info = nullptr;
  return guarded("unknown exception while decoding INFO", [&] {
    const auto d = fo4::env::decode_dialogue_info(env->value, *to_ref(record)); auto h = std::make_unique<InfoHolder>();
    auto& v = h->view;
    if (d.data) { v.has_data = 1; v.flags = (*d.data)[0]; v.reset_time_encoded = (*d.data)[1]; }
    set_optional(d.reset_hours(), v.has_reset_hours, v.reset_hours);
    v.previous_topic = to_c_key(*env, d.previous_topic); v.previous_info = to_c_key(*env, d.previous_info);
    v.shared_info = to_c_key(*env, d.shared_info); v.info_group = to_c_key(*env, d.info_group);
    v.speaker = to_c_key(*env, d.speaker); v.start_scene = to_c_key(*env, d.start_scene); v.prompt = text(*env, d.prompt);
    for (const auto& r : d.responses) h->responses.push_back({to_c_key(*env, r.emotion), to_c_key(*env, r.sound),
        r.number, r.unknown, r.interrupt_percent, r.camera_target_alias, r.camera_location_alias,
        text(*env, r.text), to_c_key(*env, r.speaker_idle), to_c_key(*env, r.listener_idle)});
    h->conditions = conditions(h->strings, d.conditions);
    v.responses = h->responses.data(); v.response_count = h->responses.size();
    v.conditions = h->conditions.data(); v.condition_count = h->conditions.size();
    v.internal = h.get(); *out_info = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_dialogue_info_free(const radicle_dialogue_info* info) {
  if (info) delete static_cast<InfoHolder*>(info->internal);
}

radicle_status radicle_scene_decode(const radicle_env* env, const radicle_record* record,
                                     const radicle_scene_info** out_scene) {
  clear_error();
  if (!env || !record || !out_scene) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_scene = nullptr;
  return guarded("unknown exception while decoding SCEN", [&] {
    const auto s = fo4::env::decode_scene(env->value, *to_ref(record)); auto h = std::make_unique<SceneHolder>();
    auto& v = h->view; set_optional(s.flags, v.has_flags, v.flags); v.parent_quest = to_c_key(*env, s.parent_quest);
    h->starts.resize(s.phases.size()); h->completions.resize(s.phases.size());
    for (std::size_t i = 0; i < s.phases.size(); ++i) {
      const auto& p = s.phases[i]; radicle_scene_phase row{}; row.name = stored_text(*env, p.name);
      set_optional(p.editor_width, row.has_editor_width, row.editor_width); set_optional(p.flags, row.has_flags, row.flags);
      if (p.quest_stages) { row.has_quest_stages = 1; row.start_quest_stage = (*p.quest_stages)[0]; row.completion_quest_stage = (*p.quest_stages)[1]; }
      h->starts[i] = conditions(h->strings, p.start_conditions); h->completions[i] = conditions(h->strings, p.completion_conditions);
      row.start_conditions = h->starts[i].data(); row.start_condition_count = h->starts[i].size();
      row.completion_conditions = h->completions[i].data(); row.completion_condition_count = h->completions[i].size(); h->phases.push_back(row);
    }
    for (const auto& a : s.actors) h->actors.push_back({a.alias, a.flags, a.behavior_flags});
    h->payloads.resize(s.actions.size());
    for (std::size_t i = 0; i < s.actions.size(); ++i) {
      const auto& a = s.actions[i]; radicle_scene_action row{}; row.type = a.type; row.name = stored_text(*env, a.name);
      set_optional(a.actor_alias, row.has_actor_alias, row.actor_alias); set_optional(a.index, row.has_index, row.index);
      set_optional(a.flags, row.has_flags, row.flags); set_optional(a.start_phase, row.has_start_phase, row.start_phase);
      set_optional(a.end_phase, row.has_end_phase, row.end_phase);
      for (const auto& sub : a.payload) {
        radicle_scene_payload raw{}; const auto name = sub.signature.to_string(); std::copy(name.begin(), name.end(), raw.signature);
        raw.bytes = reinterpret_cast<const std::uint8_t*>(sub.data.data()); raw.size = sub.data.size(); h->payloads[i].push_back(raw);
      }
      row.payload = h->payloads[i].data(); row.payload_count = h->payloads[i].size(); h->actions.push_back(row);
    }
    h->conditions = conditions(h->strings, s.conditions);
    v.phases = h->phases.data(); v.phase_count = h->phases.size(); v.actors = h->actors.data(); v.actor_count = h->actors.size();
    v.actions = h->actions.data(); v.action_count = h->actions.size(); v.conditions = h->conditions.data(); v.condition_count = h->conditions.size();
    v.internal = h.get(); *out_scene = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_scene_free(const radicle_scene_info* scene) {
  if (scene) delete static_cast<SceneHolder*>(scene->internal);
}

const char* radicle_scene_action_type_name(uint16_t type) {
  return fo4::env::scene_action_type_name(type).data();
}

radicle_status radicle_plugin_set_scene_scalar(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               const char* path,
                                               size_t group_index,
                                               const char* value) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || path == nullptr || value == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and value are required");
  }
  return guarded("unknown exception while setting scene field", [&] {
    try {
      switch (fo4::set_scene_scalar(plugin->value, *form_key, path, group_index, value)) {
        case fo4::ScalarMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::ScalarMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "scene has no such group or the group has no such subrecord");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_scene_formlink(radicle_plugin* plugin,
                                                 radicle_form_key key,
                                                 const char* path,
                                                 const radicle_form_key* target) {
  clear_error();
  const auto form_key = to_form_key(key);
  const auto target_key = target == nullptr ? std::optional<fo4::FormKey>{fo4::FormKey{}} : to_form_key(*target);
  if (plugin == nullptr || !form_key || path == nullptr || !target_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and a valid target are required");
  }
  return guarded("unknown exception while setting scene link", [&] {
    try {
      switch (fo4::set_scene_formlink(plugin->value, *form_key, path, *target_key)) {
        case fo4::FormLinkFieldMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::FormLinkFieldMutationStatus::record_mismatch:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene link path does not apply to this record type");
        case fo4::FormLinkFieldMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "scene has no such subrecord");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_scene_action_formlink(radicle_plugin* plugin,
                                                        radicle_form_key key,
                                                        size_t index,
                                                        const char* path,
                                                        const radicle_form_key* target) {
  clear_error();
  const auto form_key = to_form_key(key);
  const auto target_key = target == nullptr ? std::optional<fo4::FormKey>{fo4::FormKey{}} : to_form_key(*target);
  if (plugin == nullptr || !form_key || path == nullptr || !target_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and a valid target are required");
  }
  return guarded("unknown exception while setting action scene link", [&] {
    try {
      switch (fo4::set_scene_action_formlink(plugin->value, *form_key, index, path, *target_key)) {
        case fo4::FormLinkFieldMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::FormLinkFieldMutationStatus::record_mismatch:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene link path does not apply to this record type");
        case fo4::FormLinkFieldMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "scene has no such action or the action has no such subrecord");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_scene_group_count(const radicle_plugin* plugin,
                                                radicle_form_key key,
                                                radicle_scene_part part,
                                                size_t* out_count) {
  clear_error();
  const auto form_key = to_form_key(key);
  const auto scene_part = to_scene_part(part);
  if (plugin == nullptr || !form_key || out_count == nullptr || !scene_part) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, a scene part and out_count are required");
  }
  *out_count = 0;
  return guarded("unknown exception while counting scene groups", [&] {
    try {
      const auto count = fo4::scene_group_count(plugin->value, *form_key, *scene_part);
      if (!count) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      *out_count = *count;
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_insert_scene_phase(radicle_plugin* plugin,
                                                radicle_form_key key,
                                                size_t index,
                                                const char* name) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || name == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and a phase name are required");
  }
  return guarded("unknown exception while inserting a scene phase", [&] {
    try {
      switch (fo4::insert_scene_phase(plugin->value, *form_key, index, name)) {
        case fo4::SubrecordGroupMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::SubrecordGroupMutationStatus::index_out_of_range:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene has fewer phases than the requested index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_remove_scene_phase(radicle_plugin* plugin,
                                                radicle_form_key key,
                                                size_t index) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while removing a scene phase", [&] {
    try {
      switch (fo4::remove_scene_phase(plugin->value, *form_key, index)) {
        case fo4::SubrecordGroupMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::SubrecordGroupMutationStatus::index_out_of_range:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene has no phase at that index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_scene_phase_name(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  size_t index,
                                                  const char* name) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || name == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and a phase name are required");
  }
  return guarded("unknown exception while setting a scene phase name", [&] {
    try {
      switch (fo4::set_scene_phase_name(plugin->value, *form_key, index, name)) {
        case fo4::ScalarMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::ScalarMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "scene has no phase at that index or the phase has no name");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_insert_scene_actor(radicle_plugin* plugin,
                                                 radicle_form_key key,
                                                 size_t index) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while inserting a scene actor", [&] {
    try {
      switch (fo4::insert_scene_actor(plugin->value, *form_key, index)) {
        case fo4::SubrecordGroupMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::SubrecordGroupMutationStatus::index_out_of_range:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene has fewer actors than the requested index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_remove_scene_actor(radicle_plugin* plugin,
                                                 radicle_form_key key,
                                                 size_t index) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while removing a scene actor", [&] {
    try {
      switch (fo4::remove_scene_actor(plugin->value, *form_key, index)) {
        case fo4::SubrecordGroupMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::SubrecordGroupMutationStatus::index_out_of_range:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene has no actor at that index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_insert_scene_action(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  size_t index,
                                                  uint16_t type) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while inserting a scene action", [&] {
    try {
      switch (fo4::insert_scene_action(plugin->value, *form_key, index, type)) {
        case fo4::SubrecordGroupMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::SubrecordGroupMutationStatus::index_out_of_range:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene has fewer actions than the requested index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_remove_scene_action(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  size_t index) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while removing a scene action", [&] {
    try {
      switch (fo4::remove_scene_action(plugin->value, *form_key, index)) {
        case fo4::SubrecordGroupMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::SubrecordGroupMutationStatus::index_out_of_range:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scene has no action at that index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_scene_action_name(radicle_plugin* plugin,
                                                    radicle_form_key key,
                                                    size_t index,
                                                    const char* name) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || name == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and an action name are required");
  }
  return guarded("unknown exception while setting a scene action name", [&] {
    try {
      switch (fo4::set_scene_action_name(plugin->value, *form_key, index, name)) {
        case fo4::ScalarMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::ScalarMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "scene has no action at that index");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}
