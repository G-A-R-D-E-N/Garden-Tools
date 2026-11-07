#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_mass.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {

using fo4::nif::HknpMat3;
using fo4::nif::HknpSolidMass;
using fo4::nif::HknpVec3;

constexpr double k_relative = 1.0e-4;

double sphere_volume(double radius) { return 4.0 / 3.0 * std::numbers::pi * radius * radius * radius; }

}  // namespace

// Detects: a sphere whose volume or diagonal inertia drifts from 4/3 pi r^3 and 2/5 V r^2 about its centre.
TEST(HknpMass, SphereMatchesClosedForm) {
  const HknpSolidMass sphere = fo4::nif::sphere_solid({1.0f, -2.0f, 3.0f}, 0.75f);
  const double volume = sphere_volume(0.75);
  const double moment = 2.0 / 5.0 * volume * 0.75 * 0.75;
  EXPECT_NEAR(sphere.volume, volume, k_relative * volume);
  EXPECT_NEAR(sphere.centre[0], 1.0f, 1e-6f);
  EXPECT_NEAR(sphere.centre[1], -2.0f, 1e-6f);
  EXPECT_NEAR(sphere.centre[2], 3.0f, 1e-6f);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(sphere.inertia[i][i], moment, k_relative * moment);
    for (std::size_t j = 0; j < 3; ++j) {
      if (i != j) {
        EXPECT_EQ(sphere.inertia[i][j], 0.0f);
      }
    }
  }
}

// Detects: a box whose stored inertia is not 1.5 times the physical (V (b^2 + c^2) / 3) per axis, or whose axis pairing is swapped.
TEST(HknpMass, BoxStoredInertiaIsOneAndAHalfPhysical) {
  const HknpVec3 half{0.5f, 1.0f, 2.0f};
  const fo4::nif::HknpShape box = fo4::nif::make_hknp_box({0.0f, 0.0f, 0.0f}, half, 0U);
  const double a = 0.5;
  const double b = 1.0;
  const double c = 2.0;
  const double volume = 8.0 * a * b * c;
  const std::array<double, 3> expected{volume * (b * b + c * c) / 2.0, volume * (a * a + c * c) / 2.0,
                                       volume * (a * a + b * b) / 2.0};
  ASSERT_TRUE(box.has_mass_properties);
  EXPECT_NEAR(box.mass_properties.volume, volume, k_relative * volume);
  EXPECT_NEAR(box.mass_properties.mass, volume, k_relative * volume);
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(box.mass_properties.inertia_raw[k], expected[k], k_relative * expected[k]);
  }
}

// Detects: a capsule whose transverse or axial inertia disagrees with an independent slice integral over the sphere-swept solid.
TEST(HknpMass, CapsuleInertiaMatchesSliceIntegration) {
  const double radius = 0.5;
  const double half_length = 1.0;
  const double top = half_length + radius;
  const int steps = 400000;
  const double h = 2.0 * top / steps;
  double volume = 0.0;
  double axial = 0.0;
  double transverse = 0.0;
  for (int i = 0; i < steps; ++i) {
    const double z = -top + (i + 0.5) * h;
    const double beyond = std::max(0.0, std::fabs(z) - half_length);
    const double rho2 = radius * radius - beyond * beyond;
    const double area = std::numbers::pi * rho2;
    volume += area * h;
    axial += area * rho2 / 2.0 * h;
    transverse += area * (z * z + rho2 / 4.0) * h;
  }
  const HknpSolidMass capsule = fo4::nif::capsule_solid({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, 0.5f);
  EXPECT_NEAR(capsule.volume, volume, k_relative * volume);
  EXPECT_NEAR(capsule.inertia[0][0], transverse, k_relative * transverse);
  EXPECT_NEAR(capsule.inertia[1][1], transverse, k_relative * transverse);
  EXPECT_NEAR(capsule.inertia[2][2], axial, k_relative * axial);
}

// Detects: a capsule whose formula stops reducing to a sphere when its two ends coincide.
TEST(HknpMass, CapsuleWithCoincidentEndsIsASphere) {
  const HknpSolidMass capsule = fo4::nif::capsule_solid({2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, 0.3f);
  const HknpSolidMass sphere = fo4::nif::sphere_solid({2.0f, 0.0f, 0.0f}, 0.3f);
  EXPECT_NEAR(capsule.volume, sphere.volume, k_relative * sphere.volume);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(capsule.inertia[i][i], sphere.inertia[i][i], k_relative * sphere.inertia[i][i]);
  }
}

// Detects: a capsule oriented along a diagonal whose inertia is not the rotated axis-aligned one, because the axis term was dropped or misaligned.
TEST(HknpMass, CapsuleMomentsAreInvariantUnderRotation) {
  const HknpSolidMass along_z = fo4::nif::capsule_solid({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, 0.5f);
  const HknpSolidMass diagonal =
      fo4::nif::capsule_solid({-0.57735027f, -0.57735027f, -0.57735027f}, {0.57735027f, 0.57735027f, 0.57735027f}, 0.5f);
  const fo4::nif::HknpPrincipalAxes a = fo4::nif::principal_axes(along_z.inertia);
  const fo4::nif::HknpPrincipalAxes b = fo4::nif::principal_axes(diagonal.inertia);
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(b.moments[k], a.moments[k], 1e-3f * a.moments[k]);
  }
}

// Detects: a symmetric tensor whose Jacobi rotation sign or eigenvector order is wrong, so the axes no longer rebuild the input.
TEST(HknpMass, PrincipalAxesRebuildTheInputTensor) {
  const HknpMat3 input{{{2.0f, 0.5f, 0.1f}, {0.5f, 3.0f, 0.2f}, {0.1f, 0.2f, 4.0f}}};
  const fo4::nif::HknpPrincipalAxes axes = fo4::nif::principal_axes(input);
  EXPECT_LE(axes.moments[0], axes.moments[1]);
  EXPECT_LE(axes.moments[1], axes.moments[2]);
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      float rebuilt = 0.0f;
      for (std::size_t k = 0; k < 3; ++k) rebuilt += axes.moments[k] * axes.axes[k][i] * axes.axes[k][j];
      EXPECT_NEAR(rebuilt, input[i][j], 1e-4f);
    }
  }
  for (std::size_t k = 0; k < 3; ++k) {
    float length = 0.0f;
    for (std::size_t i = 0; i < 3; ++i) length += axes.axes[k][i] * axes.axes[k][i];
    EXPECT_NEAR(length, 1.0f, 1e-5f);
  }
}

