#pragma once

#include "fo4/plugin/mutation.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace fo4 {

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

std::optional<std::size_t> scene_group_count(const Plugin& plugin, const FormKey& scene, ScenePart part);

}
