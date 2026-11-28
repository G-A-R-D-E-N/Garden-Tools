#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace fo4::hkx::detail {

// Invariant: a stored rotation is a unit quaternion, so a non-unit input is normalised and a zero one becomes identity.
// Constraint: each component is divided by the double length, so every quotient is at most one in magnitude.
inline std::optional<std::array<float, 4>> normalised_quaternion(const std::array<float, 4>& rotation) {
  const double length = std::sqrt(static_cast<double>(rotation[0]) * rotation[0] +
                                  static_cast<double>(rotation[1]) * rotation[1] +
                                  static_cast<double>(rotation[2]) * rotation[2] +
                                  static_cast<double>(rotation[3]) * rotation[3]);
  if (!(length > 0.0)) return std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
  std::array<float, 4> out{};
  for (std::size_t axis = 0; axis < out.size(); ++axis) {
    out[axis] = static_cast<float>(static_cast<double>(rotation[axis]) / length);
    if (!std::isfinite(out[axis])) return std::nullopt;
  }
  return out;
}

}  // namespace fo4::hkx::detail
