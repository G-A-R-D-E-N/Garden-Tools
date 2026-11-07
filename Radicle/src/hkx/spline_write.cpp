#include "fo4/hkx/spline_write.hpp"

#include "spline_eval.hpp"
#include "write_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::size_t kFramesPerBlock = 256;
constexpr std::size_t kStride = kFramesPerBlock - 1;
constexpr std::size_t kMaxSamples = 1u << 20;
constexpr std::uint8_t kPositionQuantisation = 1;
constexpr std::uint8_t kScaleQuantisation = 1;
constexpr std::uint8_t kRotationFormat = 5;
constexpr std::uint8_t kStaticRotationFlag = 1;
constexpr std::uint8_t kSplineRotationFlag = 1 << 4;
constexpr std::uint8_t kDegree = 1;

enum class Kind : std::uint8_t { identity, constant, spline };

using detail::align_up;
using detail::normalised_quaternion;

void add_bytes(std::vector<std::byte>& out, const void* data, std::size_t size) {
  const auto at = out.size();
  out.resize(at + size);
  std::memcpy(out.data() + at, data, size);
}

void add_float(std::vector<std::byte>& out, float value) { add_bytes(out, &value, sizeof value); }

void add_u16(std::vector<std::byte>& out, std::uint16_t value) { add_bytes(out, &value, sizeof value); }

void add_byte(std::vector<std::byte>& out, std::uint8_t value) { out.push_back(static_cast<std::byte>(value)); }

void pad4(std::vector<std::byte>& out) { out.resize(align_up(out.size(), 4), std::byte{0}); }

std::uint16_t quantise16(float value, float min, float max) {
  const float span = max - min;
  if (!(span > 0.0f)) return 0;
  const float scaled = (value - min) / span * 65535.0f;
  if (!(scaled >= 0.0f)) return 0;
  if (scaled >= 65535.0f) return 65535;
  return static_cast<std::uint16_t>(std::lround(scaled));
}

bool is_identity_rotation(const std::array<float, 4>& quat, float tolerance) {
  return std::fabs(quat[0]) <= tolerance && std::fabs(quat[1]) <= tolerance && std::fabs(quat[2]) <= tolerance &&
         std::fabs(std::fabs(quat[3]) - 1.0f) <= tolerance;
}

// Invariant: a degree-1 clamped knot vector of n control points has n + 2 byte-wide knots 0 .. n-1.
void write_knots(std::vector<std::byte>& out, std::size_t points) {
  const auto items = static_cast<std::uint16_t>(points - 1);
  add_bytes(out, &items, sizeof items);
  add_byte(out, kDegree);
  for (std::size_t at = 0; at < points + 2; ++at) {
    add_byte(out, static_cast<std::uint8_t>(at == 0 ? 0 : std::min(at - 1, points - 1)));
  }
}

std::array<Kind, 3> classify_axes(const std::array<std::vector<float>, 3>& axes,
                                  const std::array<float, 3>& neutral, float static_tolerance,
                                  std::array<float, 3>& low, std::array<float, 3>& high,
                                  std::array<float, 3>& constant) {
  std::array<Kind, 3> kinds{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    low[axis] = *std::min_element(axes[axis].begin(), axes[axis].end());
    high[axis] = *std::max_element(axes[axis].begin(), axes[axis].end());
    constant[axis] = 0.5f * (low[axis] + high[axis]);
    if (high[axis] - low[axis] > static_tolerance) {
      kinds[axis] = Kind::spline;
    } else if (std::fabs(low[axis] - neutral[axis]) <= static_tolerance &&
               std::fabs(high[axis] - neutral[axis]) <= static_tolerance) {
      kinds[axis] = Kind::identity;
    } else {
      kinds[axis] = Kind::constant;
    }
  }
  return kinds;
}

