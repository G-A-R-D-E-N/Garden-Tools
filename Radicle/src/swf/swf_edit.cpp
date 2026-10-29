#include "fo4/swf/swf_edit.hpp"

#include "fo4/swf/swf_display.hpp"
#include "swf_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

class BitWriter {
public:
  void write_unsigned(std::uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
      const auto bit = (value >> (count - 1u - index)) & 1u;
      pending_ = static_cast<std::uint8_t>((pending_ << 1) | bit);
      ++filled_;
      if (filled_ == 8) {
        out_.push_back(static_cast<std::byte>(pending_));
        pending_ = 0;
        filled_ = 0;
      }
    }
  }

  std::vector<std::byte> take() {
    if (filled_ != 0) {
      out_.push_back(static_cast<std::byte>(pending_ << (8u - filled_)));
      pending_ = 0;
      filled_ = 0;
    }
    return std::move(out_);
  }

private:
  std::vector<std::byte> out_;
  std::uint8_t pending_{0};
  unsigned filled_{0};
};

// Constraint: a negative value must use one bit more than its magnitude's complement.
unsigned signed_bits(std::int32_t value) noexcept {
  if (value == 0) {
    return 0;
  }
  const auto magnitude =
      value < 0 ? static_cast<std::uint32_t>(~value) : static_cast<std::uint32_t>(value);
  return 33u - static_cast<unsigned>(std::countl_zero(magnitude));
}

bool quantize(double value, double scale, std::int32_t& out) noexcept {
  const double scaled = std::round(value * scale);
  if (!std::isfinite(scaled) || scaled < -2147483648.0 || scaled > 2147483647.0) {
    return false;
  }
  out = static_cast<std::int32_t>(scaled);
  return true;
}

bool matrix_from_transform(const SwfTransform& transform, SwfMatrix& out) noexcept {
  SwfMatrix matrix;
  if (!quantize(transform.scale_x, 65536.0, matrix.scale_x) ||
      !quantize(transform.scale_y, 65536.0, matrix.scale_y) ||
      !quantize(transform.rotate_skew0, 65536.0, matrix.rotate_skew0) ||
      !quantize(transform.rotate_skew1, 65536.0, matrix.rotate_skew1) ||
      !quantize(transform.translate_x, 20.0, matrix.translate_x) ||
      !quantize(transform.translate_y, 20.0, matrix.translate_y)) {
    return false;
  }
  out = matrix;
  return true;
}

// Constraint: flags and widths must be minimal, so an unchanged matrix keeps its bytes.
bool encode_matrix_fields(const SwfMatrix& matrix, std::vector<std::byte>& out) {
  const bool has_scale = matrix.scale_x != 0x10000 || matrix.scale_y != 0x10000;
  const bool has_rotate = matrix.rotate_skew0 != 0 || matrix.rotate_skew1 != 0;
  BitWriter writer;
  writer.write_unsigned(has_scale ? 1u : 0u, 1);
  if (has_scale) {
    unsigned bits = std::max(signed_bits(matrix.scale_x), signed_bits(matrix.scale_y));
    if (bits == 0) {
      bits = 1;
    }
    if (bits > 31) {
      return false;
    }
    writer.write_unsigned(bits, 5);
    writer.write_unsigned(static_cast<std::uint32_t>(matrix.scale_x), bits);
    writer.write_unsigned(static_cast<std::uint32_t>(matrix.scale_y), bits);
  }
  writer.write_unsigned(has_rotate ? 1u : 0u, 1);
  if (has_rotate) {
    unsigned bits = std::max(signed_bits(matrix.rotate_skew0), signed_bits(matrix.rotate_skew1));
    if (bits == 0) {
      bits = 1;
    }
    if (bits > 31) {
      return false;
    }
    writer.write_unsigned(bits, 5);
    writer.write_unsigned(static_cast<std::uint32_t>(matrix.rotate_skew0), bits);
    writer.write_unsigned(static_cast<std::uint32_t>(matrix.rotate_skew1), bits);
  }
  const unsigned bits = std::max(signed_bits(matrix.translate_x), signed_bits(matrix.translate_y));
  if (bits > 31) {
    return false;
  }
  writer.write_unsigned(bits, 5);
  if (bits != 0) {
    writer.write_unsigned(static_cast<std::uint32_t>(matrix.translate_x), bits);
    writer.write_unsigned(static_cast<std::uint32_t>(matrix.translate_y), bits);
  }
  out = writer.take();
  return true;
}

