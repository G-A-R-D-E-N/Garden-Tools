#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/schema/ctda.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::forms {

enum class DialogueAuthorStatus {
  ok,
  quest_missing,
  branch_missing,
  topic_missing,
  info_missing,
  not_local,
  invalid_argument,
  localized_unsupported,
  undecodable,
  structure_invalid,
};

struct DialogueCreateResult {
  DialogueAuthorStatus status{DialogueAuthorStatus::structure_invalid};
  FormKey key{};
};

struct DialogueBranchSpec {
  std::string editor_id;
  std::uint32_t category{0};
  std::uint32_t flags{0};
};

struct DialogueTopicSpec {
  std::string editor_id;
  std::string name;
  float priority{50.0f};
  std::optional<FormKey> branch;
  std::optional<FormKey> keyword;
  std::uint8_t topic_flags{0};
  std::uint8_t category{0};
  std::uint16_t subtype{0};
  std::string subtype_name;
};

struct DialogueResponseSpec {
  std::string text;
  std::string script_notes;
  std::optional<FormKey> emotion;
  std::optional<FormKey> sound;
  std::uint8_t number{1};
  std::uint16_t interrupt_percent{0};
  std::int32_t camera_target_alias{-1};
  std::int32_t camera_location_alias{-1};
};

struct DialogueInfoSpec {
  std::uint16_t flags{0};
  std::uint16_t reset_hours{0};
  std::vector<DialogueResponseSpec> responses;
  std::vector<CtdaCondition> conditions;
};

// Constraint: the quest must be local to the plugin.
DialogueCreateResult create_dialogue_branch(Plugin& plugin,
                                            const FormKey& quest,
                                            const DialogueBranchSpec& spec);

// Constraint: the new DIAL goes before the first SCEN and starts with TIFC 0.
DialogueCreateResult create_dialogue_topic(Plugin& plugin,
                                           const FormKey& quest,
                                           const DialogueTopicSpec& spec);

DialogueAuthorStatus set_dialogue_branch_start(Plugin& plugin,
                                               const FormKey& branch,
                                               const FormKey& topic);

// Constraint: the INFO is appended last and the topic's TIFC is rewritten.
DialogueCreateResult create_dialogue_info(Plugin& plugin,
                                          const FormKey& topic,
                                          const DialogueInfoSpec& spec);

// Constraint: the following INFO is relinked and an empty topic group is dropped.
DialogueAuthorStatus remove_dialogue_info(Plugin& plugin, const FormKey& info);

}  // namespace fo4::forms
