#include "fo4/math/interpolation.hpp"

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fo4::math {
namespace {

float require_finite(float value, const char* role) {
  if (!std::isfinite(value)) {
    throw InterpolationError(std::string{"pchip "} + role + " is not finite");
  }
  return value;
}

float endpoint_tangent(float h0, float h1, float d0, float d1) {
  const float tangent = ((2.0f * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
  if (tangent * d0 <= 0.0f) return 0.0f;
  if (d0 * d1 < 0.0f && std::fabs(tangent) > 3.0f * std::fabs(d0)) return 3.0f * d0;
  return tangent;
}

float hermite(float y0, float y1, float h, float m0, float m1, float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float h10 = t3 - 2.0f * t2 + t;
  const float h01 = -2.0f * t3 + 3.0f * t2;
  const float h11 = t3 - t2;
  return h00 * y0 + h10 * h * m0 + h01 * y1 + h11 * h * m1;
}

}  // namespace

float pchip(std::span<const float> xs, std::span<const float> ys, float x) {
  if (xs.size() != ys.size()) {
    throw InterpolationError("pchip needs one y value per x value");
  }
  if (xs.size() < 2) throw InterpolationError("pchip needs at least two samples");
  require_finite(x, "query");
  for (std::size_t i = 0; i < xs.size(); ++i) {
    require_finite(xs[i], "x sample");
    require_finite(ys[i], "y sample");
    if (i > 0 && !(xs[i] > xs[i - 1])) {
      throw InterpolationError("pchip samples must be in strictly increasing x order");
    }
  }
  if (x <= xs.front()) return ys.front();
  if (x >= xs.back()) return ys.back();
  const std::size_t count = xs.size();
  if (count == 2) {
    const float t = (x - xs[0]) / (xs[1] - xs[0]);
    return hermite(ys[0], ys[1], xs[1] - xs[0], (ys[1] - ys[0]) / (xs[1] - xs[0]),
                   (ys[1] - ys[0]) / (xs[1] - xs[0]), t);
  }
  std::vector<float> slopes(count - 1);
  for (std::size_t i = 0; i + 1 < count; ++i) {
    slopes[i] = (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]);
  }
  const auto tangent = [&](std::size_t index) {
    if (index == 0) {
      return endpoint_tangent(xs[1] - xs[0], xs[2] - xs[1], slopes[0], slopes[1]);
    }
    if (index + 1 == count) {
      const auto back = count - 2;
      return endpoint_tangent(xs[count - 1] - xs[count - 2], xs[count - 2] - xs[count - 3],
                              slopes[back], slopes[back - 1]);
    }
    const float previous = slopes[index - 1];
    const float next = slopes[index];
    if (previous * next <= 0.0f) return 0.0f;
    const float previous_h = xs[index] - xs[index - 1];
    const float next_h = xs[index + 1] - xs[index];
    const float w1 = 2.0f * next_h + previous_h;
    const float w2 = next_h + 2.0f * previous_h;
    return (w1 + w2) / (w1 / previous + w2 / next);
  };
  std::size_t interval = 0;
  while (interval + 2 < count && x > xs[interval + 1]) ++interval;
  const float h = xs[interval + 1] - xs[interval];
  const float m0 = tangent(interval);
  const float m1 = tangent(interval + 1);
  return hermite(ys[interval], ys[interval + 1], h, m0, m1, (x - xs[interval]) / h);
}

}  // namespace fo4::math
