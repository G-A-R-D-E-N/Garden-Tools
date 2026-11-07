#pragma once

#include <array>
#include <cmath>

namespace fo4::hkx::detail {

// Invariant: a stored rotation is a unit quaternion, so a non-unit input is normalised and a zero one becomes identity.
inline std::array<float, 4> normalised_quaternion(const std::array<float, 4>& rotation) {
  const double length = std::sqrt(static_cast<double>(rotation[0]) * rotation[0] +
                                  static_cast<double>(rotation[1]) * rotation[1] +
                                  static_cast<double>(rotation[2]) * rotation[2] +
                                  static_cast<double>(rotation[3]) * rotation[3]);
  if (!(length > 0.0)) return {0.0f, 0.0f, 0.0f, 1.0f};
  const auto scale = static_cast<float>(1.0 / length);
  return {rotation[0] * scale, rotation[1] * scale, rotation[2] * scale, rotation[3] * scale};
}

}  // namespace fo4::hkx::detail