// Detects: composed mass properties that drop the 1.5 stored-inertia factor, ignore the density, or lose the caller's packed orientation.
TEST(HknpMass, ComposeAppliesDensityAndTheStoredInertiaFactor) {
  const HknpSolidMass sphere = fo4::nif::sphere_solid({0.0f, 0.0f, 0.0f}, 1.0f);
  const fo4::nif::HknpMassProperties mass = fo4::nif::compose_mass_properties(sphere, 2.0f, 0x123456789ABCDEF0ULL);
  const double volume = sphere_volume(1.0);
  EXPECT_NEAR(mass.volume, volume, k_relative * volume);
  EXPECT_NEAR(mass.mass, 2.0 * volume, k_relative * volume * 2.0);
  const double expected = 1.5 * 2.0 * 2.0 / 5.0 * volume;
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(mass.inertia_raw[k], expected, k_relative * expected);
  }
  EXPECT_EQ(mass.packed_orientation, 0x123456789ABCDEF0ULL);
}

// Detects: a composed dynamic body whose inverse mass or inverse inertia is not the reciprocal of its density times mass or stored inertia.
TEST(HknpMass, DynInertiaInvertsDensityAndStoredInertia) {
  const HknpSolidMass sphere = fo4::nif::sphere_solid({0.0f, 0.0f, 0.0f}, 0.5f);
  const fo4::nif::HknpVec4 identity{{0.0f, 0.0f, 0.0f, 1.0f}};
  const fo4::nif::HknpDynInertia dyn = fo4::nif::compose_dyn_inertia(sphere, 4.0f, identity);
  const double volume = sphere_volume(0.5);
  EXPECT_EQ(dyn.density, 4.0f);
  EXPECT_NEAR(dyn.mass, 4.0 * volume, k_relative * 4.0 * volume);
  EXPECT_NEAR(dyn.inverse_mass * dyn.mass, 1.0f, 1.0e-5f);
  const double stored = 1.5 * 2.0 / 5.0 * volume * 0.25;
  for (std::size_t k = 0; k < 3; ++k) {
    EXPECT_NEAR(dyn.inverse_inertia[k], 1.0 / (4.0 * stored), k_relative / (4.0 * stored));
  }
  EXPECT_EQ(dyn.orientation, identity);
}

// Detects: solid constructors or diagonalisation accepting NaN, zero radius, or zero density and returning garbage instead of refusing.
TEST(HknpMass, RefusesNonFiniteAndNonPositiveInput) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::sphere_solid({0.0f, 0.0f, 0.0f}, 0.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::sphere_solid({nan, 0.0f, 0.0f}, 1.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::capsule_solid({0.0f, 0.0f, 0.0f}, {nan, 0.0f, 0.0f}, 1.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::capsule_solid({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, -1.0f), std::invalid_argument);
  const HknpMat3 bad{{{nan, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
  EXPECT_THROW(fo4::nif::principal_axes(bad), std::invalid_argument);
  const HknpSolidMass sphere = fo4::nif::sphere_solid({0.0f, 0.0f, 0.0f}, 1.0f);
  EXPECT_THROW(fo4::nif::compose_mass_properties(sphere, 0.0f, 0), std::invalid_argument);
}
