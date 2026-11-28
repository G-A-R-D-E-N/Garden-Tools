#include "fo4/swf/swf_edit.hpp"

#include "fo4/swf/swf_display.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

// Constraint: a frame is completed by its ShowFrame, so the search counts ShowFrames in tag order.
bool show_frame_index(const std::vector<SwfTag>& tags, std::uint32_t frame, std::size_t& out) {
  std::uint32_t current = 0;
  for (std::size_t index = 0; index < tags.size(); ++index) {
    if (tags[index].code != tag::kShowFrame) {
      continue;
    }
    if (current == frame) {
      out = index;
      return true;
    }
    ++current;
  }
  return false;
}

// Constraint: a tag's authored frame is the number of ShowFrames that precede it.
bool tag_frame(const std::vector<SwfTag>& tags, std::size_t tag_index, std::uint32_t& out) {
  if (tag_index >= tags.size()) {
    return false;
  }
  std::uint32_t frame = 0;
  for (std::size_t index = 0; index < tag_index; ++index) {
    if (tags[index].code == tag::kShowFrame) {
      ++frame;
    }
  }
  out = frame;
  return true;
}

// Constraint: only fields the tag authors are replaced, so an omitted field keeps the inherited value.
void merge_placement(SwfAuthoredFrameObject& object, const SwfPlacement& placement,
                     std::size_t tag_index) {
  if (placement.matrix) {
    object.matrix = placement.matrix;
    object.transform_tag_index = tag_index;
  }
  if (placement.name) {
    object.name = *placement.name;
  }
  if (placement.class_name) {
    object.class_name = *placement.class_name;
  }
  if (placement.ratio) {
    object.ratio = placement.ratio;
  }
  if (placement.clip_depth) {
    object.clip_depth = placement.clip_depth;
  }
  if (!placement.filters.empty()) {
    object.filter_count = static_cast<std::uint16_t>(placement.filters.size());
  }
  if (placement.blend_mode) {
    object.blend_mode = placement.blend_mode;
  }
}

void stamp_mask_membership(std::vector<SwfAuthoredFrameObject>& objects) {
  for (auto& object : objects) {
    object.masked_by_depth.reset();
    for (const auto& mask : objects) {
      if (!mask.clip_depth || *mask.clip_depth <= mask.depth) {
        continue;
      }
      if (mask.depth >= object.depth || object.depth > *mask.clip_depth) {
        continue;
      }
      if (!object.masked_by_depth || *object.masked_by_depth < mask.depth) {
        object.masked_by_depth = mask.depth;
      }
    }
  }
}

// Constraint: the comparator must ignore the transform source but catch every other authored field.
bool same_non_transform_state(const SwfAuthoredFrameObject& left,
                              const SwfAuthoredFrameObject& right) {
  return left.depth == right.depth && left.character_id == right.character_id &&
         left.name == right.name && left.class_name == right.class_name &&
         left.ratio == right.ratio && left.clip_depth == right.clip_depth &&
         left.masked_by_depth == right.masked_by_depth &&
         left.filter_count == right.filter_count && left.blend_mode == right.blend_mode;
}

// Constraint: the inserted tag is the canonical matrix-only Move, so it must carry no other state.
bool build_keyframe_tag(std::uint16_t depth, const SwfTransform& transform, SwfTag& out) {
  std::vector<std::byte> matrix = encode_matrix(transform);
  if (matrix.empty()) {
    return false;
  }
  out = {};
  out.code = tag::kPlaceObject2;
  out.payload.reserve(3 + matrix.size());
  out.payload.push_back(static_cast<std::byte>(0x05));
  out.payload.push_back(static_cast<std::byte>(depth & 0xFFu));
  out.payload.push_back(static_cast<std::byte>(depth >> 8));
  out.payload.insert(out.payload.end(), matrix.begin(), matrix.end());
  return true;
}

