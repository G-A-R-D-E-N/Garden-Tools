#include "animation_internal.hpp"

#include "hkx_read_internal.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace fo4::hkx::detail {
namespace {

constexpr std::uint64_t kMaxFrameTracks = std::uint64_t{1} << 22;
constexpr std::size_t kLanesPerGroup = 16;
constexpr std::size_t kSamplesPerBlock = 16;
constexpr std::size_t kFrameStride = 15;
constexpr std::size_t kHalfWindowBytes = 16;
constexpr std::uint32_t kMaxResidualWidth = 14;
constexpr std::uint32_t kFixedRangeScaleBits = 0x39000400u;
constexpr std::size_t kSlotsPerBone = 12;
constexpr std::size_t kIntOffsetCount = 9;
constexpr std::size_t kFloatOffsetCount = 3;
constexpr std::size_t kTranslationSlot = 0;
constexpr std::size_t kRotationSlot = 4;
constexpr std::size_t kScaleSlot = 8;
constexpr std::size_t kBlockOffsetLimit = 0x7fffffffu;

using Lane = std::array<int, kSamplesPerBlock>;
using Group = std::array<Lane, kLanesPerGroup>;

struct Region {
  std::size_t start;
  std::size_t end;
};

// Constraint: bit i of the window is bit (i % 8) of the byte i / 8 bytes before `end`; the caller keeps end >= 16.
std::uint32_t window_bits(std::span<const std::byte> data, std::size_t end, std::size_t first, std::size_t count) {
  std::uint32_t value = 0;
  for (std::size_t k = 0; k < count; ++k) {
    const std::size_t bit = first + k;
    const auto byte = std::to_integer<std::uint32_t>(data[end - 1 - bit / 8]);
    value |= ((byte >> (bit % 8)) & 1u) << k;
  }
  return value;
}

int decode_residual(std::uint32_t width, std::uint32_t bits) {
  if (width == 0) return 0;
  const std::int32_t sign = std::int32_t{1} << (width - 1);
  const std::int32_t extended = static_cast<std::int32_t>(bits ^ static_cast<std::uint32_t>(sign)) - sign;
  return static_cast<std::int16_t>(extended * 4);
}

HkxStatus decode_half(std::span<const std::byte> data, std::size_t end, int* residuals) {
  if (end < kHalfWindowBytes || end > data.size()) return HkxStatus::Truncated;
  std::size_t position = 0;
  for (std::size_t pair = 0; pair < 4; ++pair) {
    const std::uint32_t width = window_bits(data, end, position, 4);
    if (width > kMaxResidualWidth) return HkxStatus::UnsupportedLayout;
    residuals[pair * 2] = decode_residual(width, window_bits(data, end, position + 4, width));
    residuals[pair * 2 + 1] = decode_residual(width, window_bits(data, end, position + 4 + width, width));
    position += 4 + 2 * static_cast<std::size_t>(width);
  }
  return HkxStatus::Ok;
}

// Invariant: three wrapping int16 prefix sums, then an arithmetic shift of two, undo the encoder third difference.
Lane reconstruct_samples(const Lane& residuals) {
  std::array<std::int16_t, kSamplesPerBlock> values{};
  for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<std::int16_t>(residuals[i]);
  for (int pass = 0; pass < 3; ++pass) {
    for (std::size_t i = 1; i < values.size(); ++i) {
      values[i] = static_cast<std::int16_t>(values[i] + values[i - 1]);
    }
  }
  Lane out{};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = values[i] >> 2;
  return out;
}

// Requires `lanes` as the count of real lanes in this group: a padding lane is skipped, never decoded.
HkxStatus decode_group(std::span<const std::byte> data, std::size_t start, std::size_t end, std::size_t lanes,
                       Group& out, std::size_t& cursor) {
  if (end < start || end > data.size() || end - start < kLanesPerGroup) return HkxStatus::Truncated;
  out = Group{};
  cursor = start + kLanesPerGroup;
  for (std::size_t track = 0; track < kLanesPerGroup; ++track) {
    const auto descriptor = std::to_integer<std::size_t>(data[start + track]);
    const std::size_t first = (descriptor >> 4) + 1;
    const std::size_t second = (descriptor & 15) + 1;
    if (first + second > end - cursor) return HkxStatus::Truncated;
    if (track >= lanes) {
      cursor += first + second;
      continue;
    }
    Lane residuals{};
    if (const auto status = decode_half(data, cursor + first, residuals.data()); status != HkxStatus::Ok) {
      return status;
    }
    if (const auto status = decode_half(data, cursor + first + second, residuals.data() + 8);
        status != HkxStatus::Ok) {
      return status;
    }
    cursor += first + second;
    out[track] = reconstruct_samples(residuals);
  }
  return HkxStatus::Ok;
}

HkxStatus take_array(const HkxSection& section, std::size_t field, std::size_t pointer_size, std::size_t element_size,
                     std::vector<Region>& regions, Elements& out) {
  if (const auto status = elements_at(section, field, pointer_size, element_size, out); status != HkxStatus::Ok) {
    return status;
  }
  if (out.count == 0) return HkxStatus::Ok;
  const std::size_t end = out.first + out.count * element_size;
  for (const Region& region : regions) {
    if (out.first < region.end && end > region.start) return HkxStatus::BadSection;
  }
  regions.push_back({out.first, end});
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus decode_predictive_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                      const HkxClassLayout& layout, HkxAnimation& out, std::string& feature) {
  const auto refuse = [&feature](const char* text) {
    feature = text;
    return HkxStatus::UnsupportedLayout;
  };
  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return refuse("the hkaAnimation base layout is not in the verified tables");

  const auto type_at = member_field(section, *base, object, "type");
  const auto duration_at = member_field(section, *base, object, "duration");
  const auto tracks_at = member_field(section, *base, object, "numberOfTransformTracks");
  const auto float_tracks_at = member_field(section, *base, object, "numberOfFloatTracks");
  const auto compressed_at = member_field(section, layout, object, "compressedData");
  const auto ints_at = member_field(section, layout, object, "intData");
  const auto int_offsets_at = member_field(section, layout, object, "intArrayOffsets");
  const auto floats_at = member_field(section, layout, object, "floatData");
  const auto float_offsets_at = member_field(section, layout, object, "floatArrayOffsets");
  const auto bones_at = member_field(section, layout, object, "numBones");
  const auto float_slots_at = member_field(section, layout, object, "numFloatSlots");
  const auto frames_at = member_field(section, layout, object, "numFrames");
  const auto boundary_at = member_field(section, layout, object, "firstFloatBlockScaleAndOffsetIndex");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !compressed_at || !ints_at || !int_offsets_at ||
      !floats_at || !float_offsets_at || !bones_at || !float_slots_at || !frames_at || !boundary_at) {
    return refuse("a predictive member is missing from the verified layout");
  }
  if (!in_section(section, *int_offsets_at, kIntOffsetCount * 4) ||
      !in_section(section, *float_offsets_at, kFloatOffsetCount * 4)) {
    return HkxStatus::Truncated;
  }

