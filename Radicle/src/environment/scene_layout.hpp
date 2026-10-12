#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::env::detail {

struct ScenePhaseSpan {
  std::size_t open{0}, first_next{0}, second_next{0}, close{0};
};

struct SceneActionSpan {
  std::size_t open{0}, header_end{0}, close{0};
};

struct SceneLayout {
  std::size_t first_group{0};
  std::size_t tail{0};
  std::vector<ScenePhaseSpan> phases;
  std::vector<std::size_t> actors;
  std::vector<SceneActionSpan> actions;
};

class SceneLayoutError : public std::runtime_error {
public:
  SceneLayoutError(Signature tag, const char* message) : std::runtime_error(message), tag(tag) {}
  Signature tag;
};

SceneLayout scene_layout(std::span<const Subrecord> subs);

}
