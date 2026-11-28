#include "c_api_env_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/environment/info_result.hpp"
#include "fo4/environment/scene_targets.hpp"
#include "fo4/environment/term_fragments.hpp"
#include "fo4/text/encoding.hpp"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using Conditions = std::vector<radicle_dialogue_condition>;

struct TargetsHolder {
  radicle_scene_targets view{};
  fo4::env::SceneTargets native;
  std::vector<radicle_scene_action_target> actions;
  std::vector<std::vector<radicle_scene_target_link>> links;
  std::vector<std::vector<radicle_form_key>> packages;
  std::vector<std::vector<radicle_scene_start_entry>> entries;
  std::vector<std::vector<Conditions>> conditions;
  std::deque<std::string> strings;
};

struct InfoResultHolder {
  radicle_info_result view{};
  fo4::env::InfoResult native;
  std::vector<radicle_info_fragment_row> rows;
  std::deque<std::string> strings;
};

struct TermFragmentsHolder {
  radicle_term_fragments view{};
  fo4::env::TermFragments native;
  std::vector<radicle_term_fragment_row> rows;
  std::deque<std::string> strings;
};

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

const char* utf8(std::deque<std::string>& strings, const std::string& stored) {
  strings.push_back(fo4::text::windows1252_to_utf8(stored));
  return strings.back().c_str();
}

radicle_status invalid() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output required"); }

}  // namespace

extern "C" {

radicle_status radicle_scene_targets_decode(const radicle_env* env, const radicle_record* record,
                                            const radicle_scene_targets** out_targets) {
  clear_error();
  if (!env || !record || !out_targets) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_targets = nullptr;
  return guarded("unknown exception while decoding SCEN targets", [&] {
    auto h = std::make_unique<TargetsHolder>();
    h->native = fo4::env::decode_scene_targets(env->value, *to_ref(record));
    const auto& n = h->native;
    auto& v = h->view;
    if (n.quest_stages) {
      v.has_quest_stages = 1;
      v.quest_begin_stage = (*n.quest_stages)[0];
      v.quest_end_stage = (*n.quest_stages)[1];
    }
    const auto count = n.actions.size();
    h->links.resize(count); h->packages.resize(count); h->entries.resize(count); h->conditions.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto& a = n.actions[i];
      radicle_scene_action_target row{};
      row.type = a.type; row.malformed = a.malformed ? 1 : 0; row.end_scene_greeting = a.end_scene_greeting ? 1 : 0;
      set_optional(a.max_seconds, row.has_max_seconds, row.max_seconds);
      set_optional(a.min_seconds, row.has_min_seconds, row.min_seconds);
      if (a.camera) { row.has_camera = 1; row.camera_fov = (*a.camera)[0]; row.camera_rate = (*a.camera)[1]; }
      set_optional(a.emotion_type, row.has_emotion_type, row.emotion_type);
      set_optional(a.emotion_value, row.has_emotion_value, row.emotion_value);
      set_optional(a.quest_stage, row.has_quest_stage, row.quest_stage);
      set_optional(a.dialogue_target_actor, row.has_dialogue_target_actor, row.dialogue_target_actor);
      for (const auto& [slot, key] : a.links)
        h->links[i].push_back({static_cast<radicle_scene_link_slot>(slot), to_c_key(*env, key)});
      for (const auto& key : a.packages) h->packages[i].push_back(to_c_key(*env, key));
      h->conditions[i].resize(a.start_entries.size());
      for (std::size_t j = 0; j < a.start_entries.size(); ++j) {
        const auto& e = a.start_entries[j];
        h->conditions[i][j] = conditions(h->strings, e.conditions);
        h->entries[i].push_back({to_c_key(*env, e.scene), e.phase_index, e.phase_name ? 1 : 0,
                                 stored_text(*env, e.phase_name), h->conditions[i][j].data(), h->conditions[i][j].size()});
      }
      row.links = h->links[i].data(); row.link_count = h->links[i].size();
      row.head_tracking = a.head_tracking.data(); row.head_tracking_count = a.head_tracking.size();
      row.packages = h->packages[i].data(); row.package_count = h->packages[i].size();
      row.start_entries = h->entries[i].data(); row.start_entry_count = h->entries[i].size();
      h->actions.push_back(row);
    }
    v.actions = h->actions.data(); v.action_count = h->actions.size();
    v.internal = h.get(); *out_targets = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_scene_targets_free(const radicle_scene_targets* targets) {
  if (targets) delete static_cast<TargetsHolder*>(targets->internal);
}

radicle_status radicle_info_result_decode(const radicle_env* env, const radicle_record* record,
                                          const radicle_info_result** out_result) {
  clear_error();
  if (!env || !record || !out_result) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_result = nullptr;
  return guarded("unknown exception while decoding INFO result", [&] {
    auto h = std::make_unique<InfoResultHolder>();
    h->native = fo4::env::decode_info_result(env->value, *to_ref(record));
    const auto& n = h->native;
    auto& v = h->view;
    if (n.stages) { v.has_stages = 1; v.begin_stage = (*n.stages)[0]; v.end_stage = (*n.stages)[1]; }
    if (n.start_scene_phase) { v.has_start_scene_phase = 1; v.start_scene_phase = stored_text(*env, n.start_scene_phase); }
    v.fragment_state = static_cast<radicle_info_fragment_state>(n.fragment_state);
    if (n.fragment_state == fo4::env::InfoFragmentState::unreadable) v.fragment_failure = utf8(h->strings, n.fragment_failure);
    v.fragment_version = n.fragment_version; v.fragment_flags = n.fragment_flags;
    for (const auto& row : n.rows)
      h->rows.push_back({static_cast<std::uint8_t>(row.kind), row.unknown, utf8(h->strings, row.script), utf8(h->strings, row.function)});
    v.rows = h->rows.data(); v.row_count = h->rows.size();
    v.internal = h.get(); *out_result = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_info_result_free(const radicle_info_result* result) {
  if (result) delete static_cast<InfoResultHolder*>(result->internal);
}

radicle_status radicle_term_fragments_decode(const radicle_env* env, const radicle_record* record,
                                             const radicle_term_fragments** out_fragments) {
  clear_error();
  if (!env || !record || !out_fragments) return invalid();
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_fragments = nullptr;
  return guarded("unknown exception while decoding TERM fragments", [&] {
    auto h = std::make_unique<TermFragmentsHolder>();
    h->native = fo4::env::decode_term_fragments(env->value, *to_ref(record));
    const auto& n = h->native;
    auto& v = h->view;
    v.state = static_cast<radicle_term_fragment_state>(n.state);
    if (n.state == fo4::env::TermFragmentState::unreadable) v.failure = utf8(h->strings, n.failure);
    v.version = n.version;
    if (n.state == fo4::env::TermFragmentState::present) v.script_name = utf8(h->strings, n.script.name);
    for (const auto& row : n.rows)
      h->rows.push_back({row.index, row.unknown, utf8(h->strings, row.script), utf8(h->strings, row.function)});
    v.rows = h->rows.data(); v.row_count = h->rows.size();
    v.internal = h.get(); *out_fragments = &h.release()->view; return RADICLE_STATUS_OK;
  });
}

void radicle_term_fragments_free(const radicle_term_fragments* fragments) {
  if (fragments) delete static_cast<TermFragmentsHolder*>(fragments->internal);
}

}
