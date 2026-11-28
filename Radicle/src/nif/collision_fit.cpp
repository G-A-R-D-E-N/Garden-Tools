#include "fo4/nif/collision_fit.hpp"

#include "fo4/nif/hknp_mass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

namespace fo4::nif {
namespace {

using DVec3 = std::array<double, 3>;

constexpr int k_ritter_passes = 8;

bool all_finite(std::span<const HknpVec3> points) {
  if (points.empty()) return false;
  for (const auto& point : points) {
    for (const float value : point) {
      if (!std::isfinite(value)) return false;
    }
  }
  return true;
}

DVec3 widen(const HknpVec3& v) { return {v[0], v[1], v[2]}; }

HknpVec3 narrow(const DVec3& v) { return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])}; }

DVec3 minus(const DVec3& a, const DVec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

DVec3 plus_scaled(const DVec3& a, const DVec3& b, double factor) {
  return {a[0] + factor * b[0], a[1] + factor * b[1], a[2] + factor * b[2]};
}

double dot(const DVec3& a, const DVec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

double distance(const DVec3& a, const DVec3& b) { return std::sqrt(dot(minus(a, b), minus(a, b))); }

double distance_to_segment(const DVec3& p, const DVec3& a, const DVec3& b) {
  const DVec3 ab = minus(b, a);
  const double length_sq = dot(ab, ab);
  const double along = length_sq > 0.0 ? std::clamp(dot(minus(p, a), ab) / length_sq, 0.0, 1.0) : 0.0;
  return distance(p, plus_scaled(a, ab, along));
}

struct Frame {
  DVec3 mean{};
  // Invariant: axes ascend by variance, so axes[2] has the greatest spread.
  std::array<DVec3, 3> axes{};
};

Frame principal_frame(std::span<const HknpVec3> points) {
  Frame frame;
  for (const auto& point : points) frame.mean = plus_scaled(frame.mean, widen(point), 1.0);
  const double count = static_cast<double>(points.size());
  for (double& value : frame.mean) value /= count;
  HknpMat3 covariance{};
  for (const auto& point : points) {
    const DVec3 offset = minus(widen(point), frame.mean);
    for (std::size_t i = 0; i < 3; ++i) {
      for (std::size_t j = 0; j < 3; ++j) {
        covariance[i][j] += static_cast<float>(offset[i] * offset[j] / count);
      }
    }
  }
  const HknpPrincipalAxes axes = principal_axes(covariance);
  for (std::size_t k = 0; k < 3; ++k) frame.axes[k] = widen(axes.axes[k]);
  return frame;
}

}  // namespace

std::optional<FittedBox> fit_oriented_box(std::span<const HknpVec3> points) {
  if (!all_finite(points)) return std::nullopt;
  const Frame frame = principal_frame(points);
  DVec3 low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
  DVec3 high{-low[0], -low[1], -low[2]};
  for (const auto& point : points) {
    const DVec3 offset = minus(widen(point), frame.mean);
    for (std::size_t k = 0; k < 3; ++k) {
      const double t = dot(offset, frame.axes[k]);
      low[k] = std::min(low[k], t);
      high[k] = std::max(high[k], t);
    }
  }
  DVec3 centre = frame.mean;
  DVec3 half{};
  FittedBox box;
  for (std::size_t k = 0; k < 3; ++k) {
    centre = plus_scaled(centre, frame.axes[k], (low[k] + high[k]) / 2.0);
    half[k] = (high[k] - low[k]) / 2.0;
    box.axes[k] = narrow(frame.axes[k]);
  }
  box.centre = narrow(centre);
  box.half_extents = narrow(half);
  return box;
}

std::optional<FittedSphere> fit_bounding_sphere(std::span<const HknpVec3> points) {
  if (!all_finite(points)) return std::nullopt;
  std::array<std::size_t, 3> low_index{0, 0, 0};
  std::array<std::size_t, 3> high_index{0, 0, 0};
  for (std::size_t i = 1; i < points.size(); ++i) {
    for (std::size_t k = 0; k < 3; ++k) {
      if (points[i][k] < points[low_index[k]][k]) low_index[k] = i;
      if (points[i][k] > points[high_index[k]][k]) high_index[k] = i;
    }
  }
  std::size_t first = low_index[0];
  std::size_t second = high_index[0];
  double widest = -1.0;
  for (std::size_t k = 0; k < 3; ++k) {
    const double span_sq = dot(minus(widen(points[high_index[k]]), widen(points[low_index[k]])),
                               minus(widen(points[high_index[k]]), widen(points[low_index[k]])));
    if (span_sq > widest) {
      widest = span_sq;
      first = low_index[k];
      second = high_index[k];
    }
  }
  DVec3 centre = plus_scaled(widen(points[first]), minus(widen(points[second]), widen(points[first])), 0.5);
  double radius = distance(widen(points[first]), centre);
  for (int pass = 0; pass < k_ritter_passes; ++pass) {
    bool grew = false;
    for (const auto& point : points) {
      const DVec3 p = widen(point);
      const double d = distance(p, centre);
      if (d <= radius) continue;
      const double grown = (radius + d) / 2.0;
      centre = plus_scaled(centre, minus(p, centre), (d - grown) / d);
      radius = grown;
      grew = true;
    }
    if (!grew) break;
  }
  double enclosing = 0.0;
  for (const auto& point : points) enclosing = std::max(enclosing, distance(widen(point), centre));
  return FittedSphere{narrow(centre), static_cast<float>(enclosing)};
}

std::optional<FittedCapsule> fit_capsule(std::span<const HknpVec3> points) {
  if (!all_finite(points)) return std::nullopt;
  const Frame frame = principal_frame(points);
  const DVec3& axis = frame.axes[2];
  double low = std::numeric_limits<double>::max();
  double high = -low;
  for (const auto& point : points) {
    const double t = dot(minus(widen(point), frame.mean), axis);
    low = std::min(low, t);
    high = std::max(high, t);
  }
  double reach = 0.0;
  for (const auto& point : points) {
    const DVec3 offset = minus(widen(point), frame.mean);
    const DVec3 across = plus_scaled(offset, axis, -dot(offset, axis));
    reach = std::max(reach, distance(across, DVec3{0.0, 0.0, 0.0}));
  }
  DVec3 a = frame.mean;
  DVec3 b = frame.mean;
  if (high - low > 2.0 * reach) {
    a = plus_scaled(frame.mean, axis, low + reach);
    b = plus_scaled(frame.mean, axis, high - reach);
  } else {
    a = plus_scaled(frame.mean, axis, (low + high) / 2.0);
    b = a;
  }
  double enclosing = 0.0;
  for (const auto& point : points) enclosing = std::max(enclosing, distance_to_segment(widen(point), a, b));
  return FittedCapsule{narrow(a), narrow(b), static_cast<float>(enclosing)};
}

}  // namespace fo4::nif
