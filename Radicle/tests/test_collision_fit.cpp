#include "fo4/nif/collision_fit.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace {

using fo4::nif::HknpVec3;

constexpr float k_slack = 1.0e-4f;

double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<double, 3> diff(const HknpVec3& a, const HknpVec3& b) {
  return {static_cast<double>(a[0]) - b[0], static_cast<double>(a[1]) - b[1], static_cast<double>(a[2]) - b[2]};
}

double distance_to_segment(const HknpVec3& p, const HknpVec3& a, const HknpVec3& b) {
  const auto ab = diff(b, a);
  const auto ap = diff(p, a);
  const double length_sq = dot3(ab, ab);
  const double along = length_sq > 0.0 ? std::clamp(dot3(ap, ab) / length_sq, 0.0, 1.0) : 0.0;
  const std::array<double, 3> closest{a[0] + along * ab[0], a[1] + along * ab[1], a[2] + along * ab[2]};
  const auto offset = diff(p, {static_cast<float>(closest[0]), static_cast<float>(closest[1]),
                               static_cast<float>(closest[2])});
  return std::sqrt(dot3(offset, offset));
}

// Constraint: a fixed seed keeps the property inputs reproducible.
std::vector<HknpVec3> scattered_points(std::size_t count) {
  std::vector<HknpVec3> points;
  std::uint32_t state = 12345U;
  const auto next = [&state] {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(state >> 8U) / static_cast<float>(1U << 24U);
  };
  for (std::size_t i = 0; i < count; ++i) {
    points.push_back({-2.0f + 4.0f * next(), -1.0f + 2.0f * next(), -3.0f + 6.0f * next()});
  }
  return points;
}

bool all_finite(const HknpVec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

}  // namespace

// Detects: a PCA box fit that mixes or misorders the box axes, so the fitted half extents stop matching the box's (1, 2, 3).
TEST(CollisionFit, OrientedBoxRecoversARotatedBox) {
  const double angle = 0.5235987755982988;
  const std::array<double, 3> e1{std::cos(angle), std::sin(angle), 0.0};
  const std::array<double, 3> e2{-std::sin(angle), std::cos(angle), 0.0};
  const std::array<double, 3> e3{0.0, 0.0, 1.0};
  const std::array<double, 3> half{1.0, 2.0, 3.0};
  const HknpVec3 centre{5.0f, -2.0f, 1.0f};
  std::vector<HknpVec3> points;
  for (const double x : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
    for (const double y : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
      for (const double z : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
        points.push_back({static_cast<float>(centre[0] + half[0] * x * e1[0] + half[1] * y * e2[0] + half[2] * z * e3[0]),
                          static_cast<float>(centre[1] + half[0] * x * e1[1] + half[1] * y * e2[1] + half[2] * z * e3[1]),
                          static_cast<float>(centre[2] + half[0] * x * e1[2] + half[1] * y * e2[2] + half[2] * z * e3[2])});
      }
    }
  }
  const auto box = fo4::nif::fit_oriented_box(points);
  ASSERT_TRUE(box.has_value());
  std::array<float, 3> extents{box->half_extents[0], box->half_extents[1], box->half_extents[2]};
  std::sort(extents.begin(), extents.end());
  EXPECT_NEAR(extents[0], 1.0f, k_slack);
  EXPECT_NEAR(extents[1], 2.0f, k_slack);
  EXPECT_NEAR(extents[2], 3.0f, k_slack);
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(box->centre[k], centre[k], k_slack);
  }
}

// Detects: a bounding sphere that fails to enclose a sample point, or grows far past the 2.0 radius the samples span.
TEST(CollisionFit, BoundingSphereEnclosesSamplesOnAShellTightly) {
  std::vector<HknpVec3> points;
  const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
  const std::size_t count = 200;
  for (std::size_t i = 0; i < count; ++i) {
    const double y = 1.0 - 2.0 * (static_cast<double>(i) + 0.5) / static_cast<double>(count);
    const double ring = std::sqrt(1.0 - y * y);
    const double theta = golden * static_cast<double>(i);
    points.push_back({static_cast<float>(1.0 + 2.0 * ring * std::cos(theta)), static_cast<float>(-1.0 + 2.0 * y),
                      static_cast<float>(0.5 + 2.0 * ring * std::sin(theta))});
  }
  const auto sphere = fo4::nif::fit_bounding_sphere(points);
  ASSERT_TRUE(sphere.has_value());
  EXPECT_GE(sphere->radius, 1.98f);
  EXPECT_LE(sphere->radius, 2.05f);
  for (const auto& point : points) {
    const auto offset = diff(point, sphere->centre);
    EXPECT_LE(std::sqrt(dot3(offset, offset)), sphere->radius + k_slack);
  }
}

