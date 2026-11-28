#include "fo4/math/interpolation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

using fo4::math::InterpolationError;
using fo4::math::pchip;

namespace {

float at(std::span<const float> xs, std::span<const float> ys, float x) {
  return pchip(xs, ys, x);
}

}  // namespace

// Detects: a pchip that must reproduce a linear table exactly on every interval and endpoint.
TEST(Pchip, InterpolatesALinearTableExactly) {
  const std::array<float, 4> xs{0.0f, 1.0f, 2.0f, 3.0f};
  const std::array<float, 4> ys{0.0f, 2.0f, 4.0f, 6.0f};
  EXPECT_FLOAT_EQ(at(xs, ys, 0.5f), 1.0f);
  EXPECT_FLOAT_EQ(at(xs, ys, 1.5f), 3.0f);
  EXPECT_FLOAT_EQ(at(xs, ys, 2.5f), 5.0f);
  EXPECT_FLOAT_EQ(at(xs, ys, 1.0f), 2.0f);
  EXPECT_FLOAT_EQ(at(xs, ys, 2.0f), 4.0f);
}

// Detects: a monotone table that must stay monotone and must not overshoot between samples.
TEST(Pchip, KeepsAMonotoneTableMonotone) {
  const std::array<float, 6> xs{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  const std::array<float, 6> ys{0.0f, 0.1f, 0.15f, 1.2f, 1.25f, 5.0f};
  float previous = ys.front();
  for (int step = 0; step <= 100; ++step) {
    const float x = static_cast<float>(step) / 20.0f;
    const float value = at(xs, ys, x);
    EXPECT_GE(value + 1e-4f, previous) << x;
    EXPECT_LE(value, 5.0f + 1e-4f) << x;
    EXPECT_GE(value, -1e-4f) << x;
    previous = value;
  }
  for (std::size_t interval = 0; interval + 1 < xs.size(); ++interval) {
    const float low = std::min(ys[interval], ys[interval + 1]);
    const float high = std::max(ys[interval], ys[interval + 1]);
    for (int step = 1; step < 20; ++step) {
      const float x = xs[interval] + (xs[interval + 1] - xs[interval]) * static_cast<float>(step) / 20.0f;
      const float value = at(xs, ys, x);
      EXPECT_GE(value + 1e-4f, low) << x;
      EXPECT_LE(value - 1e-4f, high) << x;
    }
  }
}

// Detects: a query outside the table that must clamp to the end values instead of extrapolating.
TEST(Pchip, ClampsOutsideTheSampleRange) {
  const std::array<float, 3> xs{-1.0f, 0.0f, 1.0f};
  const std::array<float, 3> ys{10.0f, 20.0f, 40.0f};
  EXPECT_FLOAT_EQ(at(xs, ys, -5.0f), 10.0f);
  EXPECT_FLOAT_EQ(at(xs, ys, 5.0f), 40.0f);
}

// Detects: a two-sample table that must interpolate linearly.
TEST(Pchip, InterpolatesTwoSamplesLinearly) {
  const std::array<float, 2> xs{1.0f, 3.0f};
  const std::array<float, 2> ys{10.0f, 20.0f};
  EXPECT_FLOAT_EQ(at(xs, ys, 2.0f), 15.0f);
  EXPECT_FLOAT_EQ(at(xs, ys, 2.5f), 17.5f);
}

// Detects: malformed tables and non-finite values that must be refused, not silently extrapolated.
TEST(Pchip, RejectsInvalidTables) {
  const std::array<float, 2> two{0.0f, 1.0f};
  const std::array<float, 3> three{0.0f, 1.0f, 2.0f};
  EXPECT_THROW(at(two, three, 0.5f), InterpolationError);
  EXPECT_THROW(at(std::span<const float>(two).first(1), std::span<const float>(two).first(1), 0.5f),
               InterpolationError);
  const std::array<float, 3> duplicate{0.0f, 0.0f, 1.0f};
  EXPECT_THROW(at(duplicate, three, 0.5f), InterpolationError);
  const std::array<float, 3> decreasing{0.0f, 2.0f, 1.0f};
  EXPECT_THROW(at(decreasing, three, 0.5f), InterpolationError);
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array<float, 3> bad_x{0.0f, nan, 2.0f};
  EXPECT_THROW(at(bad_x, three, 0.5f), InterpolationError);
  const std::array<float, 3> bad_y{0.0f, std::numeric_limits<float>::infinity(), 2.0f};
  EXPECT_THROW(at(three, bad_y, 0.5f), InterpolationError);
  EXPECT_THROW(at(three, three, nan), InterpolationError);
}
