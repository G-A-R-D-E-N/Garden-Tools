#include "c_api_env_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/environment/dialogue.hpp"
#include "fo4/text/encoding.hpp"

#include <deque>
#include <memory>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using Conditions = std::vector<radicle_dialogue_condition>;

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
  clear_error(); if (out_info) *out_info = nullptr;
  if (!env || !record || !out_info) return invalid();
  return guarded("unknown exception while decoding INFO", [&] {
    const auto d = fo4::env::decode_dialogue_info(env->value, *to_ref(record)); auto h = std::make_unique<InfoHolder>();
    auto& v = h->view;
    if (d.data) { v.has_data = 1; v.flags = (*d.data)[0]; v.reset_time_encoded = (*d.data)[1]; }
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
  clear_error(); if (out_scene) *out_scene = nullptr;
  if (!env || !record || !out_scene) return invalid();
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

}
