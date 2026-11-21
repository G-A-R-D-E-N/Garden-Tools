#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/ctda.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::forms {

class SceneError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

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

// Constraint: One repeated LCEP/INTT/SSPN/CITC/CTDA entry in a type-4 Start Scene action.
struct SceneStartSceneEntry {
  FormKey scene;
  std::uint16_t phase_index{0};
  std::optional<std::string> phase_name;
  std::vector<CtdaCondition> conditions;
};

enum class SceneStartSceneStatus {
  ok,
  unchanged,
  record_missing,
  action_missing,
  action_not_start_scene,
  index_out_of_range,
  structure_invalid,
};

// Constraint: the type-4 payload reads as an ordered list; no value means the scene, the action index or the action type does not match.
std::optional<std::vector<SceneStartSceneEntry>> read_scene_start_scenes(const Plugin& plugin,
                                                                        const FormKey& scene,
                                                                        std::size_t action_index);

// Constraint: an inserted or rewritten entry must read back at its index and ordinal, or the plugin is left unchanged.
SceneStartSceneStatus insert_scene_start_scene(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t action_index,
                                               std::size_t index,
                                               const SceneStartSceneEntry& entry);

SceneStartSceneStatus update_scene_start_scene(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t action_index,
                                               std::size_t index,
                                               const SceneStartSceneEntry& entry);

SceneStartSceneStatus remove_scene_start_scene(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t action_index,
                                               std::size_t index);

// Constraint: the quest must be local to the plugin, and the new SCEN is appended last in the quest children group.
SceneCreateResult create_scene(Plugin& plugin, const FormKey& quest, const SceneSpec& spec);

// Constraint: the editor-id overload appends a bare SCEN with no phases, which the authoring API cannot express.
FormKey create_scene(Plugin& plugin, const FormKey& quest, std::string_view editor_id);

}  // namespace fo4::forms
