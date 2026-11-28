#include "fo4/swf/swf_edit.hpp"

#include "fo4/swf/swf_display.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

bool replace_placement_depth(std::uint16_t tag_code, std::span<const std::byte> body,
                             std::uint16_t depth, std::vector<std::byte>& out) {
  std::size_t offset = 0;
  switch (tag_code) {
    case tag::kPlaceObject:
    case tag::kPlaceObject3:
    case tag::kRemoveObject:
      // Constraint: the depth follows the character in a v1 placement and in a tag-five removal.
      offset = 2;
      break;
    case tag::kPlaceObject2:
      offset = 1;
      break;
    case tag::kRemoveObject2:
      offset = 0;
      break;
    default:
      return false;
  }
  if (body.size() < offset + 2) {
    return false;
  }
  out.assign(body.begin(), body.end());
  out[offset] = static_cast<std::byte>(depth & 0xFFu);
  out[offset + 1] = static_cast<std::byte>(depth >> 8);
  return true;
}

namespace {

struct DepthEvent {
  std::size_t tag_index{0};
  bool remove{false};
  bool move{false};
  std::uint16_t depth{0};
  std::optional<std::uint16_t> character_id;
};

struct ActiveDepth {
  std::size_t placement_tag{0};
  std::uint16_t character_id{0};
};

struct LifetimeScan {
  std::vector<std::size_t> rewrite_tag_indices;
  std::size_t end_tag_index{0};
  std::optional<std::size_t> terminator_tag_index;
};

bool is_placement_tag(std::uint16_t code) noexcept {
  return code == tag::kPlaceObject || code == tag::kPlaceObject2 || code == tag::kPlaceObject3;
}

// Constraint: tag five only removes its own character, so the scan must match it against the live object.
bool removal_matches(const DepthEvent& event, const ActiveDepth& live) noexcept {
  return !event.character_id.has_value() || *event.character_id == live.character_id;
}

bool collect_depth_events(const std::vector<SwfTag>& tags, std::vector<DepthEvent>& out,
                          std::string& message) {
  out.clear();
  for (std::size_t index = 0; index < tags.size(); ++index) {
    const SwfTag& tag = tags[index];
    if (tag.code == tag::kRemoveObject || tag.code == tag::kRemoveObject2) {
      if (!tag.removal) {
        message = "a removal payload did not decode";
        return false;
      }
      DepthEvent event;
      event.tag_index = index;
      event.remove = true;
      event.depth = tag.removal->depth;
      event.character_id = tag.removal->character_id;
      out.push_back(event);
      continue;
    }
    if (!is_placement_tag(tag.code)) {
      continue;
    }
    if (!tag.placement) {
      message = "a placement payload did not decode";
      return false;
    }
    DepthEvent event;
    event.tag_index = index;
    event.move = tag.placement->move;
    event.depth = tag.placement->depth;
    event.character_id = tag.placement->character_id;
    out.push_back(event);
  }
  return true;
}

// Constraint: a lifetime runs from its creating placement to the tag that takes the source depth away.
bool scan_lifetime(const std::vector<DepthEvent>& events, std::size_t start_tag,
                   std::uint16_t source_depth, std::size_t last_tag_index, LifetimeScan& out,
                   std::string& message) {
  out = {};
  std::map<std::uint16_t, ActiveDepth> active;
  bool started = false;
  for (const DepthEvent& event : events) {
    const auto slot = active.find(source_depth);
    const bool ours = started && slot != active.end() && slot->second.placement_tag == start_tag;
    if (!started && event.tag_index == start_tag && slot != active.end() &&
        slot->second.placement_tag != start_tag) {
      message = "the creating placement replaces a live object at the source depth, which would need "
                "a synthesized removal";
      return false;
    }
    if (event.remove) {
      const auto target = active.find(event.depth);
      const bool effective = target != active.end() && removal_matches(event, target->second);
      if (!effective) {
        if (started && ours && event.depth == source_depth) {
          message = "a tag-five removal at the source depth names a different character, which the "
                    "display replay erases anyway";
          return false;
        }
        continue;
      }
      active.erase(target);
      if (started && event.depth == source_depth && ours) {
        out.rewrite_tag_indices.push_back(event.tag_index);
        out.end_tag_index = event.tag_index;
        return true;
      }
      continue;
    }
    if (event.move) {
      const auto target = active.find(event.depth);
      if (target != active.end()) {
        if (event.character_id) {
          target->second = ActiveDepth{event.tag_index, *event.character_id};
        }
      } else if (event.character_id) {
        active[event.depth] = ActiveDepth{event.tag_index, *event.character_id};
      }
    } else if (event.character_id) {
      active[event.depth] = ActiveDepth{event.tag_index, *event.character_id};
    }

    if (event.tag_index == start_tag) {
      if (event.move) {
        message = "the object lifetime begins with a Move that has a character, which would need a "
                  "synthesized removal";
        return false;
      }
      const auto established = active.find(source_depth);
      if (established == active.end() || established->second.placement_tag != start_tag) {
        message = "the creating placement does not establish the resolved source object";
        return false;
      }
      started = true;
      out.rewrite_tag_indices.push_back(event.tag_index);
      continue;
    }
    if (!started || event.depth != source_depth || !ours) {
      continue;
    }
    if (event.move && !event.character_id) {
      out.rewrite_tag_indices.push_back(event.tag_index);
      continue;
    }
    if (event.move) {
      message = "the lifetime ends with a Move that takes a character at the source depth, which the "
                "rewrite would leave referring to an empty depth";
      return false;
    }
    out.end_tag_index = event.tag_index;
    out.terminator_tag_index = event.tag_index;
    return true;
  }
  if (!started) {
    message = "the creating placement tag was not found in the timeline";
    return false;
  }
  out.end_tag_index = last_tag_index;
  return true;
}

// Constraint: a v1, v3 or clip-action tag leaves state the depth rewrite cannot restate.
bool validate_lifetime_state(const std::vector<SwfTag>& tags, const LifetimeScan& lifetime,
                             std::string& message) {
  for (const std::size_t index : lifetime.rewrite_tag_indices) {
    if (index >= tags.size()) {
      message = "a lifetime tag index is out of range";
      return false;
    }
    const SwfTag& tag = tags[index];
    if (tag.code == tag::kPlaceObject) {
      message = "a lifetime PlaceObject version one is not fully modeled by the editor";
      return false;
    }
    if (tag.code == tag::kPlaceObject3) {
      message = "a lifetime PlaceObject version three is not fully modeled by the editor";
      return false;
    }
    if (tag.code == tag::kPlaceObject2 && tag.placement && tag.placement->has_clip_actions) {
      message = "the object lifetime carries clip actions that the editor does not model";
      return false;
    }
  }
  if (lifetime.terminator_tag_index) {
    const std::size_t index = *lifetime.terminator_tag_index;
    if (index < tags.size() && tags[index].placement && tags[index].placement->has_clip_actions) {
      message = "the replacement carries clip actions that the editor does not model";
      return false;
    }
  }
  return true;
}

// Constraint: a destination is unusable once another object holds it on any frame the moved object is live.
bool destination_is_free(const std::vector<SwfTag>& tags, const std::vector<DepthEvent>& events,
                         std::size_t start_tag, std::uint16_t source_depth,
                         std::uint16_t destination_depth) {
  std::map<std::uint16_t, ActiveDepth> active;
  std::size_t next = 0;
  for (std::size_t index = 0; index < tags.size(); ++index) {
    if (next < events.size() && events[next].tag_index == index) {
      const DepthEvent& event = events[next++];
      if (event.remove) {
        const auto target = active.find(event.depth);
        if (target != active.end() && removal_matches(event, target->second)) {
          active.erase(target);
        }
      } else if (event.move) {
        const auto target = active.find(event.depth);
        if (target != active.end()) {
          if (event.character_id) {
            target->second = ActiveDepth{event.tag_index, *event.character_id};
          }
        } else if (event.character_id) {
          active[event.depth] = ActiveDepth{event.tag_index, *event.character_id};
        }
      } else if (event.character_id) {
        active[event.depth] = ActiveDepth{event.tag_index, *event.character_id};
      }
      continue;
    }
    if (tags[index].code != tag::kShowFrame) {
      continue;
    }
    const auto ours = active.find(source_depth);
    if (ours != active.end() && ours->second.placement_tag == start_tag &&
        active.find(destination_depth) != active.end()) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool reassign_instance_depth(SwfFile& file, std::uint32_t root_frame, std::uint32_t instance_id,
                             std::uint16_t destination_depth, SwfDepthReassignResult& out) {
  out = {};
  out.destination_depth = destination_depth;
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
    out.message = "depth reassignment is only supported on the root timeline";
    return false;
  }
  if (found->create_tag_index >= file.tags.size()) {
    out.message = "the creating placement tag index is out of range";
    return false;
  }
  const SwfTag& creating_tag = file.tags[found->create_tag_index];
  if (!creating_tag.placement) {
    out.message = "the creating placement payload did not decode";
    return false;
  }
  const SwfPlacement& creating = *creating_tag.placement;
  const std::uint16_t source_depth = creating.depth;
  out.source_depth = source_depth;
  if (destination_depth == source_depth) {
    out.message = "destination depth equals the source depth";
    return false;
  }
  if (creating.clip_depth && *creating.clip_depth > source_depth) {
    out.message = "the object carries a clip depth (is a mask); the depth reassignment is unsafe";
    return false;
  }

  std::vector<DepthEvent> events;
  if (!collect_depth_events(file.tags, events, out.message)) {
    return false;
  }
  const std::size_t last_tag_index = file.tags.empty() ? 0 : file.tags.size() - 1;
  LifetimeScan lifetime;
  if (!scan_lifetime(events, found->create_tag_index, source_depth, last_tag_index, lifetime,
                     out.message)) {
    return false;
  }
  if (!validate_lifetime_state(file.tags, lifetime, out.message)) {
    return false;
  }

  std::map<std::size_t, std::vector<std::byte>> bodies;
  for (const std::size_t index : lifetime.rewrite_tag_indices) {
    if (index >= file.tags.size()) {
      out.message = "a lifetime tag index is out of range";
      return false;
    }
    std::vector<std::byte> body;
    if (!replace_placement_depth(file.tags[index].code, file.tags[index].payload, destination_depth,
                                 body)) {
      out.message = "a lifetime tag has no writable depth field";
      return false;
    }
    bodies[index] = std::move(body);
  }
  for (const std::size_t index : lifetime.rewrite_tag_indices) {
    if (file.tags[index].placement && file.tags[index].placement->clip_depth) {
      out.message = "the object lifetime carries a clip depth (is a mask); the depth reassignment is unsafe";
      return false;
    }
  }
  for (const SwfTag& tag : file.tags) {
    if (!tag.placement || !tag.placement->clip_depth) {
      continue;
    }
    const std::uint16_t clip_depth = *tag.placement->clip_depth;
    const std::uint16_t mask_depth = tag.placement->depth;
    if (clip_depth <= mask_depth) {
      continue;
    }
    if (mask_depth == source_depth) {
      out.message = "the source depth carries mask state in the authored timeline";
      return false;
    }
    const auto masked = [&](std::uint16_t depth) { return depth > mask_depth && depth <= clip_depth; };
    if (masked(source_depth) || masked(destination_depth)) {
      out.message = "the source or destination depth falls inside a mask range; the depth reassignment is unsafe";
      return false;
    }
  }

  std::set<std::size_t> ours(lifetime.rewrite_tag_indices.begin(),
                             lifetime.rewrite_tag_indices.end());
  if (lifetime.terminator_tag_index) {
    ours.insert(*lifetime.terminator_tag_index);
  }
  const std::size_t interval_end =
      lifetime.terminator_tag_index ? *lifetime.terminator_tag_index + 1 : lifetime.end_tag_index;
  for (const DepthEvent& event : events) {
    if (event.tag_index < found->create_tag_index || event.tag_index > interval_end ||
        ours.find(event.tag_index) != ours.end()) {
      continue;
    }
    if (event.depth == destination_depth) {
      out.message = "a control tag targets the destination depth while the object is live; the depth "
                    "reassignment is unsafe";
      return false;
    }
  }
  if (!destination_is_free(file.tags, events, found->create_tag_index, source_depth,
                           destination_depth)) {
    out.message = "the destination depth is occupied while the object is live; the depth reassignment is unsafe";
    return false;
  }

  SwfTagPlan plan;
  plan.replacements.reserve(bodies.size());
  for (const auto& entry : bodies) {
    plan.replacements.push_back(SwfTagReplacement{entry.first, entry.second});
  }
  if (lifetime.terminator_tag_index) {
    SwfTag removal;
    removal.code = tag::kRemoveObject2;
    removal.length = 2;
    removal.payload = {static_cast<std::byte>(destination_depth & 0xFFu),
                       static_cast<std::byte>(destination_depth >> 8)};
    plan.insertions.push_back(SwfTagInsertion{*lifetime.terminator_tag_index, std::move(removal)});
  }

  // Constraint: the rewrite is proven on a copy first, so a rejected edit leaves the movie byte identical.
  SwfFile preview;
  preview.header = file.header;
  preview.tags = file.tags;
  SwfTagPlanResult preview_applied;
  if (!apply_tag_plan(preview, plan, preview_applied)) {
    out.message = "the tag plan was refused";
    return false;
  }
  // Constraint: the proof must find the moved instance at the destination depth, not only its tag.
  SwfDisplayOptions preview_options;
  preview_options.record_placements = true;
  const SwfDisplayList preview_display = build_display_list(preview, preview_options);
  if (preview_display.status != SwfTimelineStatus::ok || preview_display.timelines.empty()) {
    out.message = "the rewritten movie could not be displayed";
    return false;
  }
  std::optional<std::uint32_t> moved_id;
  for (const auto& placement : preview_display.placements) {
    if (placement.frame == root_frame && !placement.inside_sprite &&
        placement.create_tag_index == found->create_tag_index) {
      moved_id = placement.instance_id;
      break;
    }
  }
  bool at_destination = false;
  const auto& preview_frames = preview_display.timelines.front().frames;
  if (moved_id && root_frame < preview_frames.size()) {
    for (const auto& instance : preview_frames[root_frame].instances) {
      if (instance.instance_id == *moved_id) {
        at_destination = instance.depth == destination_depth;
        break;
      }
    }
  }
  if (!at_destination) {
    out.message = "the reassigned object did not resolve at the destination depth after the rewrite";
    return false;
  }

  SwfTagPlanResult applied;
  if (!apply_tag_plan(file, plan, applied)) {
    out.message = "the tag plan was refused";
    return false;
  }
  out.new_instance_id = *moved_id;
  out.replaced_tag_indices.reserve(bodies.size());
  for (const auto& entry : bodies) {
    out.replaced_tag_indices.push_back(entry.first);
  }
  out.inserted_tag_indices = applied.inserted_tag_indices;
  return true;
}

}  // namespace fo4::swf
