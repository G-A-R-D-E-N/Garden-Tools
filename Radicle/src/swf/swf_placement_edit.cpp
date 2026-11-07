#include "fo4/swf/swf_edit.hpp"

#include "fo4/swf/swf_display.hpp"
#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

// Constraint: sprite children never nest, so a flat address is a top tag or one direct child.
bool resolve_flat(const std::vector<SwfTag>& tags, std::size_t flat, SwfFlatTag& out) {
  std::size_t cursor = flat;
  for (std::size_t top = 0; top < tags.size(); ++top) {
    if (cursor == 0) {
      out = SwfFlatTag{top, 0, false};
      return true;
    }
    cursor -= 1;
    const std::vector<SwfTag>& children = tags[top].children;
    for (std::size_t child = 0; child < children.size(); ++child) {
      if (cursor == 0) {
        out = SwfFlatTag{top, child, true};
        return true;
      }
      cursor -= 1;
      const std::size_t nested = subtree_size(children[child].children);
      if (cursor < nested) {
        return false;
      }
      cursor -= nested;
    }
  }
  return false;
}

bool is_flat_placement(std::uint16_t code) noexcept {
  return code == tag::kPlaceObject || code == tag::kPlaceObject2 || code == tag::kPlaceObject3;
}

// Requires the playhead scan and the linear lookup to share this predicate.
bool addresses_tag(const SwfFrameInstance& instance, std::size_t top_tag, bool match_create,
                   bool match_transform) {
  if (instance.timeline_id != 0) {
    return false;
  }
  const bool create_hit = match_create && instance.create_tag_index == top_tag;
  const bool transform_hit = match_transform && instance.transform_tag_index &&
                             *instance.transform_tag_index == top_tag;
  return create_hit || transform_hit;
}

// Invariant: out_instance comes from the linear list the verbs edit; out_addressed tells an empty scan from a refusal.
bool root_instance_for_tag(const SwfFile& file, std::size_t top_tag, bool match_create,
                           bool match_transform, const std::optional<std::uint32_t>& only_frame,
                           std::uint32_t& out_frame, std::uint32_t& out_instance,
                           bool& out_addressed, std::string& message) {
  out_addressed = false;
  SwfDisplayOptions options;
  options.apply_frame_control = true;
  const SwfDisplayList display = build_display_list(file, options);
  if (display.status != SwfTimelineStatus::ok) {
    message = display.message.empty() ? "the display list did not build" : display.message;
    return false;
  }
  if (display.timelines.empty()) {
    message = "the movie has no root timeline";
    return false;
  }
  const std::vector<SwfDisplayFrame>& frames = display.timelines.front().frames;
  std::optional<std::uint32_t> shown;
  const auto scan = [&](std::uint32_t index) {
    for (const auto& instance : frames[index].instances) {
      if (addresses_tag(instance, top_tag, match_create, match_transform)) {
        shown = instance.timeline_frame;
      }
    }
  };
  if (only_frame) {
    if (*only_frame >= frames.size()) {
      message = "the frame is out of range";
      return false;
    }
    scan(*only_frame);
  } else {
    for (std::uint32_t index = 0; index < frames.size(); ++index) {
      scan(index);
      if (shown) {
        break;
      }
    }
  }
  if (!shown) {
    return true;
  }
  out_addressed = true;
  const SwfDisplayList linear = build_display_list(file);
  if (linear.status != SwfTimelineStatus::ok || linear.timelines.empty() ||
      *shown >= linear.timelines.front().frames.size()) {
    message = "the displayed root frame does not exist on the authored timeline";
    return false;
  }
  std::optional<std::uint32_t> instance;
  for (const auto& row : linear.timelines.front().frames[*shown].instances) {
    if (!addresses_tag(row, top_tag, match_create, match_transform)) {
      continue;
    }
    if (instance && *instance != row.instance_id) {
      message = "the placement addresses more than one instance; refusing to guess";
      return false;
    }
    instance = row.instance_id;
  }
  if (!instance) {
    message = "no root-timeline instance is addressed by that placement";
    return false;
  }
  out_frame = *shown;
  out_instance = *instance;
  return true;
}

// Requires code to name a PlaceObject2 or PlaceObject3; the returned offset must start the MATRIX bits.
bool matrix_insert_offset(std::uint16_t code, std::span<const std::byte> body, std::size_t& out) {
  if (code != tag::kPlaceObject2 && code != tag::kPlaceObject3) {
    return false;
  }
  if (body.empty()) {
    return false;
  }
  detail::SwfReader reader(body, 0);
  std::uint8_t flags1 = 0;
  std::uint8_t flags3 = 0;
  if (!reader.read_u8(flags1)) {
    return false;
  }
  if (code == tag::kPlaceObject3 && !reader.read_u8(flags3)) {
    return false;
  }
  std::uint16_t depth = 0;
  if (!reader.read_u16(depth)) {
    return false;
  }
  const bool has_character = (flags1 & 0x02u) != 0;
  const bool has_image = (flags3 & 0x10u) != 0;
  const bool has_class_name = (flags3 & 0x08u) != 0 || (has_image && !has_character);
  if (has_class_name) {
    std::string name;
    if (!reader.read_zstring(name)) {
      return false;
    }
  }
  if (has_character) {
    std::uint16_t character = 0;
    if (!reader.read_u16(character)) {
      return false;
    }
  }
  out = reader.position();
  return true;
}