  out.kind = HkxAnimationKind::Predictive;
  out.type = value_at<std::int32_t>(section.data, *type_at);
  out.duration = value_at<float>(section.data, *duration_at);
  const auto declared_frames = value_at<std::int32_t>(section.data, *frames_at);
  const auto declared_bones = value_at<std::int32_t>(section.data, *bones_at);
  const auto declared_floats = value_at<std::int32_t>(section.data, *float_slots_at);
  if (declared_frames == 0 && declared_bones == 0 && declared_floats == 0) return HkxStatus::Ok;
  if (declared_frames <= 0 || declared_bones < 0 || declared_floats < 0) return HkxStatus::BadSection;
  const auto frames = static_cast<std::size_t>(declared_frames);
  const auto bones = static_cast<std::size_t>(declared_bones);
  const auto floats = static_cast<std::size_t>(declared_floats);
  if (static_cast<std::uint64_t>(frames) * (static_cast<std::uint64_t>(bones) + floats) > kMaxFrameTracks) {
    return HkxStatus::BadSection;
  }
  if (value_at<std::int32_t>(section.data, *tracks_at) != declared_bones ||
      value_at<std::int32_t>(section.data, *float_tracks_at) != declared_floats) {
    return HkxStatus::BadSection;
  }

  std::vector<Region> regions;
  regions.push_back({static_cast<std::size_t>(object.offset), static_cast<std::size_t>(object.offset) + layout.size});
  Elements compressed;
  Elements int_array;
  Elements float_array;
  if (const auto status = take_array(section, *compressed_at, packfile.pointer_size, 1, regions, compressed);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = take_array(section, *ints_at, packfile.pointer_size, 2, regions, int_array);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = take_array(section, *floats_at, packfile.pointer_size, 4, regions, float_array);
      status != HkxStatus::Ok) {
    return status;
  }