// Requires one vector channel of one track in one block; returns the mask flags the reader decodes.
std::uint8_t write_vector(std::vector<std::byte>& out, const std::array<std::vector<float>, 3>& axes,
                          const std::array<float, 3>& neutral, float static_tolerance) {
  std::array<float, 3> low{};
  std::array<float, 3> high{};
  std::array<float, 3> constant{};
  const auto kinds = classify_axes(axes, neutral, static_tolerance, low, high, constant);
  const bool any_spline = kinds[0] == Kind::spline || kinds[1] == Kind::spline || kinds[2] == Kind::spline;

  std::uint8_t flags = 0;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (kinds[axis] == Kind::constant) flags |= static_cast<std::uint8_t>(1U << axis);
    if (kinds[axis] == Kind::spline) flags |= static_cast<std::uint8_t>(1U << (axis + 4));
  }

  if (!any_spline) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (kinds[axis] == Kind::constant) add_float(out, constant[axis]);
    }
    pad4(out);
    return flags;
  }

  // Constraint: the reader aligns to four after the knots and after the control points, so both are padded here.
  write_knots(out, axes[0].size());
  pad4(out);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (kinds[axis] == Kind::spline) {
      add_float(out, low[axis]);
      add_float(out, high[axis]);
    } else if (kinds[axis] == Kind::constant) {
      add_float(out, constant[axis]);
    }
  }
  pad4(out);
  for (std::size_t point = 0; point < axes[0].size(); ++point) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (kinds[axis] != Kind::spline) continue;
      add_u16(out, quantise16(axes[axis][point], low[axis], high[axis]));
    }
  }
  pad4(out);
  return flags;
}

// Requires normalised quaternions in frames order; returns the mask flags the reader decodes.
std::uint8_t write_rotation(std::vector<std::array<float, 4>>& quats, std::vector<std::byte>& out,
                            float static_tolerance) {
  // Constraint: the reader flips a control point whose dot with the previous one is negative, so do the same here.
  for (std::size_t at = 1; at < quats.size(); ++at) {
    const auto& previous = quats[at - 1];
    const float dot = quats[at][0] * previous[0] + quats[at][1] * previous[1] + quats[at][2] * previous[2] +
                      quats[at][3] * previous[3];
    if (dot < 0.0f) {
      for (float& value : quats[at]) value = -value;
    }
  }

  bool identity = true;
  for (const auto& quat : quats) {
    if (!is_identity_rotation(quat, static_tolerance)) {
      identity = false;
      break;
    }
  }
  if (identity) return 0;

  bool constant = true;
  for (std::size_t at = 1; at < quats.size() && constant; ++at) {
    for (std::size_t component = 0; component < 4; ++component) {
      if (std::fabs(quats[at][component] - quats[0][component]) > static_tolerance) {
        constant = false;
        break;
      }
    }
  }
  if (constant) {
    pad4(out);
    for (const float value : quats[0]) add_float(out, value);
    pad4(out);
    return kStaticRotationFlag;
  }

  write_knots(out, quats.size());
  pad4(out);
  for (const auto& quat : quats) {
    for (const float value : quat) add_float(out, value);
  }
  pad4(out);
  return kSplineRotationFlag;
}

std::vector<float> axis_samples(const std::vector<std::array<float, 3>>& samples, std::size_t axis) {
  std::vector<float> values;
  values.reserve(samples.size());
  for (const auto& sample : samples) values.push_back(sample[axis]);
  return values;
}

bool finite_samples(const HkxInterleavedAnimation& animation) {
  for (const auto& sample : animation.translations) {
    for (const float value : sample) {
      if (!std::isfinite(value)) return false;
    }
  }
  for (const auto& sample : animation.rotations) {
    for (const float value : sample) {
      if (!std::isfinite(value)) return false;
    }
  }
  for (const auto& sample : animation.scales) {
    for (const float value : sample) {
      if (!std::isfinite(value)) return false;
    }
  }
  return true;
}

}  // namespace

