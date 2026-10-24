#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace fo4::hkx::detail {

enum class ChannelKind : std::uint8_t {
  Identity = 0,
  Static = 1,
  Spline = 2,
};

struct TrackMask {
  int pos_quantisation{0};
  int rot_quantisation{0};
  int scale_quantisation{0};
  std::array<ChannelKind, 3> pos{};
  ChannelKind rot{ChannelKind::Identity};
  std::array<ChannelKind, 3> scale{};
};

inline ChannelKind axis_kind(std::uint8_t flags, int axis) {
  if (((flags >> (axis + 4)) & 1) != 0) return ChannelKind::Spline;
  if (((flags >> axis) & 1) != 0) return ChannelKind::Static;
  return ChannelKind::Identity;
}

inline ChannelKind rotation_kind(std::uint8_t flags) {
  if (((flags >> 4) & 0x0F) != 0) return ChannelKind::Spline;
  if ((flags & 0x0F) != 0) return ChannelKind::Static;
  return ChannelKind::Identity;
}

// Requires the four mask bytes of one track: quantisation bits, then position, rotation and scale flags.
inline TrackMask decode_mask(std::span<const std::byte> bytes) {
  const auto b0 = std::to_integer<std::uint8_t>(bytes[0]);
  const auto pos_flags = std::to_integer<std::uint8_t>(bytes[1]);
  const auto rot_flags = std::to_integer<std::uint8_t>(bytes[2]);
  const auto scale_flags = std::to_integer<std::uint8_t>(bytes[3]);
  TrackMask mask;
  mask.pos_quantisation = b0 & 0x03;
  mask.rot_quantisation = (b0 >> 2) & 0x0F;
  mask.scale_quantisation = (b0 >> 6) & 0x03;
  for (int axis = 0; axis < 3; ++axis) {
    mask.pos[axis] = axis_kind(pos_flags, axis);
    mask.scale[axis] = axis_kind(scale_flags, axis);
  }
  mask.rot = rotation_kind(rot_flags);
  return mask;
}

inline int rotation_align(int format) {
  if (format == 1 || format == 3) return 1;
  if (format == 2 || format == 4) return 2;
  return 4;
}

// Requires one of Havok's six rotation quantisations; an unknown format has no width.
inline int rotation_width(int format) {
  switch (format) {
    case 0:
      return 4;
    case 1:
      return 5;
    case 2:
      return 6;
    case 3:
      return 3;
    case 4:
      return 2;
    case 5:
      return 16;
    default:
      return 0;
  }
}

inline std::size_t align_up(std::size_t value, std::size_t to) {
  const std::size_t over = value % to;
  return over == 0 ? value : value + (to - over);
}

inline float read_quantised8(std::uint8_t raw, float min, float max) {
  return min + (max - min) * (static_cast<float>(raw) / 255.0f);
}

inline float read_quantised16(std::uint16_t raw, float min, float max) {
  return min + (max - min) * (static_cast<float>(raw) / 65535.0f);
}

inline int find_knot_span(int degree, float t, int control_points, std::span<const float> knots) {
  if (control_points <= 0 || knots.empty()) return 0;
  if (t >= knots[static_cast<std::size_t>(control_points)]) return control_points - 1;

  int lo = degree;
  int hi = control_points;
  int mid = (lo + hi) / 2;
  for (int step = 0; step < 100; ++step) {
    if (mid < 0 || static_cast<std::size_t>(mid) + 1 >= knots.size()) break;
    if (t < knots[static_cast<std::size_t>(mid)]) {
      hi = mid;
    } else if (t >= knots[static_cast<std::size_t>(mid) + 1]) {
      lo = mid;
    } else {
      break;
    }
    mid = (lo + hi) / 2;
  }
  return mid;
}

inline void basis(int span, int degree, float t, std::span<const float> knots, std::span<float> into) {
  std::fill(into.begin(), into.end(), 0.0f);
  if (into.empty()) return;
  into[0] = 1.0f;

  for (int i = 1; i <= degree; ++i) {
    for (int j = i - 1; j >= 0; --j) {
      const int high = span + i - j;
      const int low = span - j;
      const bool in_range = high >= 0 && static_cast<std::size_t>(high) < knots.size() && low >= 0;
      const float width = in_range ? knots[static_cast<std::size_t>(high)] - knots[static_cast<std::size_t>(low)] : 0.0f;
      const float along = width >= 1e-10f ? (t - knots[static_cast<std::size_t>(low)]) / width : 0.0f;
      const float scaled = into[static_cast<std::size_t>(j)] * along;
      if (static_cast<std::size_t>(j + 1) < into.size()) {
        into[static_cast<std::size_t>(j + 1)] += into[static_cast<std::size_t>(j)] - scaled;
      }
      into[static_cast<std::size_t>(j)] = scaled;
    }
  }
}