  std::vector<std::uint16_t> ints(int_array.count);
  for (std::size_t i = 0; i < ints.size(); ++i) {
    ints[i] = value_at<std::uint16_t>(section.data, int_array.first + 2 * i);
  }
  std::array<std::int64_t, kIntOffsetCount + 1> offsets{};
  for (std::size_t i = 0; i < kIntOffsetCount; ++i) {
    offsets[i] = value_at<std::int32_t>(section.data, *int_offsets_at + 4 * i);
  }
  offsets[kIntOffsetCount] = static_cast<std::int64_t>(ints.size());
  for (std::size_t i = 0; i < kIntOffsetCount; ++i) {
    if (offsets[i] < 0 || offsets[i] > offsets[i + 1]) return HkxStatus::BadSection;
  }
  std::array<std::int64_t, kFloatOffsetCount + 1> float_offsets{};
  for (std::size_t i = 0; i < kFloatOffsetCount; ++i) {
    float_offsets[i] = value_at<std::int32_t>(section.data, *float_offsets_at + 4 * i);
  }
  float_offsets[kFloatOffsetCount] = static_cast<std::int64_t>(float_array.count);
  for (std::size_t i = 0; i < kFloatOffsetCount; ++i) {
    if (float_offsets[i] < 0 || float_offsets[i] > float_offsets[i + 1]) return HkxStatus::BadSection;
  }

  // Constraint: each refusal below is a case the BGS game-decoder evidence leaves unresolved; none is guessed at.
  if (offsets[2] != offsets[1] || value_at<std::int32_t>(section.data, *boundary_at) != 0) {
    return refuse("float/bone boundary sampling");
  }
  const auto dynamic_count = static_cast<std::size_t>(offsets[5] - offsets[4]);
  if (floats != 0 || offsets[6] != offsets[5]) return refuse("float-track and float/bone boundary sampling");
  if (offsets[8] != offsets[6] || float_offsets[1] != float_offsets[0]) return refuse("static track index walks");
  if (offsets[9] != offsets[8]) return refuse("quaternion renormalization index consumption");
  if (bones == 0 || dynamic_count == 0) return refuse("reference/default pose reconstruction");

  const std::size_t groups = (dynamic_count + kLanesPerGroup - 1) / kLanesPerGroup;
  if (static_cast<std::size_t>(offsets[4] - offsets[3]) < groups) return HkxStatus::BadSection;
  if (static_cast<std::size_t>(offsets[3] - offsets[2]) < groups) return HkxStatus::BadSection;
  const auto bitmap = static_cast<std::size_t>(offsets[3]);
  bool fixed_only = true;
  bool ranged_only = true;
  for (std::size_t i = 0; i < dynamic_count; ++i) {
    const bool fixed_range = (ints[bitmap + i / 16] & (1u << (i % 16))) != 0;
    fixed_only = fixed_only && fixed_range;
    ranged_only = ranged_only && !fixed_range;
  }
  if (!fixed_only && !ranged_only) return refuse("mixed fixed/ranged scale-array cursor association");

  const auto scales_count = static_cast<std::size_t>(float_offsets[2] - float_offsets[1]);
  const auto bias_count = static_cast<std::size_t>(float_offsets[3] - float_offsets[2]);
  if (!fixed_only && (scales_count < dynamic_count || bias_count < dynamic_count)) return HkxStatus::BadSection;

  const auto scatter = static_cast<std::size_t>(offsets[4]);
  std::vector<bool> seen(kSlotsPerBone * bones, false);
  for (std::size_t i = 0; i < dynamic_count; ++i) {
    const std::size_t slot = ints[scatter + i];
    if (slot >= seen.size() || seen[slot]) return HkxStatus::BadSection;
    seen[slot] = true;
  }
  for (std::size_t bone = 0; bone < bones; ++bone) {
    for (std::size_t component = 0; component < kSlotsPerBone; ++component) {
      if (component != 3 && component != 11 && !seen[bone * kSlotsPerBone + component]) {
        return refuse("reference/default pose reconstruction");
      }
    }
  }