// Detects: a capsule fit that picks the wrong axis, misplaces its end caps, or reports a radius other than the cylinder's 1.
TEST(CollisionFit, CapsuleRecoversEndsAndRadiusOfASweptSegment) {
  std::vector<HknpVec3> points;
  constexpr int azimuths = 12;
  for (int ring = 0; ring <= 4; ++ring) {
    for (int a = 0; a < azimuths; ++a) {
      const double phi = 2.0 * std::numbers::pi * a / azimuths;
      points.push_back({static_cast<float>(std::cos(phi)), static_cast<float>(std::sin(phi)), static_cast<float>(ring)});
    }
  }
  for (const double theta : {std::numbers::pi / 6.0, std::numbers::pi / 3.0, std::numbers::pi / 2.0}) {
    for (int a = 0; a < azimuths; ++a) {
      const double phi = 2.0 * std::numbers::pi * a / azimuths;
      const auto cap = static_cast<float>(std::cos(theta));
      points.push_back({cap * static_cast<float>(std::cos(phi)), cap * static_cast<float>(std::sin(phi)),
                        static_cast<float>(-std::sin(theta))});
      points.push_back({cap * static_cast<float>(std::cos(phi)), cap * static_cast<float>(std::sin(phi)),
                        static_cast<float>(4.0 + std::sin(theta))});
    }
  }
  const auto capsule = fo4::nif::fit_capsule(points);
  ASSERT_TRUE(capsule.has_value());
  const float low_z = std::min(capsule->point_a[2], capsule->point_b[2]);
  const float high_z = std::max(capsule->point_a[2], capsule->point_b[2]);
  EXPECT_NEAR(low_z, 0.0f, 1.0e-3f);
  EXPECT_NEAR(high_z, 4.0f, 1.0e-3f);
  EXPECT_NEAR(capsule->radius, 1.0f, 1.0e-3f);
}

// Detects: a fitted primitive that leaves a point outside it, which would let a collision shape miss part of the mesh.
TEST(CollisionFit, EveryFittedPrimitiveContainsEveryInputPoint) {
  const auto points = scattered_points(400);
  const auto box = fo4::nif::fit_oriented_box(points);
  const auto sphere = fo4::nif::fit_bounding_sphere(points);
  const auto capsule = fo4::nif::fit_capsule(points);
  ASSERT_TRUE(box.has_value());
  ASSERT_TRUE(sphere.has_value());
  ASSERT_TRUE(capsule.has_value());
  for (const auto& point : points) {
    for (std::size_t k = 0; k < 3; ++k) {
      const auto offset = diff(point, box->centre);
      EXPECT_LE(std::fabs(dot3(offset, {box->axes[k][0], box->axes[k][1], box->axes[k][2]})),
                static_cast<double>(box->half_extents[k]) + k_slack);
    }
    const auto offset = diff(point, sphere->centre);
    EXPECT_LE(std::sqrt(dot3(offset, offset)), sphere->radius + k_slack);
    EXPECT_LE(distance_to_segment(point, capsule->point_a, capsule->point_b), capsule->radius + k_slack);
  }
}

// Detects: a fit whose output changes between identical calls, from uninitialised or leaked state.
TEST(CollisionFit, RepeatedFitsOfTheSameInputAreIdentical) {
  const auto points = scattered_points(64);
  const auto first = fo4::nif::fit_bounding_sphere(points);
  const auto second = fo4::nif::fit_bounding_sphere(points);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->radius, second->radius);
  EXPECT_EQ(first->centre, second->centre);
  const auto box_a = fo4::nif::fit_oriented_box(points);
  const auto box_b = fo4::nif::fit_oriented_box(points);
  ASSERT_TRUE(box_a.has_value());
  ASSERT_TRUE(box_b.has_value());
  EXPECT_EQ(box_a->half_extents, box_b->half_extents);
  EXPECT_EQ(box_a->axes, box_b->axes);
}