// Invariant: a missing HasMatrix flag must be set on the copy, so the spliced MATRIX stays addressed.
bool splice_matrix_forgiving(std::uint16_t code, std::span<const std::byte> body,
                             const SwfMatrix& matrix, std::vector<std::byte>& out) {
  SwfMatrixSpan span;
  if (placement_matrix_span(code, body, span)) {
    return splice_matrix(code, body, matrix, out);
  }
  if (code != tag::kPlaceObject2 && code != tag::kPlaceObject3) {
    return false;
  }
  std::size_t start = 0;
  if (!matrix_insert_offset(code, body, start)) {
    return false;
  }
  std::vector<std::byte> encoded;
  if (!encode_matrix_fields(matrix, encoded)) {
    return false;
  }
  out.assign(body.begin(), body.end());
  out.insert(out.begin() + static_cast<std::ptrdiff_t>(start), encoded.begin(), encoded.end());
  out[0] = static_cast<std::byte>(std::to_integer<std::uint8_t>(out[0]) | 0x04u);
  return true;
}

bool replace_flat_top(SwfFile& file, std::size_t top, std::vector<std::byte> payload,
                      std::string& message) {
  SwfTagPlan plan;
  plan.replacements.push_back(SwfTagReplacement{top, std::move(payload)});
  SwfTagPlanResult applied;
  if (!apply_tag_plan(file, plan, applied)) {
    message = "the tag plan was refused";
    return false;
  }
  return true;
}

std::size_t count_show_frames(const std::vector<SwfTag>& tags) {
  std::size_t total = 0;
  for (const auto& tag : tags) {
    if (tag.code == tag::kShowFrame) {
      ++total;
    }
  }
  return total;
}

}  // namespace

bool resolve_flat_tag(const SwfFile& file, std::size_t flat, SwfFlatTag& out) {
  return resolve_flat(file.tags, flat, out);
}

bool set_placement_depth_flat(SwfFile& file, std::size_t flat_tag, std::uint16_t depth,
                              std::string& message) {
  SwfFlatTag addr;
  if (!resolve_flat(file.tags, flat_tag, addr)) {
    message = "the tag index is out of range";
    return false;
  }
  const SwfTag& tag = addr.inside ? file.tags[addr.top].children[addr.child] : file.tags[addr.top];
  if (!is_flat_placement(tag.code)) {
    message = "the tag is not a placement";
    return false;
  }
  if (!addr.inside) {
    std::uint32_t frame = 0;
    std::uint32_t instance = 0;
    bool addressed = false;
    if (!root_instance_for_tag(file, addr.top, true, false, std::nullopt, frame, instance, addressed,
                               message)) {
      return false;
    }
    if (!addressed) {
      message = "no root-timeline instance is addressed by that placement";
      return false;
    }
    SwfDepthReassignResult result;
    if (!reassign_instance_depth(file, frame, instance, depth, result)) {
      message = result.message;
      return false;
    }
    return true;
  }
  std::vector<std::byte> body;
  if (!replace_placement_depth(tag.code, tag.payload, depth, body)) {
    message = "the placement payload is too short";
    return false;
  }
  std::vector<std::byte> payload;
  std::map<std::size_t, std::vector<std::byte>> replaced;
  replaced[addr.child] = std::move(body);
  if (!rebuild_sprite_payload(file.tags[addr.top], replaced, payload)) {
    message = "the sprite payload could not be rebuilt";
    return false;
  }
  return replace_flat_top(file, addr.top, std::move(payload), message);
}