  const std::size_t blocks = (frames - 1) / kFrameStride + 1;
  const auto block_table = static_cast<std::size_t>(offsets[0]);
  if (static_cast<std::size_t>(offsets[1] - offsets[0]) < 2 * blocks) return HkxStatus::BadSection;

  const std::span<const std::byte> bytes =
      std::span<const std::byte>(section.data).subspan(compressed.first, compressed.count);
  std::vector<Group> decoded;
  std::size_t previous = 0;
  for (std::size_t block = 0; block < blocks; ++block) {
    const std::size_t block_end = static_cast<std::size_t>(ints[block_table + 2 * block]) |
                                  (static_cast<std::size_t>(ints[block_table + 2 * block + 1]) << 16);
    if (block_end > kBlockOffsetLimit) return HkxStatus::BadSection;
    if (block_end <= previous || block_end > compressed.count) return HkxStatus::BadSection;
    std::size_t cursor = previous;
    for (std::size_t group = 0; group < groups; ++group) {
      Group samples;
      if (const auto status =
              decode_group(bytes, cursor, block_end, dynamic_count - group * kLanesPerGroup, samples, cursor);
          status != HkxStatus::Ok) {
        if (status == HkxStatus::UnsupportedLayout) return refuse("a residual width above the supported 0..14 arithmetic");
        return status;
      }
      decoded.push_back(samples);
    }
    if (cursor != block_end) return HkxStatus::BadSection;
    previous = block_end;
  }
  if (previous != compressed.count) return HkxStatus::BadSection;

  const float duration = out.duration;
  if (!std::isfinite(duration) || duration < 0.0f || (frames > 1 && duration <= 0.0f)) return HkxStatus::BadSection;
  out.float_track_count = 0;
  out.frame_count = frames;
  out.blocks.count = blocks;
  out.blocks.max_frames_per_block = kSamplesPerBlock;
  out.frame_duration = frames > 1 ? duration / static_cast<float>(frames - 1) : 0.0f;
  out.blocks.block_duration = static_cast<float>(kFrameStride) * out.frame_duration;

  out.tracks.resize(bones);
  for (HkxAnimationTrack& track : out.tracks) {
    track.translations.reserve(frames);
    track.rotations.reserve(frames);
    track.scales.reserve(frames);
    track.translation_animated = {true, true, true};
    track.rotation_animated = true;
    track.scale_animated = {true, true, true};
  }

  float fixed_scale = 0.0f;
  std::memcpy(&fixed_scale, &kFixedRangeScaleBits, sizeof fixed_scale);
  const auto scale_first = static_cast<std::size_t>(float_offsets[1]);
  const auto bias_first = static_cast<std::size_t>(float_offsets[2]);
  std::vector<float> values(kSlotsPerBone * bones, 0.0f);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const std::size_t block = frame / kFrameStride;
    const std::size_t sample = frame % kFrameStride;
    for (std::size_t lane = 0; lane < dynamic_count; ++lane) {
      const Group& group = decoded[block * groups + lane / kLanesPerGroup];
      const float quantised = static_cast<float>(group[lane % kLanesPerGroup][sample]);
      const float scale =
          fixed_only ? fixed_scale : value_at<float>(section.data, float_array.first + 4 * (scale_first + lane));
      const float bias =
          fixed_only ? 0.0f : value_at<float>(section.data, float_array.first + 4 * (bias_first + lane));
      const float value = quantised * scale + bias;
      if (!std::isfinite(scale) || !std::isfinite(bias) || !std::isfinite(value)) return HkxStatus::BadSection;
      values[ints[scatter + lane]] = value;
    }
    for (std::size_t bone = 0; bone < bones; ++bone) {
      const float* slots = values.data() + bone * kSlotsPerBone;
      HkxAnimationTrack& into = out.tracks[bone];
      into.translations.push_back({slots[kTranslationSlot], slots[kTranslationSlot + 1], slots[kTranslationSlot + 2]});
      into.rotations.push_back(
          {slots[kRotationSlot], slots[kRotationSlot + 1], slots[kRotationSlot + 2], slots[kRotationSlot + 3]});
      into.scales.push_back({slots[kScaleSlot], slots[kScaleSlot + 1], slots[kScaleSlot + 2]});
    }
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx::detail
