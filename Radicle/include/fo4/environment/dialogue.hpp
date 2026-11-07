#pragma once

#include "fo4/environment/world.hpp"
#include "fo4/schema/ctda.hpp"

#include <array>
#include <span>
#include <vector>

namespace fo4::env {

struct DialogueTopic {
  struct Data { std::uint8_t flags{0}, category{0}; std::uint16_t subtype{0}; };
  std::optional<Data> data;
  std::optional<float> priority;
  std::optional<std::uint32_t> info_count;
  std::optional<std::array<std::uint8_t, 4>> subtype_name;
  std::optional<FormKey> branch, quest, keyword;
};

struct DialogueResponse {
  FormKey emotion, sound;
  std::uint8_t number{0}, unknown{0};
  std::uint16_t interrupt_percent{0};
  std::int32_t camera_target_alias{0}, camera_location_alias{0};
  RecordName text;
  std::optional<FormKey> speaker_idle, listener_idle;
};

struct DialogueInfo {
  std::optional<std::array<std::uint16_t, 2>> data;
  std::optional<FormKey> previous_topic, previous_info, shared_info, info_group, speaker, start_scene;
  RecordName prompt;
  std::vector<DialogueResponse> responses;
  std::vector<CtdaCondition> conditions;
  std::optional<double> reset_hours() const;
};

enum class SceneActionType : std::uint16_t {
  dialogue = 0,
  package = 1,
  timer = 2,
  player_dialogue = 3,
  start_scene = 4,
  npc_response_dialogue = 5,
  radio = 6
};

std::optional<SceneActionType> scene_action_type(std::uint16_t type);
std::string_view scene_action_type_name(std::uint16_t type);

struct ScenePhase {
  std::optional<std::string_view> name;
  std::optional<std::uint32_t> editor_width;
  std::optional<std::uint16_t> flags;
  std::optional<std::array<std::int16_t, 2>> quest_stages;
  std::vector<CtdaCondition> start_conditions, completion_conditions;
};

struct SceneActor { std::int32_t alias{0}; std::uint32_t flags{0}, behavior_flags{0}; };

struct SceneAction {
  std::uint16_t type{0};
  std::optional<std::string_view> name;
  std::optional<std::int32_t> actor_alias;
  std::optional<std::uint32_t> index, flags;
  std::optional<std::uint32_t> start_phase, end_phase;
  std::span<const Subrecord> payload;
};

struct Scene {
  std::optional<std::uint32_t> flags;
  std::optional<FormKey> parent_quest;
  std::optional<FormKey> template_scene;
  std::optional<std::uint32_t> index;
  std::vector<ScenePhase> phases;
  std::vector<SceneActor> actors;
  std::vector<SceneAction> actions;
  std::vector<CtdaCondition> conditions;
};

DialogueTopic decode_dialogue_topic(const Environment& env, const RecordRef& ref);
DialogueInfo decode_dialogue_info(const Environment& env, const RecordRef& ref);
Scene decode_scene(const Environment& env, const RecordRef& ref);

}
