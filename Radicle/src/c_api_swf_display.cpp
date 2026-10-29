#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_frame_svg.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

struct radicle_swf_frames {
  fo4::swf::SwfDisplayList display;
  std::shared_ptr<radicle_swf_detail::revision_state> revision;
  std::uint64_t built_revision{0};
};

namespace {

// Invariant: every accessor must fail once the movie revision this list copied has moved.
bool stale(const radicle_swf_frames* frames) {
  return frames != nullptr && frames->revision &&
         frames->revision->revision != frames->built_revision;
}

radicle_status fail_stale() {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT,
              "this frame list was built before the movie was edited; reopen it");
}

const fo4::swf::SwfDisplayTimeline* timeline_at(const radicle_swf_frames* frames, std::size_t index) {
  if (frames == nullptr || index >= frames->display.timelines.size()) {
    return nullptr;
  }
  return &frames->display.timelines[index];
}

const fo4::swf::SwfDisplayFrame* frame_at(const radicle_swf_frames* frames, std::size_t timeline,
                                          std::size_t frame) {
  const auto* found = timeline_at(frames, timeline);
  if (found == nullptr || frame >= found->frames.size()) {
    return nullptr;
  }
  return &found->frames[frame];
}

radicle_swf_transform to_transform(const fo4::swf::SwfTransform& value) {
  radicle_swf_transform out{};
  out.scale_x = value.scale_x;
  out.rotate_skew0 = value.rotate_skew0;
  out.rotate_skew1 = value.rotate_skew1;
  out.scale_y = value.scale_y;
  out.translate_x = value.translate_x;
  out.translate_y = value.translate_y;
  return out;
}

radicle_swf_bounds to_bounds(const fo4::swf::SwfBounds& value) {
  radicle_swf_bounds out{};
  out.x = value.x;
  out.y = value.y;
  out.width = value.width;
  out.height = value.height;
  return out;
}

// Invariant: after this, every create and transform index of the list is a flat radicle_swf_tag_at index.
bool to_flat_indices(const fo4::swf::SwfFile& file, fo4::swf::SwfDisplayList& display) {
  std::map<std::uint16_t, std::vector<std::size_t>> flat_by_timeline;
  for (auto& timeline : display.timelines) {
    for (auto& frame : timeline.frames) {
      for (auto& instance : frame.instances) {
        auto cached = flat_by_timeline.find(instance.timeline_id);
        if (cached == flat_by_timeline.end()) {
          std::vector<std::size_t> positions;
          if (!radicle_swf_detail::timeline_flat_indices(file, instance.timeline_id, positions)) {
            return false;
          }
          cached = flat_by_timeline.emplace(instance.timeline_id, std::move(positions)).first;
        }
        const std::vector<std::size_t>& flat = cached->second;
        if (instance.create_tag_index >= flat.size()) return false;
        instance.create_tag_index = flat[instance.create_tag_index];
        if (!instance.transform_tag_index) continue;
        if (*instance.transform_tag_index >= flat.size()) return false;
        instance.transform_tag_index = flat[*instance.transform_tag_index];
      }
    }
  }
  return true;
}