inline float evaluate_scalar(int span, int degree, float t, std::span<const float> knots,
                             std::span<const float> control_points) {
  if (control_points.empty()) return 0.0f;
  if (control_points.size() == 1) return control_points[0];

  std::array<float, 8> weights{};
  basis(span, degree, t, knots, std::span<float>(weights).first(static_cast<std::size_t>(degree) + 1));

  float total = 0.0f;
  for (int i = 0; i <= degree; ++i) {
    const int at = span - i;
    if (at >= 0 && static_cast<std::size_t>(at) < control_points.size()) {
      total += control_points[static_cast<std::size_t>(at)] * weights[static_cast<std::size_t>(i)];
    }
  }
  return total;
}

inline std::array<float, 4> normalize_quat(std::array<float, 4> q) {
  const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (length <= 0.0f) return {0.0f, 0.0f, 0.0f, 1.0f};
  for (float& value : q) value /= length;
  return q;
}

inline std::array<float, 4> evaluate_quat(int span, int degree, float t, std::span<const float> knots,
                                          std::span<const std::array<float, 4>> control_points) {
  if (control_points.empty()) return {0.0f, 0.0f, 0.0f, 1.0f};
  if (control_points.size() == 1) return control_points[0];

  std::array<float, 8> weights{};
  basis(span, degree, t, knots, std::span<float>(weights).first(static_cast<std::size_t>(degree) + 1));

  std::array<float, 4> total{0.0f, 0.0f, 0.0f, 0.0f};
  for (int i = 0; i <= degree; ++i) {
    const int at = span - i;
    if (at < 0 || static_cast<std::size_t>(at) >= control_points.size()) continue;
    const auto& q = control_points[static_cast<std::size_t>(at)];
    for (std::size_t c = 0; c < 4; ++c) total[c] += q[c] * weights[static_cast<std::size_t>(i)];
  }
  return normalize_quat(total);
}

