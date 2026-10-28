#pragma once

#include <fo4/swf/swf.hpp>

#include <cstdint>
#include <vector>

namespace fo4::swf::detail {

enum class FrameControlKind : std::uint8_t { stop, play, go_to, next, previous };

struct FrameControl {
  FrameControlKind kind{FrameControlKind::stop};
  std::uint32_t frame{0};
};

using FrameControls = std::vector<std::vector<FrameControl>>;

struct Playhead {
  std::uint32_t frame{0};
  std::vector<std::uint32_t> entered;
};

FrameControls collect_frame_controls(const std::vector<SwfTag>& tags);

Playhead simulate_playhead(const FrameControls& controls, std::uint32_t age);

}  // namespace fo4::swf::detail