bool set_placement_transform_flat(SwfFile& file, std::size_t flat_tag, const SwfTransform& transform,
                                  std::string& message) {
  SwfFlatTag addr;
  if (!resolve_flat(file.tags, flat_tag, addr)) {
    message = "the tag index is out of range";
    return false;
  }
  const SwfTag& tag = addr.inside ? file.tags[addr.top].children[addr.child] : file.tags[addr.top];
  if (!is_flat_placement(tag.code)) {
    message = "the tag is not a placement";
    return false;
  }
  if (!addr.inside) {
    std::uint32_t frame = 0;
    std::uint32_t instance = 0;
    bool addressed = false;
    if (!root_instance_for_tag(file, addr.top, false, true, std::nullopt, frame, instance, addressed,
                               message)) {
      return false;
    }
    if (addressed) {
      std::vector<SwfInstanceTransformEdit> edits;
      edits.push_back(SwfInstanceTransformEdit{instance, transform});
      SwfTransformEditResult result;
      if (!apply_instance_transforms(file, frame, edits, result)) {
        message = result.message;
        return false;
      }
      return true;
    }
    message.clear();
  }
  SwfMatrix matrix;
  if (!matrix_from_transform(transform, matrix) || encode_matrix(transform).empty()) {
    message = "the transform does not fit the MATRIX fields";
    return false;
  }
  std::vector<std::byte> body;
  if (!splice_matrix_forgiving(tag.code, tag.payload, matrix, body)) {
    message = "the placement body has no MATRIX to rewrite";
    return false;
  }
  if (!addr.inside) {
    return replace_flat_top(file, addr.top, std::move(body), message);
  }
  std::vector<std::byte> payload;
  std::map<std::size_t, std::vector<std::byte>> replaced;
  replaced[addr.child] = std::move(body);
  if (!rebuild_sprite_payload(file.tags[addr.top], replaced, payload)) {
    message = "the sprite payload could not be rebuilt";
    return false;
  }
  return replace_flat_top(file, addr.top, std::move(payload), message);
}

bool insert_placement_keyframe_flat(SwfFile& file, std::size_t flat_tag, std::uint32_t frame,
                                    std::string& message) {
  SwfFlatTag addr;
  if (!resolve_flat(file.tags, flat_tag, addr)) {
    message = "the tag index is out of range";
    return false;
  }
  const SwfTag& tag = addr.inside ? file.tags[addr.top].children[addr.child] : file.tags[addr.top];
  if (tag.code != tag::kPlaceObject2) {
    message = "only a PlaceObject2 placement can carry a keyframe this round";
    return false;
  }
  if (!addr.inside) {
    if (frame >= count_show_frames(file.tags)) {
      message = "the frame is out of range";
      return false;
    }
    std::uint32_t root_frame = 0;
    std::uint32_t instance = 0;
    bool addressed = false;
    if (!root_instance_for_tag(file, addr.top, true, true, frame, root_frame, instance, addressed,
                               message)) {
      return false;
    }
    if (!addressed) {
      message = "no root-timeline instance is addressed by that placement";
      return false;
    }
    SwfKeyframeInsertResult result;
    if (!insert_instance_transform_keyframe(file, root_frame, instance, result)) {
      message = result.message;
      return false;
    }
    return true;
  }
  const SwfTag& sprite = file.tags[addr.top];
  if (sprite.code != tag::kDefineSprite) {
    message = "the placement parent is not a DefineSprite";
    return false;
  }
  std::vector<std::size_t> sprite_frames;
  for (std::size_t index = 0; index < sprite.children.size(); ++index) {
    if (sprite.children[index].code == tag::kShowFrame) {
      sprite_frames.push_back(index);
    }
  }
  if (frame >= sprite_frames.size()) {
    message = "the frame is out of range";
    return false;
  }
  const SwfTag& source = sprite.children[addr.child];
  if (source.payload.size() < 3u) {
    message = "the placement payload is too short";
    return false;
  }
  std::vector<std::byte> move_bytes(source.payload.begin(), source.payload.end());
  if ((std::to_integer<std::uint8_t>(move_bytes[0]) & 0x02u) != 0) {
    if (move_bytes.size() < 5u) {
      message = "the placement payload is too short";
      return false;
    }
    move_bytes.erase(move_bytes.begin() + 3, move_bytes.begin() + 5);
  }
  move_bytes[0] = static_cast<std::byte>((std::to_integer<std::uint8_t>(move_bytes[0]) & ~0x02u) | 0x01u);
  SwfTag move_tag;
  move_tag.code = tag::kPlaceObject2;
  std::vector<std::byte> child_bytes;
  if (!append_tag(child_bytes, move_tag, move_bytes)) {
    message = "the placement payload is too large";
    return false;
  }
  const SwfTag& target = sprite.children[sprite_frames[frame]];
  const std::size_t header_size = target.long_header ? 6 : 2;
  if (target.offset < header_size) {
    message = "the sprite child offset is out of range";
    return false;
  }
  const std::size_t header_start = target.offset - header_size;
  if (header_start > sprite.payload.size()) {
    message = "the sprite child offset is out of range";
    return false;
  }
  std::vector<std::byte> payload;
  payload.reserve(sprite.payload.size() + child_bytes.size());
  payload.insert(payload.end(), sprite.payload.begin(),
                 sprite.payload.begin() + static_cast<std::ptrdiff_t>(header_start));
  payload.insert(payload.end(), child_bytes.begin(), child_bytes.end());
  payload.insert(payload.end(),
                 sprite.payload.begin() + static_cast<std::ptrdiff_t>(header_start),
                 sprite.payload.end());
  return replace_flat_top(file, addr.top, std::move(payload), message);
}

}  // namespace fo4::swf
