#pragma once

#include "fo4/plugin_core/container.hpp"
#include "fo4/plugin_core/scene_layout.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace fo4::detail {

struct StartSceneLayout {
  std::vector<std::pair<std::size_t, std::size_t>> entries;
  std::size_t marker;
};

env::detail::SceneLayout checked_scene_layout(const Record& record);

std::optional<StartSceneLayout> start_scene_layout(const Record& record, std::size_t action);

std::optional<std::vector<std::pair<std::size_t, std::size_t>>> start_scene_entries(const Record& record, std::size_t action);

}  // namespace fo4::detail
