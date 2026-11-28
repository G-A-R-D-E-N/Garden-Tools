#pragma once

#include "fo4/environment/dialogue.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::env {

enum class SceneLinkSlot : std::uint32_t {
  topic = 0,
  sound = 1,
  dialogue_subtype = 2,
  anim_arch_type = 3,
  audio_output_override = 4,
  player_positive_response = 5,
  player_negative_response = 6,
  player_neutral_response = 7,
  player_question_response = 8,
  player_positive_subtype = 9,
  player_negative_subtype = 10,
  player_neutral_subtype = 11,
  player_question_subtype = 12,
  npc_positive_response = 13,
  npc_negative_response = 14,
  npc_neutral_response = 15,
  npc_question_response = 16,
  npc_positive_subtype = 17,
  npc_negative_subtype = 18,
  npc_neutral_subtype = 19,
  npc_question_subtype = 20
};

struct SceneStartEntry {
  FormKey scene;
  std::uint16_t phase_index{0};
  std::optional<std::string_view> phase_name;
  std::vector<CtdaCondition> conditions;
};

struct SceneActionTarget {
  std::uint16_t type{0};
  bool malformed{false};
  bool end_scene_greeting{false};
  std::vector<std::pair<SceneLinkSlot, FormKey>> links;
  std::optional<float> max_seconds, min_seconds;
  std::optional<std::array<float, 2>> camera;
  std::optional<std::uint32_t> emotion_type, emotion_value;
  std::optional<std::int16_t> quest_stage;
  std::optional<std::int32_t> dialogue_target_actor;
  std::vector<std::int32_t> head_tracking;
  std::vector<FormKey> packages;
  std::vector<SceneStartEntry> start_entries;
};

struct SceneTargets {
  std::optional<std::array<std::int16_t, 2>> quest_stages;
  std::vector<SceneActionTarget> actions;
};

SceneTargets decode_scene_targets(const Environment& env, const RecordRef& ref);

}  // namespace fo4::env
