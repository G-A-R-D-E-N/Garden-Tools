#include "animation_internal.hpp"

#include "hkx_read_internal.hpp"
#include "spline_eval.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::hkx::detail {
namespace {

using detail::member_field;

// Invariant: spline streams pad to file offsets, so the cursor carries the section's file offset.
struct Cursor {
  std::span<const std::byte> bytes;
  std::size_t at{0};
  std::size_t limit{0};  // invariant: never read past the animated block's data array
  std::size_t origin{0};

  bool has(std::size_t width) const { return at <= limit && width <= limit - at; }

  void align(std::size_t to) { at = align_up(origin + at, to) - origin; }
};

struct AxisChannel {
  ChannelKind kind{ChannelKind::Identity};
  float min{0.0f};
  float max{0.0f};
};

// Invariant: the degree must fit the fixed basis buffer, so a higher degree is a bad block.
constexpr std::uint8_t kMaxSplineDegree = 7;

// Invariant: a frame of one track decodes to forty bytes, so this cap bounds every array a file sizes.
constexpr std::size_t kMaxDecodedSamples = 1u << 20;

HkxStatus read_knots(Cursor& cursor, std::vector<float>& knots, std::uint16_t& items, std::uint8_t& degree) {
  if (!cursor.has(3)) return HkxStatus::Truncated;
  items = value_at<std::uint16_t>(cursor.bytes, cursor.at);
  degree = std::to_integer<std::uint8_t>(cursor.bytes[cursor.at + 2]);
  cursor.at += 3;
  if (degree > kMaxSplineDegree) return HkxStatus::BadSection;
  const std::size_t count = static_cast<std::size_t>(items) + degree + 2;
  if (!cursor.has(count)) return HkxStatus::Truncated;
  knots.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    knots[i] = static_cast<float>(std::to_integer<std::uint8_t>(cursor.bytes[cursor.at + i]));
  }
  cursor.at += count;
  return HkxStatus::Ok;
}

std::optional<float> read_scalar(Cursor& cursor, int quantisation, float min, float max) {
  const std::size_t width = quantisation == 0 ? 1 : 2;
  if (!cursor.has(width)) return std::nullopt;
  const float value = width == 1
                          ? read_quantised8(std::to_integer<std::uint8_t>(cursor.bytes[cursor.at]), min, max)
                          : read_quantised16(value_at<std::uint16_t>(cursor.bytes, cursor.at), min, max);
  cursor.at += width;
  return value;
}

// Requires one vector channel (translation or scale); writes exactly `frames` samples.
HkxStatus decode_vector_channel(Cursor& cursor, const std::array<ChannelKind, 3>& kinds, int quantisation,
                                float identity, std::size_t frames, std::vector<std::array<float, 3>>& out) {
  const bool any_spline =
      kinds[0] == ChannelKind::Spline || kinds[1] == ChannelKind::Spline || kinds[2] == ChannelKind::Spline;

  if (!any_spline) {
    std::array<float, 3> value{identity, identity, identity};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (kinds[axis] != ChannelKind::Static) continue;
      if (!cursor.has(4)) return HkxStatus::Truncated;
      value[axis] = value_at<float>(cursor.bytes, cursor.at);
      cursor.at += 4;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) out.push_back(value);
    return HkxStatus::Ok;
  }

  std::vector<float> knots;
  std::uint16_t items = 0;
  std::uint8_t degree = 0;
  if (const auto status = read_knots(cursor, knots, items, degree); status != HkxStatus::Ok) return status;
  cursor.align(4);

  std::array<AxisChannel, 3> channels{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    channels[axis].kind = kinds[axis];
    if (kinds[axis] == ChannelKind::Spline) {
      if (!cursor.has(8)) return HkxStatus::Truncated;
      channels[axis].min = value_at<float>(cursor.bytes, cursor.at);
      channels[axis].max = value_at<float>(cursor.bytes, cursor.at + 4);
      cursor.at += 8;
    } else if (kinds[axis] == ChannelKind::Static) {
      if (!cursor.has(4)) return HkxStatus::Truncated;
      channels[axis].min = value_at<float>(cursor.bytes, cursor.at);
      channels[axis].max = channels[axis].min;
      cursor.at += 4;
    }
  }

  const std::size_t points = static_cast<std::size_t>(items) + 1;
  std::array<std::vector<float>, 3> control_points;
  for (std::size_t point = 0; point < points; ++point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (channels[axis].kind != ChannelKind::Spline) continue;
      const auto value = read_scalar(cursor, quantisation, channels[axis].min, channels[axis].max);
      if (!value) return HkxStatus::Truncated;
      control_points[axis].push_back(*value);
    }
  }
  cursor.align(4);

  for (std::size_t frame = 0; frame < frames; ++frame) {
    std::array<float, 3> value{identity, identity, identity};
    const float at = static_cast<float>(frame);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (channels[axis].kind == ChannelKind::Spline && !control_points[axis].empty()) {
        const int span = find_knot_span(degree, at, static_cast<int>(control_points[axis].size()), knots);
        value[axis] = evaluate_scalar(span, degree, at, knots, control_points[axis]);
      } else if (channels[axis].kind == ChannelKind::Static) {
        value[axis] = channels[axis].min;
      }
    }
    out.push_back(value);
  }
  return HkxStatus::Ok;
}