const SwfAuthoredFrameObject* find_authored_object(const SwfAuthoredFrame& frame,
                                                   std::size_t create_tag_index) {
  const auto found = std::find_if(frame.objects.begin(), frame.objects.end(),
                                  [&](const SwfAuthoredFrameObject& object) {
                                    return object.create_tag_index == create_tag_index;
                                  });
  return found == frame.objects.end() ? nullptr : &*found;
}

bool insert_keyframe(SwfFile& file, std::uint32_t root_frame, std::uint32_t instance_id,
                     const SwfTransform* requested, SwfKeyframeInsertResult& out) {
  out = {};
  out.root_frame = root_frame;
  if (file.header.version < 3) {
    out.message = "transform keyframe insertion requires SWF version 3 or newer";
    return false;
  }
  const auto targets = build_instance_edit_targets(file, root_frame);
  if (targets.empty()) {
    out.message = "the movie has no renderable root frame";
    return false;
  }
  const auto found = std::find_if(targets.begin(), targets.end(),
                                  [&](const SwfInstanceEditTarget& target) {
                                    return target.instance_id == instance_id;
                                  });
  if (found == targets.end()) {
    out.message = "instance id not found on that frame";
    return false;
  }
  if (found->inside_sprite) {
    out.message = "transform keyframe insertion is only supported on the root timeline";
    return false;
  }

  SwfAuthoredFrame before;
  if (!build_authored_frame(file.tags, root_frame, before, out.message)) {
    return false;
  }
  const SwfAuthoredFrameObject* object = find_authored_object(before, found->create_tag_index);
  if (object == nullptr) {
    out.message = "the instance does not resolve in the authored frame";
    return false;
  }
  out.depth = object->depth;

  std::uint32_t source_frame = 0;
  if (object->matrix && tag_frame(file.tags, object->transform_tag_index, source_frame) &&
      source_frame == root_frame) {
    out.message = "the authored frame already has an explicit transform source for this object";
    return false;
  }

  std::size_t show_frame = 0;
  if (!show_frame_index(file.tags, root_frame, show_frame)) {
    out.message = "the timeline has no ShowFrame for that frame";
    return false;
  }
  const SwfTransform transform = requested != nullptr ? *requested : authored_transform(object->matrix);
  SwfTag tag;
  if (!build_keyframe_tag(object->depth, transform, tag)) {
    out.message = "the transform does not fit the MATRIX fields";
    return false;
  }

  // Constraint: the insertion is proven on a copy first, so a refused edit leaves the movie byte identical.
  SwfFile preview;
  preview.header = file.header;
  preview.tags = file.tags;
  SwfTagPlan plan;
  plan.insertions.push_back(SwfTagInsertion{show_frame, std::move(tag)});
  SwfTagPlanResult applied;
  if (!apply_tag_plan(preview, plan, applied)) {
    out.message = "the tag plan was refused";
    return false;
  }

  // Constraint: every authored field the display list does not model must survive the insertion.
  SwfAuthoredFrame after;
  if (!build_authored_frame(preview.tags, root_frame, after, out.message)) {
    return false;
  }
  const SwfAuthoredFrameObject* updated = find_authored_object(after, object->create_tag_index);
  if (updated == nullptr) {
    out.message = "the transform keyframe removed the selected object";
    return false;
  }
  if (!same_non_transform_state(*object, *updated)) {
    out.message = "the transform keyframe would change non-transform authored state";
    return false;
  }
  const SwfTransform authored = authored_transform(updated->matrix);
  if (requested == nullptr) {
    if (!(authored == authored_transform(object->matrix))) {
      out.message = "the transform keyframe would change the authored frame render";
      return false;
    }
  } else if (!updated->matrix) {
    out.message = "the transform keyframe did not establish an explicit matrix source";
    return false;
  }

  // Constraint: the read side must reproduce the whole movie and show the authored transform on that frame.
  const SwfDisplayList display = build_display_list(preview);
  if (display.status != SwfTimelineStatus::ok || display.timelines.empty() ||
      root_frame >= display.timelines.front().frames.size()) {
    out.message = "the movie is not renderable after the keyframe insertion";
    return false;
  }
  const auto& instances = display.timelines.front().frames[root_frame].instances;
  const auto instance = std::find_if(instances.begin(), instances.end(),
                                     [&](const SwfFrameInstance& candidate) {
                                       return candidate.instance_id == instance_id;
                                     });
  if (instance == instances.end()) {
    out.message = "the instance does not resolve after the keyframe insertion";
    return false;
  }
  if (!(instance->transform == authored)) {
    out.message = "the display does not reproduce the authored transform after the insertion";
    return false;
  }

  if (!apply_tag_plan(file, plan, applied)) {
    out.message = "the tag plan was refused";
    return false;
  }
  out.inserted_tag_index = show_frame;
  return true;
}

}  // namespace

