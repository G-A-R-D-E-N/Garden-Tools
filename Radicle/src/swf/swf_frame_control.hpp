#pragma once

#include <fo4/avm1/avm1.hpp>
#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace fo4::swf::detail {

enum class FrameControlKind : std::uint8_t { stop, play, go_to, next, previous, init_barrier };

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

// Requires one budget per movie; a call whose body does not fit is a no-op, never a refusal.
inline constexpr std::size_t kInlineActionBudget = 65536;

// Requires the budget shared by every timeline of the movie; init actions run once per simulation.
FrameControls collect_frame_controls(const std::vector<SwfTag>& tags,
                                     const InitActionStreams& init, std::size_t& budget);

// Requires top-level tags; groups decoded DoInitAction streams by sprite id.
std::map<std::uint16_t, InitActionStreams> collect_init_actions(
    const std::vector<SwfTag>& tags);

Playhead simulate_playhead(const FrameControls& controls, std::uint32_t ticks);

}  // namespace fo4::swf::detail
