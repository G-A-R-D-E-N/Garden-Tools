#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Constraint: units per metre must equal k_hknp_game_units_per_metre (69.99125), so a plain value is in engine metres.
inline constexpr float k_plain_units_per_metre = 69.99125f;
inline constexpr float k_plain_max_rate = 240.0f;
inline constexpr float k_plain_up_tolerance = 1e-3f;

// Constraint: engine axes are (x, z, -y) for positions and rotation vectors, (x, z, y) for scale, positions in metres.
HkxQsTransform to_engine_axes(const HkxQsTransform& transform);

struct HkxPlainTrack {
  std::int32_t bone{0};
  std::vector<std::array<float, 3>> translations;
  std::vector<std::array<float, 4>> rotations;  // x, y, z, w
  std::vector<std::array<float, 3>> scales;
};

struct HkxPlainEvent {
  float time{0.0f};
  std::string name;
};

// Invariant: a delta is relative to the previous key's heading, so a sequence of deltas applies to an actor in order.
struct HkxPlainRootMotion {
  HkxMotionKind kind{HkxMotionKind::None};
  std::vector<std::array<float, 4>> deltas;  // x, y, z, yaw in radians about +Y; key 0 is zero
  std::array<float, 3> net_translation{};
  float net_yaw{0.0f};
  float mean_planar_speed{0.0f};
  float mean_turn_rate{0.0f};
};

struct HkxPlainClip {
  float length{0.0f};
  float rate{0.0f};
  bool loop{false};
  std::size_t key_count{0};
  std::vector<HkxPlainTrack> tracks;
  std::vector<HkxPlainEvent> events;
  HkxPlainRootMotion root_motion;
};

// Requires a decoded skeleton in the file's axes; every bone comes back in engine axes.
std::vector<HkxQsTransform> plain_rest_pose(const HkxSkeleton& skeleton);

// Requires rate 0 (the clip's own frame rate) or a finite rate up to k_plain_max_rate; only bones with a track get keys.
HkxStatus sample_plain_clip(const HkxSkeleton& skeleton, const HkxAnimation& animation,
                            const HkxAnimationBinding& binding, const HkxExtractedMotion* motion, float rate, bool loop,
                            HkxPlainClip& out);

}  // namespace fo4::hkx
