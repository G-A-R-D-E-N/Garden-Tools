#pragma once

#include <fo4/avm1/avm1.hpp>
#include <fo4/swf/swf.hpp>

#include <cstdint>
#include <map>
#include <vector>

namespace fo4::swf::detail {

enum class FrameControlKind : std::uint8_t { stop, play, go_to, next, previous };

struct FrameControl {
  FrameControlKind kind{FrameControlKind::stop};
  std::uint32_t frame{0};
};

using FrameControls = std::vector<std::vector<FrameControl>>;

struct Playhead {
  std::vector<std::uint32_t> frame_at;
  std::vector<std::vector<std::uint32_t>> entries;
};

using InitActionStreams = std::vector<std::vector<avm1::Avm1Action>>;

FrameControls collect_frame_controls(const std::vector<SwfTag>& tags);
FrameControls collect_frame_controls(const std::vector<SwfTag>& tags,
                                     const InitActionStreams& init);

// Requires top-level tags; groups decoded DoInitAction streams by sprite id.
std::map<std::uint16_t, InitActionStreams> collect_init_actions(
    const std::vector<SwfTag>& tags);

Playhead simulate_playhead(const FrameControls& controls, std::uint32_t ticks);

}  // namespace fo4::swf::detail