bool append_tag(std::vector<std::byte>& out, const SwfTag& tag, std::span<const std::byte> payload) {
  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const auto length = static_cast<std::uint32_t>(payload.size());
  const auto code = static_cast<std::uint16_t>((tag.code & 0x3FFu) << 6);
  if (tag.long_header || length >= 0x3Fu) {
    const auto header = static_cast<std::uint16_t>(code | 0x3Fu);
    out.push_back(static_cast<std::byte>(header & 0xFFu));
    out.push_back(static_cast<std::byte>(header >> 8));
    for (unsigned byte = 0; byte < 4; ++byte) {
      out.push_back(static_cast<std::byte>((length >> (8 * byte)) & 0xFFu));
    }
  } else {
    const auto header = static_cast<std::uint16_t>(code | (length & 0x3Fu));
    out.push_back(static_cast<std::byte>(header & 0xFFu));
    out.push_back(static_cast<std::byte>(header >> 8));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return true;
}

// Constraint: the rebuilt body must keep the id, the frame count and every child header.
bool assemble_sprite_body(const SwfTag& sprite,
                          const std::map<std::size_t, std::vector<std::byte>>& replaced,
                          std::vector<std::byte>& out) {
  if (!sprite.sprite_id.has_value() || !sprite.sprite_frame_count.has_value()) {
    return false;
  }
  out.clear();
  out.push_back(static_cast<std::byte>(*sprite.sprite_id & 0xFFu));
  out.push_back(static_cast<std::byte>((*sprite.sprite_id >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>(*sprite.sprite_frame_count & 0xFFu));
  out.push_back(static_cast<std::byte>((*sprite.sprite_frame_count >> 8) & 0xFFu));
  for (std::size_t index = 0; index < sprite.children.size(); ++index) {
    const SwfTag& child = sprite.children[index];
    const auto found = replaced.find(index);
    const std::span<const std::byte> payload =
        found == replaced.end() ? std::span<const std::byte>(child.payload)
                                : std::span<const std::byte>(found->second);
    if (!append_tag(out, child, payload)) {
      return false;
    }
  }
  return true;
}

// Precondition: the unedited rebuild must equal the body, so an edit cannot drop bytes it does not explain.
bool rebuild_sprite_payload(const SwfTag& sprite,
                            const std::map<std::size_t, std::vector<std::byte>>& replaced,
                            std::vector<std::byte>& out) {
  std::vector<std::byte> base;
  if (!assemble_sprite_body(sprite, {}, base) || base != sprite.payload) {
    return false;
  }
  return assemble_sprite_body(sprite, replaced, out);
}

// Constraint: the caller passes an already quantized MATRIX so an edit cannot re-round itself.
bool splice_matrix(std::uint16_t tag_code, std::span<const std::byte> body, const SwfMatrix& matrix,
                   std::vector<std::byte>& out) {
  SwfMatrixSpan span;
  if (!placement_matrix_span(tag_code, body, span)) {
    return false;
  }
  std::vector<std::byte> encoded;
  if (!encode_matrix_fields(matrix, encoded)) {
    return false;
  }
  out.clear();
  out.reserve(body.size() - (span.end - span.begin) + encoded.size());
  out.insert(out.end(), body.begin(), body.begin() + static_cast<std::ptrdiff_t>(span.begin));
  out.insert(out.end(), encoded.begin(), encoded.end());
  out.insert(out.end(), body.begin() + static_cast<std::ptrdiff_t>(span.end), body.end());
  return true;
}

}  // namespace

bool placement_matrix_span(std::uint16_t tag_code, std::span<const std::byte> body,
                           SwfMatrixSpan& out) {
  std::size_t start = 0;
  if (tag_code == tag::kPlaceObject) {
    // MATRIX offset follows character and depth in a v1 PlaceObject.
    if (body.size() < 4) {
      return false;
    }
    start = 4;
  } else if (tag_code == tag::kPlaceObject2 || tag_code == tag::kPlaceObject3) {
    if (body.empty()) {
      return false;
    }
    const auto flags = std::to_integer<std::uint8_t>(body[0]);
    if ((flags & 0x04u) == 0) {
      return false;
    }
    detail::SwfReader reader(body, 0);
    std::uint8_t flags1 = 0;
    std::uint8_t flags3 = 0;
    if (!reader.read_u8(flags1)) {
      return false;
    }
    if (tag_code == tag::kPlaceObject3 && !reader.read_u8(flags3)) {
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
    start = reader.position();
  } else {
    return false;
  }

  detail::BitReader bits(body, start * 8);
  SwfMatrix matrix;
  if (!detail::read_matrix(bits, matrix)) {
    return false;
  }
  if (bits.byte_position() <= start) {
    return false;
  }
  out.begin = start;
  out.end = bits.byte_position();
  return true;
}

std::vector<std::byte> encode_matrix(const SwfTransform& transform) {
  SwfMatrix matrix;
  if (!matrix_from_transform(transform, matrix)) {
    return {};
  }
  std::vector<std::byte> out;
  if (!encode_matrix_fields(matrix, out)) {
    return {};
  }
  return out;
}

bool replace_placement_transform(std::uint16_t tag_code, std::span<const std::byte> body,
                                 const SwfTransform& transform, std::vector<std::byte>& out) {
  SwfMatrix matrix;
  if (!matrix_from_transform(transform, matrix)) {
    return false;
  }
  return splice_matrix(tag_code, body, matrix, out);
}

namespace {

std::vector<SwfInstanceEditTarget> instance_targets(const SwfDisplayList& display,
                                                    std::uint32_t root_frame) {
  std::vector<SwfInstanceEditTarget> out;
  for (const auto& placement : display.placements) {
    if (placement.frame != root_frame) {
      continue;
    }
    SwfInstanceEditTarget target;
    target.instance_id = placement.instance_id;
    target.path = placement.path;
    target.inside_sprite = placement.inside_sprite;
    target.sprite_id = placement.sprite_id;
    target.sprite_tag_index = placement.sprite_tag_index;
    target.has_authored_matrix = placement.has_authored_matrix;
    target.transform_tag_index = placement.transform_tag_index;
    target.create_tag_index = placement.create_tag_index;
    out.push_back(std::move(target));
  }
  return out;
}

}  // namespace

std::vector<SwfInstanceEditTarget> build_instance_edit_targets(const SwfFile& file,
                                                               std::uint32_t root_frame) {
  SwfDisplayOptions options;
  options.record_placements = true;
  const SwfDisplayList display = build_display_list(file, options);
  if (display.status != SwfTimelineStatus::ok) {
    return {};
  }
  return instance_targets(display, root_frame);
}

bool apply_instance_transforms(SwfFile& file, std::uint32_t root_frame,
                               const std::vector<SwfInstanceTransformEdit>& edits,
                               SwfTransformEditResult& out) {
  out = {};
  if (edits.empty()) {
    return true;
  }
  SwfDisplayOptions options;
  options.record_placements = true;
  const SwfDisplayList display = build_display_list(file, options);
  if (display.status != SwfTimelineStatus::ok) {
    out.message = "the movie timeline could not be walked";
    return false;
  }
  if (display.timelines.empty() || root_frame >= display.timelines.front().frames.size()) {
    out.message = "the root frame index is out of range";
    return false;
  }
  const auto targets = instance_targets(display, root_frame);
  if (targets.empty()) {
    out.message = "the root frame has no instances";
    return false;
  }

  // Constraint: one key per authored placement, so shared bytes cannot disagree.
  std::map<std::tuple<bool, std::size_t, std::size_t>, SwfMatrix> wanted;
  for (const auto& edit : edits) {
    const auto found = std::find_if(targets.begin(), targets.end(),
                                    [&](const SwfInstanceEditTarget& target) {
                                      return target.instance_id == edit.instance_id;
                                    });
    if (found == targets.end()) {
      out.message = "instance id not found on that frame";
      return false;
    }
    if (!found->has_authored_matrix) {
      out.message = "instance has no editable explicit matrix";
      return false;
    }
    SwfMatrix matrix;
    if (!matrix_from_transform(edit.transform, matrix)) {
      out.message = "the transform does not fit the MATRIX fields";
      return false;
    }
    std::vector<std::byte> encoded;
    if (!encode_matrix_fields(matrix, encoded)) {
      out.message = "the transform does not fit the MATRIX fields";
      return false;
    }
    const auto key =
        std::make_tuple(found->inside_sprite, found->sprite_tag_index, found->transform_tag_index);
    const auto inserted = wanted.emplace(key, matrix);
    if (!inserted.second && !(inserted.first->second == matrix)) {
      out.message = "conflicting transforms target the same shared authored placement";
      return false;
    }
  }

  std::map<std::size_t, std::vector<std::byte>> replacements;
  std::map<std::size_t, std::map<std::size_t, std::vector<std::byte>>> sprite_children;
  for (const auto& entry : wanted) {
    const auto& [inside_sprite, sprite_tag_index, tag_index] = entry.first;
    const SwfTag* tag = nullptr;
    if (inside_sprite) {
      if (sprite_tag_index >= file.tags.size()) {
        out.message = "the sprite tag index is out of range";
        return false;
      }
      const SwfTag& sprite = file.tags[sprite_tag_index];
      if (tag_index >= sprite.children.size()) {
        out.message = "the sprite placement index is out of range";
        return false;
      }
      tag = &sprite.children[tag_index];
    } else {
      if (tag_index >= file.tags.size()) {
        out.message = "the placement tag index is out of range";
        return false;
      }
      tag = &file.tags[tag_index];
    }
    // The visible matrix already equals the request, so the authored encoding must not be normalised.
    if (tag->placement && tag->placement->matrix && *tag->placement->matrix == entry.second) {
      continue;
    }
    std::vector<std::byte> body;
    if (!splice_matrix(tag->code, tag->payload, entry.second, body)) {
      out.message = "the placement body has no MATRIX to rewrite";
      return false;
    }
    if (inside_sprite) {
      sprite_children[sprite_tag_index][tag_index] = std::move(body);
    } else {
      replacements[tag_index] = std::move(body);
    }
  }

  for (const auto& entry : sprite_children) {
    std::vector<std::byte> payload;
    if (!rebuild_sprite_payload(file.tags[entry.first], entry.second, payload)) {
      out.message = "the sprite payload could not be rebuilt";
      return false;
    }
    if (payload == file.tags[entry.first].payload) {
      continue;
    }
    replacements[entry.first] = std::move(payload);
  }

  if (replacements.empty()) {
    return true;
  }

  SwfTagPlan plan;
  plan.replacements.reserve(replacements.size());
  for (const auto& entry : replacements) {
    plan.replacements.push_back(SwfTagReplacement{entry.first, entry.second});
  }
  SwfTagPlanResult applied;
  if (!apply_tag_plan(file, plan, applied)) {
    out.message = "the tag plan was refused";
    return false;
  }
  out.replaced_tag_indices.reserve(replacements.size());
  for (const auto& entry : replacements) {
    out.replaced_tag_indices.push_back(entry.first);
  }
  return true;
}

}  // namespace fo4::swf
