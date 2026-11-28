#pragma once

#include <fo4/swf/swf.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
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

// Invariant: src/swf includes no avm1 header; the index is defined in src/flash/swf_frame_control_actions.cpp.
struct InitActionIndex;

// Requires one budget per movie; a call whose body does not fit is a no-op, never a refusal.
inline constexpr std::size_t kInlineActionBudget = 65536;

// Requires top-level tags; groups decoded DoInitAction streams by sprite id. Defined in src/flash.
std::shared_ptr<const InitActionIndex> collect_init_actions(const std::vector<SwfTag>& tags);

// Requires the budget shared by every timeline of the movie; init actions run once per simulation. A null index or a sprite with no stream has no init actions. Defined in src/flash.
FrameControls collect_frame_controls(const std::vector<SwfTag>& tags, const InitActionIndex* init,
                                     std::uint16_t sprite_id, std::size_t& budget);

Playhead simulate_playhead(const FrameControls& controls, std::uint32_t ticks);

}  // namespace fo4::swf::detail
