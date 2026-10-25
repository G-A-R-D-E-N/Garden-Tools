#pragma once

#include "fo4/swf/swf_timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::swf {

struct SwfFrameInstance {
  std::uint32_t instance_id{0};
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::uint16_t timeline_id{0};
  std::uint32_t timeline_frame{0};
  std::string name;
  std::string path;
  SwfTransform transform{};
  SwfTransform world_transform{};
  SwfBounds bounds{};
  bool has_bounds{false};
  bool is_sprite{false};
  std::optional<std::uint16_t> clip_depth;

  bool operator==(const SwfFrameInstance&) const = default;
};

struct SwfDisplayFrame {
  std::uint32_t frame{0};
  std::vector<SwfFrameInstance> instances;

  bool operator==(const SwfDisplayFrame&) const = default;
};

struct SwfDisplayTimeline {
  std::uint16_t character_id{0};
  std::vector<SwfDisplayFrame> frames;

  bool operator==(const SwfDisplayTimeline&) const = default;
};

struct SwfDisplayOptions {
  std::size_t max_instances{250000};
  unsigned max_depth{64};
};

struct SwfDisplayList {
  std::vector<SwfDisplayTimeline> timelines;
  SwfTimelineStatus status{SwfTimelineStatus::malformed};
  std::string message;
};

/* timelines[0] must be the root and the rest must follow in character order; each frame must be the state at its own index with every sprite expanded at the age its placement gives it. */
SwfDisplayList build_display_list(const SwfFile& file, const SwfDisplayOptions& options = {});

}  // namespace fo4::swf