// Detects: a fit that crashes or returns NaN on an empty or non-finite list instead of refusing it.
TEST(CollisionFit, RefusesEmptyAndNonFiniteInput) {
  const std::vector<HknpVec3> empty;
  EXPECT_FALSE(fo4::nif::fit_oriented_box(empty).has_value());
  EXPECT_FALSE(fo4::nif::fit_bounding_sphere(empty).has_value());
  EXPECT_FALSE(fo4::nif::fit_capsule(empty).has_value());
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<HknpVec3> bad{{0.0f, 0.0f, 0.0f}, {nan, 1.0f, 1.0f}};
  EXPECT_FALSE(fo4::nif::fit_oriented_box(bad).has_value());
  EXPECT_FALSE(fo4::nif::fit_bounding_sphere(bad).has_value());
  EXPECT_FALSE(fo4::nif::fit_capsule(bad).has_value());
}

// Detects: duplicate points that make a fit return a NaN or a non-finite radius instead of a zero-size primitive.
TEST(CollisionFit, DuplicatePointsGiveZeroSizePrimitives) {
  const std::vector<HknpVec3> points(10, HknpVec3{1.0f, 2.0f, 3.0f});
  const auto box = fo4::nif::fit_oriented_box(points);
  const auto sphere = fo4::nif::fit_bounding_sphere(points);
  const auto capsule = fo4::nif::fit_capsule(points);
  ASSERT_TRUE(box.has_value());
  ASSERT_TRUE(sphere.has_value());
  ASSERT_TRUE(capsule.has_value());
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_EQ(box->half_extents[k], 0.0f);
    EXPECT_NEAR(box->centre[k], points[0][k], k_slack);
    EXPECT_NEAR(sphere->centre[k], points[0][k], k_slack);
  }
  EXPECT_EQ(sphere->radius, 0.0f);
  EXPECT_EQ(capsule->radius, 0.0f);
  EXPECT_TRUE(all_finite(capsule->point_a));
  EXPECT_TRUE(all_finite(capsule->point_b));
}

// Detects: a collinear or coplanar input that fits a box with a non-zero extent on an axis the points never span.
TEST(CollisionFit, CollinearAndCoplanarInputsHaveZeroExtentOnTheUnspannedAxes) {
  std::vector<HknpVec3> line;
  for (int i = -2; i <= 2; ++i) line.push_back({static_cast<float>(i), 0.0f, 0.0f});
  const auto line_box = fo4::nif::fit_oriented_box(line);
  ASSERT_TRUE(line_box.has_value());
  std::array<float, 3> extents{line_box->half_extents[0], line_box->half_extents[1], line_box->half_extents[2]};
  std::sort(extents.begin(), extents.end());
  EXPECT_NEAR(extents[0], 0.0f, k_slack);
  EXPECT_NEAR(extents[1], 0.0f, k_slack);
  EXPECT_NEAR(extents[2], 2.0f, k_slack);
  const auto line_capsule = fo4::nif::fit_capsule(line);
  ASSERT_TRUE(line_capsule.has_value());
  EXPECT_NEAR(line_capsule->radius, 0.0f, k_slack);

  std::vector<HknpVec3> plane;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) plane.push_back({static_cast<float>(i), static_cast<float>(j), 7.0f});
  }
  const auto plane_box = fo4::nif::fit_oriented_box(plane);
  ASSERT_TRUE(plane_box.has_value());
  float thinnest = plane_box->half_extents[0];
  for (const float extent : plane_box->half_extents) thinnest = std::min(thinnest, extent);
  EXPECT_NEAR(thinnest, 0.0f, k_slack);
  const auto plane_sphere = fo4::nif::fit_bounding_sphere(plane);
  ASSERT_TRUE(plane_sphere.has_value());
  EXPECT_TRUE(std::isfinite(plane_sphere->radius));
}
