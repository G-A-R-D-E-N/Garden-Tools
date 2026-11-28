#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {

struct ScenePayloadPath {
  std::string_view path;
  Signature signature;
  bool link;
};

enum class ScenePart {
  phase,
  actor,
  action,
};

ScalarMutationStatus set_scene_scalar(Plugin& plugin,
                                      const FormKey& scene,
                                      std::string_view path,
                                      std::size_t group_index,
                                      std::string_view value);

FormLinkFieldMutationStatus set_scene_formlink(Plugin& plugin,
                                               const FormKey& scene,
                                               std::string_view path,
                                               const FormKey& target);

FormLinkFieldMutationStatus set_scene_action_formlink(Plugin& plugin,
                                                      const FormKey& scene,
                                                      std::size_t index,
                                                      std::string_view path,
                                                      const FormKey& target);

struct SceneActionCamera { float fov; float rate; };
struct SceneActionCameraState { std::optional<SceneActionCamera> value; };
std::optional<SceneActionCameraState> read_scene_action_camera(const Plugin& plugin, const FormKey& scene, std::size_t action);
FormLinkFieldMutationStatus set_scene_action_camera(Plugin& plugin, const FormKey& scene, std::size_t action,
                                                    std::optional<SceneActionCamera> value);

std::optional<std::vector<FormKey>> read_scene_action_packages(const Plugin& plugin, const FormKey& scene,
                                                               std::size_t action);

FormLinkFieldMutationStatus set_scene_action_packages(Plugin& plugin,
                                                       const FormKey& scene,
                                                       std::size_t action,
                                                       const std::vector<FormKey>& packages);

FormLinkFieldMutationStatus set_scene_action_headtracking(Plugin& plugin, const FormKey& scene,
                                                           std::size_t action,
                                                           const std::vector<std::int32_t>& aliases);

FormLinkFieldMutationStatus insert_scene_action_start_scene(Plugin& plugin, const FormKey& scene,
                                                             std::size_t action, std::size_t index, const FormKey& target);
FormLinkFieldMutationStatus set_scene_action_start_scene(Plugin& plugin, const FormKey& scene,
                                                          std::size_t action, std::size_t index, const FormKey& target);
FormLinkFieldMutationStatus remove_scene_action_start_scene(Plugin& plugin, const FormKey& scene,
                                                             std::size_t action, std::size_t index);

struct SceneStartPhase {
  std::optional<std::uint16_t> index;
  std::optional<std::string> name;
};

std::optional<SceneStartPhase> read_scene_action_start_phase(const Plugin& plugin, const FormKey& scene,
                                                            std::size_t action, std::size_t entry);
FormLinkFieldMutationStatus set_scene_action_start_phase_index(Plugin& plugin, const FormKey& scene,
    std::size_t action, std::size_t entry, std::optional<std::uint16_t> value);
FormLinkFieldMutationStatus set_scene_action_start_phase_name(Plugin& plugin, const FormKey& scene,
    std::size_t action, std::size_t entry, std::optional<std::string_view> value);

std::optional<std::size_t> scene_group_count(const Plugin& plugin, const FormKey& scene, ScenePart part);

// Requires: one row per SCEN action payload member the writer accepts, with the subrecord it is stored in, so a caller's key table can be checked against it.
std::vector<ScenePayloadPath> scene_action_payload_paths();

SubrecordGroupMutationStatus insert_scene_phase(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index,
                                                std::string_view name);

SubrecordGroupMutationStatus remove_scene_phase(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index);

// Constraint: moves the phase to the given index and rewrites every phase reference that must follow it.
SubrecordGroupMutationStatus move_scene_phase(Plugin& plugin,
                                              const FormKey& scene,
                                              std::size_t from,
                                              std::size_t to);

ScalarMutationStatus set_scene_phase_name(Plugin& plugin,
                                          const FormKey& scene,
                                          std::size_t index,
                                          std::string_view name);

SubrecordGroupMutationStatus insert_scene_actor(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index);

SubrecordGroupMutationStatus remove_scene_actor(Plugin& plugin,
                                                const FormKey& scene,
                                                std::size_t index);

SubrecordGroupMutationStatus insert_scene_action(Plugin& plugin,
                                                 const FormKey& scene,
                                                 std::size_t index,
                                                 std::uint16_t type);

SubrecordGroupMutationStatus remove_scene_action(Plugin& plugin,
                                                 const FormKey& scene,
                                                 std::size_t index);

// Constraint: moves the action to the given index and renumbers every action index that must follow it.
SubrecordGroupMutationStatus move_scene_action(Plugin& plugin,
                                               const FormKey& scene,
                                               std::size_t from,
                                               std::size_t to);

ScalarMutationStatus set_scene_action_name(Plugin& plugin,
                                           const FormKey& scene,
                                           std::size_t index,
                                           std::string_view name);

// Constraint: an absent member must be inserted at its canonical position and must read back in its scope, or the plugin is left unchanged.
ScalarMutationStatus ensure_scene_member(Plugin& plugin,
                                         const FormKey& scene,
                                         std::string_view path,
                                         std::size_t group_index);

}