// Requires the block's rotation quantisation format; the width is 2, 3, 4, 5, 6 or 16 bytes.
inline std::array<float, 4> decode_quat(int format, std::span<const std::byte> bytes, std::size_t& off) {
  std::uint64_t raw = 0;
  switch (format) {
    case 5: {
      std::array<float, 4> q{};
      for (std::size_t c = 0; c < 4; ++c) {
        std::memcpy(&q[c], bytes.data() + off + c * 4, sizeof(float));
      }
      off += 16;
      return normalize_quat(q);
    }
    case 0: {
      std::memcpy(&raw, bytes.data() + off, 4);
      off += 4;
      constexpr float kFrac = 1.0f / static_cast<float>((1 << 10) - 1);
      float w = static_cast<float>((raw >> 18) & 0x3FFu) * kFrac;
      w = 1.0f - w * w;
      const auto point = static_cast<float>(raw & 0x3FFFFu);
      float phi = std::floor(std::sqrt(point));
      float theta = 0.0f;
      if (phi > 0.0f) {
        theta = 3.14159265358979323846f / 4.0f * (point - phi * phi) / phi;
        phi = 3.14159265358979323846f / 2.0f / 511.0f * phi;
      }
      const float mag = std::sqrt(std::max(0.0f, 1.0f - w * w));
      const float sp = std::sin(phi);
      const float cp = std::cos(phi);
      const float st = std::sin(theta);
      const float ct = std::cos(theta);
      std::array<float, 4> q{sp * ct * mag, sp * st * mag, cp * mag, w};
      constexpr std::uint32_t kSigns[4] = {0x10000000u, 0x20000000u, 0x40000000u, 0x80000000u};
      for (std::size_t c = 0; c < 4; ++c) {
        if ((raw & kSigns[c]) != 0) q[c] = -q[c];
      }
      return normalize_quat(q);
    }
    case 3: {
      const auto b0 = std::to_integer<std::uint8_t>(bytes[off]);
      const auto b1 = std::to_integer<std::uint8_t>(bytes[off + 1]);
      const auto b2 = std::to_integer<std::uint8_t>(bytes[off + 2]);
      off += 3;
      const float kScale = 1.0f / (63.0f * std::sqrt(2.0f));
      const std::size_t largest = static_cast<std::size_t>(((b0 >> 7) & 1) | ((b1 >> 6) & 2));
      const bool negative = (b2 >> 7) != 0;
      const std::array<float, 3> packed = {
          (static_cast<float>(b0 & 0x7F) - 63.0f) * kScale,
          (static_cast<float>(b1 & 0x7F) - 63.0f) * kScale,
          (static_cast<float>(b2 & 0x7F) - 63.0f) * kScale,
      };
      std::array<float, 4> q{};
      std::size_t next = 0;
      float squares = 0.0f;
      for (std::size_t component = 0; component < 4; ++component) {
        if (component == largest) continue;
        q[component] = packed[next++];
        squares += q[component] * q[component];
      }
      q[largest] = std::sqrt(std::max(0.0f, 1.0f - squares));
      if (negative) q[largest] = -q[largest];
      return normalize_quat(q);
    }
    case 4: {
      const auto b0 = std::to_integer<std::uint8_t>(bytes[off]);
      const auto b1 = std::to_integer<std::uint8_t>(bytes[off + 1]);
      off += 2;
      constexpr float kNibbles = 7.0f;
      const std::array<float, 4> q = {
          static_cast<float>(b0 & 0x0F) - kNibbles,
          static_cast<float>(b0 >> 4) - kNibbles,
          static_cast<float>(b1 & 0x0F) - kNibbles,
          static_cast<float>(b1 >> 4) - kNibbles,
      };
      return normalize_quat({q[0] / kNibbles, q[1] / kNibbles, q[2] / kNibbles, q[3] / kNibbles});
    }
    case 2: {
      std::uint16_t xr = 0;
      std::uint16_t yr = 0;
      std::uint16_t zr = 0;
      std::memcpy(&xr, bytes.data() + off, 2);
      std::memcpy(&yr, bytes.data() + off + 2, 2);
      std::memcpy(&zr, bytes.data() + off + 4, 2);
      off += 6;
      constexpr float kFrac = 0.000043161f;
      constexpr int kMask = (1 << 15) - 1;
      const int shift = ((yr >> 14) & 2) | ((xr >> 15) & 1);
      const bool negative = (zr >> 15) != 0;
      const float v0 = static_cast<float>((static_cast<int>(xr) & kMask) - (kMask >> 1)) * kFrac;
      const float v1 = static_cast<float>((static_cast<int>(yr) & kMask) - (kMask >> 1)) * kFrac;
      const float v2 = static_cast<float>((static_cast<int>(zr) & kMask) - (kMask >> 1)) * kFrac;
      float w = std::sqrt(std::max(0.0f, 1.0f - v0 * v0 - v1 * v1 - v2 * v2));
      if (negative) w = -w;
      const std::array<float, 4> ordered[4] = {
          {w, v0, v1, v2}, {v0, w, v1, v2}, {v0, v1, w, v2}, {v0, v1, v2, w}};
      return normalize_quat(ordered[static_cast<std::size_t>(shift & 3)]);
    }
    default: {
      for (std::size_t i = 0; i < 5; ++i) {
        raw |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[off + i])) << (i * 8);
      }
      off += 5;
      constexpr float kFrac = 0.000345436f;
      const float v0 = static_cast<float>(static_cast<std::int64_t>((raw >> 0) & 0xFFF) - 2049) * kFrac;
      const float v1 = static_cast<float>(static_cast<std::int64_t>((raw >> 12) & 0xFFF) - 2049) * kFrac;
      const float v2 = static_cast<float>(static_cast<std::int64_t>((raw >> 24) & 0xFFF) - 2049) * kFrac;
      float w = std::sqrt(std::max(0.0f, 1.0f - v0 * v0 - v1 * v1 - v2 * v2));
      if (((raw >> 38) & 1) != 0) w = -w;
      const auto shift = static_cast<std::size_t>((raw >> 36) & 3);
      const std::array<float, 4> ordered[4] = {
          {w, v0, v1, v2}, {v0, w, v1, v2}, {v0, v1, w, v2}, {v0, v1, v2, w}};
      return normalize_quat(ordered[shift]);
    }
  }
}

}  // namespace fo4::hkx::detail
