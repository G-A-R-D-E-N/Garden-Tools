#include "animation_internal.hpp"

#include "hkx_read_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fo4::hkx::detail {
namespace {

constexpr std::size_t kHeaderWords = 20;
constexpr std::size_t kHeaderBytes = kHeaderWords * 2;
constexpr std::uint64_t kMaxFrameTracks = std::uint64_t{1} << 22;
constexpr std::size_t kSlotsPerBone = 12;
constexpr std::size_t kQuaternionBytes = 6;
constexpr float kRotationStep = 4.3161006e-05f;
constexpr float kRotationBias = 0.70710677f;
constexpr float kQuantisationSlack = 0.001f;

struct Header {
  std::array<std::uint16_t, kHeaderWords> word{};
  std::size_t bones{0};
  std::size_t floats{0};
  std::size_t frames{0};
  float duration{0.0f};
  std::size_t static_scalars{0};
  std::size_t static_rotations{0};
  std::size_t static_floats{0};
  std::size_t dynamic_scalars{0};
  std::size_t dynamic_rotations{0};
  std::size_t dynamic_floats{0};
};

struct Regions {
  std::uint64_t size{0};
  std::vector<std::pair<std::uint64_t, std::uint64_t>> taken;

  HkxStatus add(std::uint64_t start, std::uint64_t length) {
    if (length == 0) return HkxStatus::Ok;
    if (start > size || length > size - start) return HkxStatus::BadSection;
    const std::uint64_t end = start + length;
    for (const auto& region : taken) {
      if (start < region.second && end > region.first) return HkxStatus::BadSection;
    }
    taken.emplace_back(start, end);
    return HkxStatus::Ok;
  }
};

std::size_t align16(std::size_t value) { return (value + 15) & ~std::size_t{15}; }

HkxStatus read_finite(std::span<const std::byte> blob, std::size_t at, float& out) {
  out = value_at<float>(blob, at);
  return std::isfinite(out) ? HkxStatus::Ok : HkxStatus::BadSection;
}

HkxStatus decode_scalar(std::uint16_t value, float scale, float offset, float& out) {
  if (!std::isfinite(scale) || !std::isfinite(offset)) return HkxStatus::BadSection;
  out = static_cast<float>(value) * scale + offset;
  return std::isfinite(out) ? HkxStatus::Ok : HkxStatus::BadSection;
}

// Invariant: the top bit of the first two words names the omitted component, the top bit of the third its sign.
HkxStatus decode_quaternion(std::span<const std::byte> blob, std::size_t at, std::array<float, 4>& out) {
  if (at > blob.size() || kQuaternionBytes > blob.size() - at) return HkxStatus::Truncated;
  const std::array<std::uint16_t, 3> words{value_at<std::uint16_t>(blob, at), value_at<std::uint16_t>(blob, at + 2),
                                           value_at<std::uint16_t>(blob, at + 4)};
  const std::size_t omitted = (words[0] >> 15) | ((words[1] >> 15) << 1);
  float sum = 0.0f;
  for (std::size_t i = 0, next = 0; i < 4; ++i) {
    if (i == omitted) continue;
    const float value = static_cast<float>(words[next++] & 0x7fff) * kRotationStep - kRotationBias;
    out[i] = value;
    sum += value * value;
  }
  const float remaining = 1.0f - sum;
  if (!std::isfinite(remaining) || remaining < -kQuantisationSlack) return HkxStatus::BadSection;
  out[omitted] = std::sqrt(std::max(0.0f, remaining)) * ((words[2] & 0x8000) == 0 ? 1.0f : -1.0f);
  return HkxStatus::Ok;
}

// Requires `slots` to be the occupancy of the target array; a rotation claims four slots from a bone's component 4.
HkxStatus read_indices(std::span<const std::byte> blob, std::size_t at, std::size_t count, bool rotation,
                       std::vector<bool>& slots, std::vector<std::size_t>& out) {
  const std::size_t width = rotation ? 4 : 1;
  out.assign(count, 0);
  for (std::size_t i = 0; i < count; ++i) {
    std::size_t index = value_at<std::uint16_t>(blob, at + 2 * i);
    if (rotation) index *= 4;
    if (index + width > slots.size() || (rotation && index % kSlotsPerBone != 4)) return HkxStatus::BadSection;
    for (std::size_t c = 0; c < width; ++c) {
      if (slots[index + c]) return HkxStatus::BadSection;
      slots[index + c] = true;
    }
    out[i] = index;
  }
  return HkxStatus::Ok;
}

HkxStatus read_header(std::span<const std::byte> blob, Header& header) {
  if (blob.size() < kHeaderBytes) return HkxStatus::Truncated;
  for (std::size_t i = 0; i < kHeaderWords; ++i) header.word[i] = value_at<std::uint16_t>(blob, 2 * i);
  header.bones = header.word[1];
  header.floats = header.word[2];
  header.frames = header.word[3];
  if (const auto status = read_finite(blob, 8, header.duration); status != HkxStatus::Ok) return status;
  if (header.frames < 1 || header.duration < 0.0f || (header.frames > 1 && header.duration <= 0.0f)) {
    return HkxStatus::BadSection;
  }
  if (static_cast<std::uint64_t>(header.frames) * (header.bones + header.floats) > kMaxFrameTracks) {
    return HkxStatus::BadSection;
  }
  header.static_scalars = static_cast<std::size_t>(header.word[6]) + header.word[8];
  header.static_rotations = header.word[7];
  header.static_floats = header.word[9];
  header.dynamic_scalars = static_cast<std::size_t>(header.word[10]) + header.word[12];
  header.dynamic_rotations = header.word[11];
  header.dynamic_floats = header.word[13];
  const std::size_t used = 2 * header.dynamic_scalars + 6 * header.dynamic_rotations + 2 * header.dynamic_floats;
  if (static_cast<std::size_t>(header.word[14]) < used) return HkxStatus::BadSection;
  return HkxStatus::Ok;
}

HkxStatus decode_quantized_blob(std::span<const std::byte> blob, HkxAnimation& out) {
  Header header;
  if (const auto status = read_header(blob, header); status != HkxStatus::Ok) return status;
  const auto& w = header.word;
  const std::size_t bones = header.bones;
  const std::size_t floats = header.floats;
  const std::size_t frames = header.frames;
  const std::size_t ns = header.static_scalars;
  const std::size_t nr = header.static_rotations;
  const std::size_t nf = header.static_floats;
  const std::size_t ds = header.dynamic_scalars;
  const std::size_t dr = header.dynamic_rotations;
  const std::size_t df = header.dynamic_floats;
  const std::size_t stride = w[14];

  const std::size_t static_floats_at = align16(w[17] + 4 * ns + 6 * nr);
  const std::size_t float_offsets_at = align16(w[18] + 4 * ds);
  const std::size_t float_scales_at = align16(w[19] + 4 * ds);
  Regions regions;
  regions.size = blob.size();
  regions.taken.emplace_back(0, kHeaderBytes);
  const std::pair<std::uint64_t, std::uint64_t> table[] = {
      {w[15], 2 * (ns + nr + nf)}, {w[16], 2 * (ds + dr + df)}, {w[17], 4 * ns + 6 * nr},
      {static_floats_at, 4 * nf},  {w[18], 4 * ds},             {w[19], 4 * ds},
      {float_offsets_at, 4 * df},  {float_scales_at, 4 * df},   {w[0], static_cast<std::uint64_t>(stride) * frames}};
  for (const auto& region : table) {
    if (const auto status = regions.add(region.first, region.second); status != HkxStatus::Ok) return status;
  }

  std::vector<bool> occupied(bones * kSlotsPerBone, false);
  std::vector<bool> occupied_floats(floats, false);
  std::vector<std::size_t> static_scalar_slots;
  std::vector<std::size_t> static_rotation_slots;
  std::vector<std::size_t> static_float_slots;
  std::vector<std::size_t> dynamic_scalar_slots;
  std::vector<std::size_t> dynamic_rotation_slots;
  std::vector<std::size_t> dynamic_float_slots;
  if (const auto status = read_indices(blob, w[15], ns, false, occupied, static_scalar_slots); status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_indices(blob, w[15] + 2 * ns, nr, true, occupied, static_rotation_slots);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_indices(blob, w[15] + 2 * (ns + nr), nf, false, occupied_floats, static_float_slots);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_indices(blob, w[16], ds, false, occupied, dynamic_scalar_slots);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_indices(blob, w[16] + 2 * ds, dr, true, occupied, dynamic_rotation_slots);
      status != HkxStatus::Ok) {
    return status;
  }
  if (const auto status = read_indices(blob, w[16] + 2 * (ds + dr), df, false, occupied_floats, dynamic_float_slots);
      status != HkxStatus::Ok) {
    return status;
  }

  // Constraint: a clip that leaves a pose component or a float track unspecified is refused, not defaulted.
  for (std::size_t bone = 0; bone < bones; ++bone) {
    for (std::size_t component = 0; component < kSlotsPerBone; ++component) {
      if (component != 3 && component != 11 && !occupied[bone * kSlotsPerBone + component]) {
        return HkxStatus::UnsupportedLayout;
      }
    }
  }
  if (std::find(occupied_floats.begin(), occupied_floats.end(), false) != occupied_floats.end()) {
    return HkxStatus::UnsupportedLayout;
  }

  std::vector<float> values(bones * kSlotsPerBone, 0.0f);
  std::vector<float> float_values(floats, 0.0f);
  for (std::size_t i = 0; i < ns; ++i) {
    if (const auto status = read_finite(blob, w[17] + 4 * i, values[static_scalar_slots[i]]); status != HkxStatus::Ok) {
      return status;
    }
  }
  for (std::size_t i = 0; i < nr; ++i) {
    std::array<float, 4> rotation{};
    if (const auto status = decode_quaternion(blob, w[17] + 4 * ns + 6 * i, rotation); status != HkxStatus::Ok) {
      return status;
    }
    std::copy(rotation.begin(), rotation.end(), values.begin() + static_cast<std::ptrdiff_t>(static_rotation_slots[i]));
  }
  for (std::size_t i = 0; i < nf; ++i) {
    if (const auto status = read_finite(blob, static_floats_at + 4 * i, float_values[static_float_slots[i]]);
        status != HkxStatus::Ok) {
      return status;
    }
  }

  std::vector<float> scalar_scales(ds);
  std::vector<float> scalar_offsets(ds);
  for (std::size_t i = 0; i < ds; ++i) {
    if (const auto status = read_finite(blob, w[19] + 4 * i, scalar_scales[i]); status != HkxStatus::Ok) return status;
    if (const auto status = read_finite(blob, w[18] + 4 * i, scalar_offsets[i]); status != HkxStatus::Ok) return status;
  }
  std::vector<float> float_scales(df);
  std::vector<float> float_offsets(df);
  for (std::size_t i = 0; i < df; ++i) {
    if (const auto status = read_finite(blob, float_scales_at + 4 * i, float_scales[i]); status != HkxStatus::Ok) {
      return status;
    }
    if (const auto status = read_finite(blob, float_offsets_at + 4 * i, float_offsets[i]); status != HkxStatus::Ok) {
      return status;
    }
  }

  out.duration = header.duration;
  out.frame_count = frames;
  out.frame_duration = frames > 1 ? header.duration / static_cast<float>(frames - 1) : 0.0f;
  out.blocks.count = 1;
  out.blocks.max_frames_per_block = frames;
  out.blocks.block_duration = header.duration;
  out.float_track_count = static_cast<std::int32_t>(floats);
  out.tracks.resize(bones);
  for (HkxAnimationTrack& track : out.tracks) {
    track.translations.reserve(frames);
    track.rotations.reserve(frames);
    track.scales.reserve(frames);
    track.translation_animated = {true, true, true};
    track.rotation_animated = true;
    track.scale_animated = {true, true, true};
  }
  out.float_tracks.assign(frames * floats, 0.0f);

  for (std::size_t frame = 0; frame < frames; ++frame) {
    const std::size_t record = w[0] + frame * stride;
    for (std::size_t i = 0; i < ds; ++i) {
      const auto value = value_at<std::uint16_t>(blob, record + 2 * i);
      if (const auto status = decode_scalar(value, scalar_scales[i], scalar_offsets[i], values[dynamic_scalar_slots[i]]);
          status != HkxStatus::Ok) {
        return status;
      }
    }
    for (std::size_t i = 0; i < dr; ++i) {
      std::array<float, 4> rotation{};
      if (const auto status = decode_quaternion(blob, record + 2 * ds + 6 * i, rotation); status != HkxStatus::Ok) {
        return status;
      }
      std::copy(rotation.begin(), rotation.end(),
                values.begin() + static_cast<std::ptrdiff_t>(dynamic_rotation_slots[i]));
    }
    for (std::size_t i = 0; i < df; ++i) {
      const auto value = value_at<std::uint16_t>(blob, record + 2 * ds + 6 * dr + 2 * i);
      if (const auto status = decode_scalar(value, float_scales[i], float_offsets[i], float_values[dynamic_float_slots[i]]);
          status != HkxStatus::Ok) {
        return status;
      }
    }
    for (std::size_t bone = 0; bone < bones; ++bone) {
      const float* slots = values.data() + bone * kSlotsPerBone;
      HkxAnimationTrack& into = out.tracks[bone];
      into.translations.push_back({slots[0], slots[1], slots[2]});
      into.rotations.push_back({slots[4], slots[5], slots[6], slots[7]});
      into.scales.push_back({slots[8], slots[9], slots[10]});
    }
    for (std::size_t track = 0; track < floats; ++track) out.float_tracks[frame * floats + track] = float_values[track];
  }
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus decode_quantized_animation(const HkxPackfile& packfile, const HkxObjectView& object,
                                     const HkxClassLayout& layout, HkxAnimation& out) {
  const HkxSection& section = packfile.sections[packfile.data_section];
  const HkxClassLayout* base = find_layout("hkaAnimation");
  if (base == nullptr) return HkxStatus::UnsupportedLayout;

  const auto type_at = member_field(section, *base, object, "type");
  const auto duration_at = member_field(section, *base, object, "duration");
  const auto tracks_at = member_field(section, *base, object, "numberOfTransformTracks");
  const auto float_tracks_at = member_field(section, *base, object, "numberOfFloatTracks");
  const auto data_at = member_field(section, layout, object, "data");
  const auto endian_at = member_field(section, layout, object, "endian");
  if (!type_at || !duration_at || !tracks_at || !float_tracks_at || !data_at || !endian_at) {
    return HkxStatus::UnsupportedLayout;
  }

  out.kind = HkxAnimationKind::Quantized;
  out.type = value_at<std::int32_t>(section.data, *type_at);
  out.duration = value_at<float>(section.data, *duration_at);

  Elements blob;
  if (const auto status = elements_at(section, *data_at, packfile.pointer_size, 1, blob); status != HkxStatus::Ok) {
    return status;
  }
  if (blob.count == 0) {
    const bool declares_tracks = value_at<std::int32_t>(section.data, *tracks_at) != 0 ||
                                 value_at<std::int32_t>(section.data, *float_tracks_at) != 0;
    return declares_tracks ? HkxStatus::BadSection : HkxStatus::Ok;
  }
  if (value_at<std::int32_t>(section.data, *endian_at) != 0) return HkxStatus::UnsupportedLayout;

  const std::span<const std::byte> bytes = std::span<const std::byte>(section.data).subspan(blob.first, blob.count);
  if (const auto status = decode_quantized_blob(bytes, out); status != HkxStatus::Ok) return status;
  if (value_at<std::int32_t>(section.data, *tracks_at) != static_cast<std::int32_t>(out.tracks.size()) ||
      value_at<std::int32_t>(section.data, *float_tracks_at) != out.float_track_count) {
    return HkxStatus::BadSection;
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx::detail