radicle_status build_checked_display(const fo4::swf::SwfFile& file, fo4::swf::SwfDisplayList& display) {
  fo4::swf::SwfDisplayOptions options;
  options.apply_frame_control = true;
  display = fo4::swf::build_display_list(file, options);
  if (display.status != fo4::swf::SwfTimelineStatus::ok) {
    return fail(RADICLE_STATUS_PARSE_ERROR,
                display.message.empty() ? "the display list did not build" : display.message.c_str());
  }
  if (!to_flat_indices(file, display)) {
    return fail(RADICLE_STATUS_PARSE_ERROR, "the display list does not address the movie's tags");
  }
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_swf_frames_open(const radicle_swf* swf, radicle_swf_frames** out_frames) {
  clear_error();
  if (out_frames) *out_frames = nullptr;
  if (swf == nullptr || out_frames == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_frames are required");
  }
  return guarded("unknown exception while building the display list", [&] {
    std::unique_ptr<radicle_swf_frames> frames(new radicle_swf_frames());
    const auto status = build_checked_display(swf->file, frames->display);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    frames->revision = swf->revision;
    frames->built_revision = swf->revision ? swf->revision->revision : 0;
    *out_frames = frames.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_swf_frames_destroy(radicle_swf_frames* frames) { delete frames; }

size_t radicle_swf_frames_timeline_count(const radicle_swf_frames* frames) {
  clear_error();
  if (stale(frames)) {
    fail_stale();
    return 0;
  }
  return frames == nullptr ? 0 : frames->display.timelines.size();
}

radicle_status radicle_swf_frames_timeline_at(const radicle_swf_frames* frames, size_t index,
                                               uint16_t* out_character_id, size_t* out_frame_count) {
  clear_error();
  if (out_character_id) *out_character_id = 0;
  if (out_frame_count) *out_frame_count = 0;
  if (stale(frames)) return fail_stale();
  const auto* found = timeline_at(frames, index);
  if (found == nullptr || out_character_id == nullptr || out_frame_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "frames, an in-range index and both outputs are required");
  }
  *out_character_id = found->character_id;
  *out_frame_count = found->frames.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_frames_instance_count(const radicle_swf_frames* frames, size_t timeline,
                                                 size_t frame, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (stale(frames)) return fail_stale();
  const auto* found = frame_at(frames, timeline, frame);
  if (found == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "frames, an in-range timeline and frame, and out_count are required");
  }
  *out_count = found->instances.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_frames_instance_at(const radicle_swf_frames* frames, size_t timeline,
                                              size_t frame, size_t index,
                                              radicle_swf_frames_instance* out_instance) {
  clear_error();
  if (out_instance) *out_instance = {};
  if (stale(frames)) return fail_stale();
  const auto* found = frame_at(frames, timeline, frame);
  if (found == nullptr || out_instance == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "frames, an in-range timeline and frame, and out_instance are required");
  }
  if (index >= found->instances.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the instance index is out of range");
  }
  const auto& source = found->instances[index];
  radicle_swf_frames_instance instance{};
  instance.instance_id = source.instance_id;
  instance.timeline_frame = source.timeline_frame;
  instance.depth = source.depth;
  instance.character_id = source.character_id;
  instance.timeline_id = source.timeline_id;
  instance.clip_depth = source.clip_depth.value_or(0);
  instance.is_sprite = source.is_sprite ? 1 : 0;
  instance.has_bounds = source.has_bounds ? 1 : 0;
  instance.has_clip_depth = source.clip_depth ? 1 : 0;
  instance.transform = to_transform(source.transform);
  instance.world_transform = to_transform(source.world_transform);
  instance.bounds = to_bounds(source.bounds);
  instance.name = source.name.c_str();
  instance.path = source.path.c_str();
  instance.create_tag_index = static_cast<std::uint32_t>(source.create_tag_index);
  instance.transform_tag_index =
      source.transform_tag_index ? static_cast<std::uint32_t>(*source.transform_tag_index) : UINT32_MAX;
  *out_instance = instance;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_frame_svg(const radicle_swf* swf, uint32_t frame, uint8_t** out_svg,
                                     size_t* out_size) {
  clear_error();
  if (out_svg) *out_svg = nullptr;
  if (out_size) *out_size = 0;
  if (swf == nullptr || out_svg == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_svg and out_size are required");
  }
  return guarded("unknown exception while exporting a frame", [&] {
    fo4::swf::SwfDisplayList display;
    const auto status = build_checked_display(swf->file, display);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::string svg = fo4::swf::frame_to_svg(swf->file, display, frame);
    *out_svg = radicle_swf_detail::copy_text(svg, out_size);
    if (*out_svg == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the SVG text");
    }
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
