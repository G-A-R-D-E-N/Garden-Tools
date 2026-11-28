#include "fo4/hkx/motion_preview.hpp"

#include <algorithm>
#include <cmath>

namespace fo4::hkx {

std::vector<HkxMotionPose> motion_second_marks(const HkxExtractedMotion& motion) {
  std::vector<HkxMotionPose> marks;
  if (!motion_previewable(motion)) return marks;
  const float last = std::min(motion.duration, kMotionMaxMarkSeconds);
  for (std::size_t second = 0; static_cast<float>(second) <= last; ++second) {
    marks.push_back(detail::motion_at_time_unchecked(motion, static_cast<float>(second)));
  }
  return marks;
}

std::vector<HkxMotionPose> motion_path_points(const HkxExtractedMotion& motion, std::size_t max_points) {
  std::vector<HkxMotionPose> points;
  if (max_points == 0 || !motion_previewable(motion)) return points;
  const std::size_t count = motion.samples.size();
  const auto pose = [&motion](std::size_t index) {
    const auto& at = motion.samples[index];
    return HkxMotionPose{{at[0], at[1], at[2]}, at[3]};
  };
  if (count <= max_points) {
    for (std::size_t index = 0; index < count; ++index) points.push_back(pose(index));
    return points;
  }
  if (max_points == 1) {
    points.push_back(pose(0));
    return points;
  }
  for (std::size_t slot = 0; slot < max_points; ++slot) {
    points.push_back(pose(slot * (count - 1) / (max_points - 1)));
  }
  return points;
}

}  // namespace fo4::hkx
