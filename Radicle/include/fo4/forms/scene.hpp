#pragma once

#include "fo4/plugin/container.hpp"

#include <cstdint>
#include <string>

namespace fo4::forms {

enum class SceneAuthorStatus {
  ok,
  quest_missing,
  not_local,
  invalid_argument,
  undecodable,
  structure_invalid,
};

struct SceneCreateResult {
  SceneAuthorStatus status{SceneAuthorStatus::structure_invalid};
  FormKey key{};
};

struct SceneSpec {
  std::string editor_id;
  std::uint32_t flags{0};
  std::uint32_t index{0};
};

// Constraint: the quest must be local to the plugin, and the new SCEN is appended last in the quest children group.
SceneCreateResult create_scene(Plugin& plugin, const FormKey& quest, const SceneSpec& spec);

}  // namespace fo4::forms
