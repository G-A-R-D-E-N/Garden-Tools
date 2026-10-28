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
  /* Invariant: 0 on the root timeline, plus one for each enclosing sprite, and never derived from path. */
  std::uint32_t nesting_level{0};
  SwfTransform transform{};
  SwfTransform world_transform{};
  SwfBounds bounds{};
  bool has_bounds{false};
  bool is_sprite{false};
  std::optional<std::uint16_t> clip_depth;
  std::optional<SwfColorTransform> world_color_transform;
  /* Invariant: false when this row or any sprite above it is hidden by a PlaceObject3 visible flag. */
  bool world_visible{true};
  /* Invariant: the tag that created the object, top level for a root row and a child index inside its DefineSprite. */
  std::size_t create_tag_index{0};
  /* Invariant: absent until a placement authors a MATRIX, otherwise the address of the one that last authored it. */
  std::optional<std::size_t> transform_tag_index;

  /* Invariant: compares what the frame displays, never where each placement lives. */
  bool operator==(const SwfFrameInstance& other) const {
    return instance_id == other.instance_id && depth == other.depth &&
           character_id == other.character_id && timeline_id == other.timeline_id &&
           timeline_frame == other.timeline_frame && name == other.name && path == other.path &&
           transform == other.transform && world_transform == other.world_transform &&
           bounds == other.bounds && has_bounds == other.has_bounds && is_sprite == other.is_sprite &&
           nesting_level == other.nesting_level && clip_depth == other.clip_depth &&
           world_color_transform == other.world_color_transform && world_visible == other.world_visible;
  }
};

/* Invariant: transform_tag_index names the placement that last authored the visible MATRIX. */
struct SwfInstancePlacement {
  std::uint32_t instance_id{0};
  std::uint32_t frame{0};
  std::string path;
  bool inside_sprite{false};
  std::uint16_t sprite_id{0};
  std::size_t sprite_tag_index{0};
  bool has_authored_matrix{false};
  std::size_t transform_tag_index{0};
  /* Invariant: the tag that created the object, top level for a root instance and a child index for a sprite one. */
  std::size_t create_tag_index{0};

  bool operator==(const SwfInstancePlacement&) const = default;
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
  /* Constraint: off by default, so a read-only frame handle does not pay for the separate placement list. */
  bool record_placements{false};
};

struct SwfDisplayList {
  std::vector<SwfDisplayTimeline> timelines;
  /* Invariant: only the root traversal fills this. */
  std::vector<SwfInstancePlacement> placements;
  SwfTimelineStatus status{SwfTimelineStatus::malformed};
  std::string message;
};

/* timelines[0] must be the root and the rest must follow in character order; each frame must be the state at its own index with every sprite expanded at the age its placement gives it. */
SwfDisplayList build_display_list(const SwfFile& file, const SwfDisplayOptions& options = {});

}  // namespace fo4::swf