HkxStatus decode_rotation_channel(Cursor& cursor, const TrackMask& mask, std::size_t frames,
                                  std::vector<std::array<float, 4>>& out) {
  const int format = mask.rot_quantisation;
  const int alignment = rotation_align(format);
  const std::size_t width = static_cast<std::size_t>(rotation_width(format));

  if (mask.rot == ChannelKind::Spline) {
    std::vector<float> knots;
    std::uint16_t items = 0;
    std::uint8_t degree = 0;
    if (const auto status = read_knots(cursor, knots, items, degree); status != HkxStatus::Ok) return status;
    if (alignment > 1) cursor.align(static_cast<std::size_t>(alignment));

    const std::size_t points = static_cast<std::size_t>(items) + 1;
    std::vector<std::array<float, 4>> control_points;
    control_points.reserve(points);
    for (std::size_t point = 0; point < points; ++point) {
      if (!cursor.has(width)) return HkxStatus::Truncated;
      std::array<float, 4> quaternion = decode_quat(format, cursor.bytes, cursor.at);
      if (!control_points.empty()) {
        const auto& previous = control_points.back();
        const float dot = quaternion[0] * previous[0] + quaternion[1] * previous[1] +
                          quaternion[2] * previous[2] + quaternion[3] * previous[3];
        if (dot < 0.0f) {
          for (float& value : quaternion) value = -value;
        }
      }
      control_points.push_back(quaternion);
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const float at = static_cast<float>(frame);
      const int span = find_knot_span(degree, at, static_cast<int>(control_points.size()), knots);
      out.push_back(evaluate_quat(span, degree, at, knots, control_points));
    }
    return HkxStatus::Ok;
  }

  if (alignment > 1) cursor.align(static_cast<std::size_t>(alignment));
  if (mask.rot == ChannelKind::Static) {
    if (!cursor.has(width)) return HkxStatus::Truncated;
    const std::array<float, 4> quaternion = decode_quat(format, cursor.bytes, cursor.at);
    for (std::size_t frame = 0; frame < frames; ++frame) out.push_back(quaternion);
    return HkxStatus::Ok;
  }

  const std::array<float, 4> identity{0.0f, 0.0f, 0.0f, 1.0f};
  for (std::size_t frame = 0; frame < frames; ++frame) out.push_back(identity);
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus decode_spline_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                  const HkxClassLayout& layout, HkxAnimation& out) {
  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxStatus::UnsupportedLayout;

  const auto type_at = member_field(section, *base, object, "type");
  const auto duration_at = member_field(section, *base, object, "duration");
  const auto tracks_at = member_field(section, *base, object, "numberOfTransformTracks");
  const auto float_tracks_at = member_field(section, *base, object, "numberOfFloatTracks");
  const auto frames_at = member_field(section, layout, object, "numFrames");
  const auto blocks_at = member_field(section, layout, object, "numBlocks");
  const auto block_frames_at = member_field(section, layout, object, "maxFramesPerBlock");
  const auto mask_size_at = member_field(section, layout, object, "maskAndQuantizationSize");
  const auto block_duration_at = member_field(section, layout, object, "blockDuration");
  const auto frame_duration_at = member_field(section, layout, object, "frameDuration");
  const auto block_offsets_at = member_field(section, layout, object, "blockOffsets");
  const auto data_at = member_field(section, layout, object, "data");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !frames_at || !blocks_at ||
      !block_frames_at || !mask_size_at || !block_duration_at || !frame_duration_at || !block_offsets_at ||
      !data_at) {
    return HkxStatus::UnsupportedLayout;
  }

  out.kind = HkxAnimationKind::Spline;
  out.type = value_at<std::int32_t>(section.data, *type_at);
  out.duration = value_at<float>(section.data, *duration_at);
  out.float_track_count = value_at<std::int32_t>(section.data, *float_tracks_at);
  const auto declared_tracks = value_at<std::int32_t>(section.data, *tracks_at);
  const auto declared_frames = value_at<std::int32_t>(section.data, *frames_at);
  const auto declared_blocks = value_at<std::int32_t>(section.data, *blocks_at);
  const auto declared_block_frames = value_at<std::int32_t>(section.data, *block_frames_at);
  const auto mask_size = value_at<std::int32_t>(section.data, *mask_size_at);
  if (declared_tracks < 0 || declared_frames < 0 || declared_blocks < 0 || declared_block_frames < 0 ||
      mask_size < 0) {
    return HkxStatus::BadSection;
  }

  out.frame_duration = value_at<float>(section.data, *frame_duration_at);
  out.blocks.count = static_cast<std::size_t>(declared_blocks);
  out.blocks.max_frames_per_block = static_cast<std::size_t>(declared_block_frames);
  out.blocks.block_duration = value_at<float>(section.data, *block_duration_at);

  std::size_t frames = static_cast<std::size_t>(declared_frames);
  if (frames == 0 && out.frame_duration > 0.0f && out.duration > 0.0f) {
    // Requires the derived count to stay in range: a huge duration must be refused, not converted.
    const float ratio = out.duration / out.frame_duration + 0.5f;
    if (!(ratio >= 0.0f) || ratio >= static_cast<float>(kMaxDecodedSamples)) return HkxStatus::BadSection;
    frames = static_cast<std::size_t>(ratio) + 1;
  }
  out.frame_count = frames;
  const auto tracks = static_cast<std::size_t>(declared_tracks);
  if (tracks == 0) return HkxStatus::Ok;

  // Requires every declared shape to be refused before an array is sized from it.
  if (tracks > kMaxDecodedSamples) return HkxStatus::BadSection;
  if (frames > kMaxDecodedSamples / tracks) return HkxStatus::BadSection;
  if (frames == 0) {
    out.tracks.resize(tracks);
    return HkxStatus::Ok;
  }

  Elements offset_elements;
  if (const auto status = elements_at(section, *block_offsets_at, packfile.pointer_size, 4, offset_elements);
      status != HkxStatus::Ok) {
    return status;
  }
  Elements blob;
  if (const auto status = elements_at(section, *data_at, packfile.pointer_size, 1, blob); status != HkxStatus::Ok) {
    return status;
  }
  if (out.blocks.count > offset_elements.count) return HkxStatus::Truncated;

  // Invariant: the block geometry must cover every frame, so a short frame count is a bad section.
  if (out.blocks.count == 0 || out.blocks.max_frames_per_block == 0) return HkxStatus::BadSection;
  const std::size_t blocks_needed = frames / out.blocks.max_frames_per_block +
                                    (frames % out.blocks.max_frames_per_block == 0 ? 0 : 1);
  if (blocks_needed > out.blocks.count) return HkxStatus::BadSection;

  // Requires room for one four-byte mask per track at the start of a block.
  if (tracks > blob.count / 4) return HkxStatus::Truncated;
  out.tracks.resize(tracks);

  const std::size_t mask_and_quantisation =
      mask_size == 0 ? align_up(4 * tracks, 4) : static_cast<std::size_t>(mask_size);

  for (std::size_t block = 0; block < out.blocks.count; ++block) {
    const std::size_t first_frame = block * out.blocks.max_frames_per_block;
    if (first_frame >= frames) continue;
    // Invariant: only the last block is short, so an overshooting block count fails the frame-count check.
    const std::size_t block_frames =
        block + 1 == out.blocks.count ? frames - first_frame : out.blocks.max_frames_per_block;
    if (block_frames == 0) continue;
    const std::size_t block_offset =
        value_at<std::uint32_t>(section.data, offset_elements.first + block * 4);
    if (block_offset > blob.count) return HkxStatus::Truncated;

    Cursor cursor{section.data, blob.first + block_offset, blob.first + blob.count, section.file_offset};
    if (!cursor.has(4 * tracks)) return HkxStatus::Truncated;
    std::vector<TrackMask> masks;
    masks.reserve(tracks);
    for (std::size_t track = 0; track < tracks; ++track) {
      masks.push_back(decode_mask(cursor.bytes.subspan(cursor.at + track * 4, 4)));
    }
    cursor.at = blob.first + block_offset + mask_and_quantisation;
    if (cursor.at > blob.first + blob.count) return HkxStatus::Truncated;

    for (std::size_t track = 0; track < tracks; ++track) {
      const TrackMask& mask = masks[track];
      HkxAnimationTrack& into = out.tracks[track];
      if (const auto status = decode_vector_channel(cursor, mask.pos, mask.pos_quantisation, 0.0f, block_frames,
                                                    into.translations);
          status != HkxStatus::Ok) {
        return status;
      }
      cursor.align(4);
      if (const auto status = decode_rotation_channel(cursor, mask, block_frames, into.rotations);
          status != HkxStatus::Ok) {
        return status;
      }
      cursor.align(4);
      if (const auto status = decode_vector_channel(cursor, mask.scale, mask.scale_quantisation, 1.0f, block_frames,
                                                    into.scales);
          status != HkxStatus::Ok) {
        return status;
      }
      cursor.align(4);

      for (std::size_t axis = 0; axis < 3; ++axis) {
        into.translation_animated[axis] = mask.pos[axis] != ChannelKind::Identity;
        into.scale_animated[axis] = mask.scale[axis] != ChannelKind::Identity;
      }
      into.rotation_animated = mask.rot != ChannelKind::Identity;
    }
  }

  for (const HkxAnimationTrack& track : out.tracks) {
    if (track.translations.size() != frames || track.rotations.size() != frames || track.scales.size() != frames) {
      return HkxStatus::BadSection;
    }
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx::detail