SwfTransform authored_transform(const std::optional<SwfMatrix>& matrix) noexcept {
  return matrix ? transform_from_matrix(*matrix) : SwfTransform{};
}

bool build_authored_frame(const std::vector<SwfTag>& tags, std::uint32_t frame,
                          SwfAuthoredFrame& out, std::string& message) {
  out = {};
  std::size_t stop = 0;
  if (!show_frame_index(tags, frame, stop)) {
    message = "the timeline has no ShowFrame for that frame";
    return false;
  }
  std::map<std::uint16_t, SwfAuthoredFrameObject> active;
  for (std::size_t index = 0; index < stop; ++index) {
    const SwfTag& tag = tags[index];
    if (tag.code == tag::kRemoveObject || tag.code == tag::kRemoveObject2) {
      if (!tag.removal) {
        message = "a removal payload did not decode";
        return false;
      }
      active.erase(tag.removal->depth);
      continue;
    }
    if (tag.code != tag::kPlaceObject && tag.code != tag::kPlaceObject2 &&
        tag.code != tag::kPlaceObject3) {
      continue;
    }
    if (!tag.placement) {
      message = "a placement payload did not decode";
      return false;
    }
    const SwfPlacement& placement = *tag.placement;
    // Constraint: a class-only placement has no depth state, so the read side skips it here too.
    if (placement.class_name && !placement.character_id) {
      continue;
    }
    auto existing = active.find(placement.depth);
    if (placement.move && existing == active.end()) {
      message = "a move targets a depth that is not live";
      return false;
    }
    if (!placement.character_id) {
      if (existing == active.end()) {
        message = "a placement without a character has no live depth";
        return false;
      }
      merge_placement(existing->second, placement, index);
      continue;
    }
    SwfAuthoredFrameObject object;
    if (placement.move && existing != active.end()) {
      object = existing->second;
    } else {
      object.depth = placement.depth;
      object.create_tag_index = index;
    }
    object.character_id = *placement.character_id;
    if (placement.move) {
      object.create_tag_index = index;
    }
    merge_placement(object, placement, index);
    active[placement.depth] = std::move(object);
  }

  out.objects.reserve(active.size());
  for (auto& entry : active) {
    out.objects.push_back(std::move(entry.second));
  }
  stamp_mask_membership(out.objects);
  return true;
}

bool insert_instance_transform_keyframe(SwfFile& file, std::uint32_t root_frame,
                                        std::uint32_t instance_id, SwfKeyframeInsertResult& out) {
  return insert_keyframe(file, root_frame, instance_id, nullptr, out);
}

bool insert_instance_transform_keyframe(SwfFile& file, std::uint32_t root_frame,
                                        std::uint32_t instance_id, const SwfTransform& transform,
                                        SwfKeyframeInsertResult& out) {
  return insert_keyframe(file, root_frame, instance_id, &transform, out);
}

}  // namespace fo4::swf
