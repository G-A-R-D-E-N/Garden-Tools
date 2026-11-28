#pragma once

#include "fo4/hkx/animation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fo4::hkx {

// Invariant: a report always names the bone and track it measured, so a still clip cannot read as a missing track.
struct HkxRootMotionSample {
  std::size_t frame{0};
  float time{0.0f};
  std::array<float, 3> translation{};
  std::array<float, 3> delta{};
};

struct HkxRootMotion {
  bool has_track{false};
  std::int32_t bone{-1};
  std::size_t track{0};
  std::size_t frame_count{0};
  float duration{0.0f};
  float frame_duration{0.0f};
  std::array<float, 3> total{};
  float distance{0.0f};
  float yaw_degrees{0.0f};
  std::array<float, 3> velocity{};
  bool loops{false};
  float loop_gap{0.0f};
  std::vector<HkxRootMotionSample> samples;
};

// Constraint: a clip that ends within this distance of where it started counts as a loop.
inline constexpr float kRootMotionLoopTolerance = 0.01f;

// Requires a decoded clip: the binding maps one track to `bone`, and that track holds at least one translation.
HkxStatus read_root_motion(const HkxAnimation& animation, const HkxAnimationBinding& binding,
                           std::int32_t bone, std::size_t sample_limit, HkxRootMotion& out);

}  // namespace fo4::hkx
