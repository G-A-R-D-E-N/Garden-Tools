#include "swf_frame_control.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::swf::detail {

namespace {

constexpr std::size_t kMaxHops = 64;
constexpr std::size_t kEntriesPerTick = 4;
constexpr std::size_t kEntrySlack = 1024;

struct Cursor {
  std::uint32_t frame{0};
  bool playing{true};
  std::size_t recorded{0};
  std::size_t budget{0};
  bool init_done{false};
};

void enter(Playhead& head, Cursor& cursor, std::uint32_t to, std::uint32_t tick) {
  const std::uint32_t first = to > cursor.frame ? cursor.frame + 1 : to;
  for (std::uint32_t frame = first; frame <= to; ++frame) {
    std::vector<std::uint32_t>& ticks = head.entries[frame];
    if (ticks.empty() || ticks.back() != tick) {
      ticks.push_back(tick);
      ++cursor.recorded;
    }
  }
}

bool land(const FrameControls& controls, std::uint32_t tick, std::uint32_t target, Cursor& cursor,
          Playhead& head) {
  const auto count = static_cast<std::uint32_t>(controls.size());
  const std::size_t max_hops = std::min(controls.size() + 1, kMaxHops);
  bool moved = false;
  for (std::size_t hop = 0; hop < max_hops; ++hop) {
    if (target != cursor.frame) {
      if (cursor.recorded >= cursor.budget) {
        return moved;
      }
      enter(head, cursor, target, tick);
      cursor.frame = target;
      moved = true;
    }
    std::optional<std::uint32_t> jump;
    // Init controls must run once per simulation, so later landings on frame 0 skip to the marker.
    bool skip = cursor.init_done && cursor.frame == 0;
    for (const FrameControl& control : controls[cursor.frame]) {
      if (control.kind == FrameControlKind::init_barrier) {
        cursor.init_done = true;
        skip = false;
        continue;
      }
      if (skip) {
        continue;
      }
      switch (control.kind) {
        case FrameControlKind::stop:
          cursor.playing = false;
          break;
        case FrameControlKind::play:
          cursor.playing = true;
          break;
        case FrameControlKind::go_to:
          jump = std::min(control.frame, count - 1);
          break;
        case FrameControlKind::next:
          if (cursor.frame + 1 < count) {
            jump = cursor.frame + 1;
            cursor.playing = false;
          }
          break;
        case FrameControlKind::previous:
          if (cursor.frame > 0) {
            jump = cursor.frame - 1;
            cursor.playing = false;
          }
          break;
        case FrameControlKind::init_barrier:
          // The guard above must consume the barrier, so this case never runs.
          break;
      }
    }
    if (!jump) {
      return moved;
    }
    target = *jump;
  }
  return moved;
}

}  // namespace

Playhead simulate_playhead(const FrameControls& controls, std::uint32_t ticks) {
  if (std::all_of(controls.begin(), controls.end(),
                  [](const std::vector<FrameControl>& frame) { return frame.empty(); })) {
    return {};
  }
  const auto count = static_cast<std::uint32_t>(controls.size());
  Playhead head;
  head.entries.resize(count);
  head.frame_at.reserve(ticks);
  head.entries[0].push_back(0);
  Cursor cursor;
  cursor.recorded = 1;
  cursor.budget = kEntriesPerTick * ticks + kEntrySlack;
  land(controls, 0, 0, cursor, head);
  head.frame_at.push_back(cursor.frame);
  for (std::uint32_t tick = 1; tick < ticks; ++tick) {
    const bool was_playing = cursor.playing;
    const bool moved = was_playing && land(controls, tick, (cursor.frame + 1) % count, cursor, head);
    head.frame_at.push_back(cursor.frame);
    if (!moved && cursor.playing == was_playing) {
      break;
    }
  }
  return head;
}

}  // namespace fo4::swf::detail
