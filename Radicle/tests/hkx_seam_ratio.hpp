#pragma once

#include "fo4/hkx/animation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fo4::test::hkx {

// Requires unit quaternions: |dot| folds the sign and the angle is twice the half-angle.
inline float rotation_angle(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  const float dot = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
  return 2.0f * std::acos(std::min(1.0f, dot));
}

// Requires the same scale for both parts: the step is the translation distance plus the turn in radians.
inline float pose_step(const fo4::hkx::HkxAnimationTrack& track, std::size_t frame) {
  const auto& from = track.translations[frame];
  const auto& to = track.translations[frame + 1];
  const float dx = to[0] - from[0];
  const float dy = to[1] - from[1];
  const float dz = to[2] - from[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz) +
         rotation_angle(track.rotations[frame], track.rotations[frame + 1]);
}

struct SeamRatio {
  std::size_t moving{0};
  std::size_t within_band{0};
  float median{0.0f};
};

// Requires the seam step and two neighbours either side; the ratio must collapse under a packed stride.
inline SeamRatio seam_ratio(const fo4::hkx::HkxAnimation& animation, float min_step) {
  SeamRatio result;
  if (animation.blocks.max_frames_per_block < 4) return result;
  const std::size_t seam = animation.blocks.max_frames_per_block - 1;
  if (animation.frame_count < seam + 4) return result;

  std::vector<float> ratios;
  for (const fo4::hkx::HkxAnimationTrack& track : animation.tracks) {
    if (track.translations.size() < seam + 4 || track.rotations.size() < seam + 4) continue;
    const float neighbours = 0.25f * (pose_step(track, seam - 3) + pose_step(track, seam - 2) +
                                      pose_step(track, seam + 1) + pose_step(track, seam + 2));
    if (!(neighbours > min_step)) continue;
    ratios.push_back(pose_step(track, seam) / neighbours);
  }

  result.moving = ratios.size();
  if (ratios.empty()) return result;
  for (const float ratio : ratios) {
    if (ratio >= 0.25f && ratio <= 4.0f) ++result.within_band;
  }
  std::sort(ratios.begin(), ratios.end());
  const std::size_t middle = ratios.size() / 2;
  result.median =
      ratios.size() % 2 == 0 ? 0.5f * (ratios[middle - 1] + ratios[middle]) : ratios[middle];
  return result;
}

}  // namespace fo4::test::hkx