HkxStatus encode_spline_blob(const HkxInterleavedAnimation& animation, const HkxSplineOptions& options,
                             HkxSplineBlob& out) {
  out = HkxSplineBlob{};
  if (!std::isfinite(animation.duration) || !(animation.duration > 0.0f)) return HkxStatus::BadSection;
  if (!std::isfinite(options.position_tolerance) || !std::isfinite(options.rotation_tolerance) ||
      !std::isfinite(options.scale_tolerance) || !std::isfinite(options.static_tolerance)) {
    return HkxStatus::BadSection;
  }
  if (options.position_tolerance < 0.0f || options.rotation_tolerance < 0.0f ||
      options.scale_tolerance < 0.0f || options.static_tolerance < 0.0f) {
    return HkxStatus::BadSection;
  }
  if (animation.frame_count == 0 || animation.track_count == 0) return HkxStatus::BadSection;
  if (std::max<std::size_t>(animation.frame_count, 2) > kMaxSamples / animation.track_count) {
    return HkxStatus::BadSection;
  }
  const auto given = animation.frame_count * animation.track_count;
  if (animation.translations.size() != given || animation.rotations.size() != given ||
      animation.scales.size() != given) {
    return HkxStatus::BadSection;
  }

  // Constraint: Havok cannot store a one-frame clip, so the single frame is duplicated and stored as two.
  HkxInterleavedAnimation source = animation;
  if (source.frame_count == 1) {
    const auto tracks = source.track_count;
    source.translations.resize(tracks * 2);
    source.rotations.resize(tracks * 2);
    source.scales.resize(tracks * 2);
    for (std::size_t track = 0; track < tracks; ++track) {
      source.translations[tracks + track] = source.translations[track];
      source.rotations[tracks + track] = source.rotations[track];
      source.scales[tracks + track] = source.scales[track];
    }
    source.frame_count = 2;
  }
  if (!finite_samples(source)) return HkxStatus::BadSection;

  HkxSplineBlob blob;
  blob.tracks = source.track_count;
  blob.frames = source.frame_count;
  blob.max_frames_per_block = kFramesPerBlock;
  blob.mask_size = source.track_count * 4;
  blob.blocks = (blob.frames - 2) / kStride + 1;
  blob.duration = source.duration;
  blob.frame_duration = source.duration / static_cast<float>(blob.frames - 1);
  blob.block_duration = blob.frame_duration * static_cast<float>(kStride);
  blob.block_inverse_duration = blob.block_duration > 0.0f ? 1.0f / blob.block_duration : 0.0f;
  blob.block_offsets.assign(blob.blocks, 0);

  for (std::size_t block = 0; block < blob.blocks; ++block) {
    blob.data.resize(align_up(blob.data.size(), 16), std::byte{0});
    blob.block_offsets[block] = static_cast<std::uint32_t>(blob.data.size());
    const std::size_t first = block * kStride;
    if (first >= blob.frames) return HkxStatus::BadSection;
    const std::size_t in_block = std::min(kFramesPerBlock, blob.frames - first);

    std::vector<std::byte> masks(blob.tracks * 4, std::byte{0});
    std::vector<std::byte> channels;
    for (std::size_t track = 0; track < blob.tracks; ++track) {
      std::vector<std::array<float, 3>> translations;
      std::vector<std::array<float, 4>> rotations;
      std::vector<std::array<float, 3>> scales;
      translations.reserve(in_block);
      rotations.reserve(in_block);
      scales.reserve(in_block);
      for (std::size_t sample = 0; sample < in_block; ++sample) {
        const auto index = (first + sample) * blob.tracks + track;
        translations.push_back(source.translations[index]);
        rotations.push_back(normalised_quaternion(source.rotations[index]));
        scales.push_back(source.scales[index]);
      }

      std::array<std::vector<float>, 3> axes{};
      for (std::size_t axis = 0; axis < 3; ++axis) axes[axis] = axis_samples(translations, axis);
      const auto position_flags = write_vector(channels, axes, {0.0f, 0.0f, 0.0f}, options.static_tolerance);
      const auto rotation_flags = write_rotation(rotations, channels, options.static_tolerance);
      for (std::size_t axis = 0; axis < 3; ++axis) axes[axis] = axis_samples(scales, axis);
      const auto scale_flags = write_vector(channels, axes, {1.0f, 1.0f, 1.0f}, options.static_tolerance);

      const auto quant = static_cast<std::uint8_t>(
          static_cast<std::uint8_t>(kPositionQuantisation) |
          static_cast<std::uint8_t>((kRotationFormat & 0x0FU) << 2) |
          static_cast<std::uint8_t>(kScaleQuantisation << 6));
      masks[track * 4 + 0] = static_cast<std::byte>(quant);
      masks[track * 4 + 1] = static_cast<std::byte>(position_flags);
      masks[track * 4 + 2] = static_cast<std::byte>(rotation_flags);
      masks[track * 4 + 3] = static_cast<std::byte>(scale_flags);
    }

    blob.data.insert(blob.data.end(), masks.begin(), masks.end());
    blob.data.insert(blob.data.end(), channels.begin(), channels.end());
  }

  out = std::move(blob);
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
