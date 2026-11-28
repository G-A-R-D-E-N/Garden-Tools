#pragma once

#include "fo4/hkx/animation.hpp"

#include <cstddef>
#include <vector>

namespace fo4::hkx {

inline constexpr float kMotionMaxMarkSeconds = 3600.0f;

// Constraint: one pose per whole second from 0 to min(duration, 3600); none when the motion is not previewable.
std::vector<HkxMotionPose> motion_second_marks(const HkxExtractedMotion& motion);

// Constraint: at most max_points poses spread evenly over the samples, including the first and the last when max_points is at least 2 (only the first for 1); none when the motion is not previewable or max_points is 0.
std::vector<HkxMotionPose> motion_path_points(const HkxExtractedMotion& motion, std::size_t max_points);

}  // namespace fo4::hkx
