#include "fo4/nif/hknp_mass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace fo4::nif {
namespace {

constexpr int k_jacobi_sweeps = 64;

using DVec3 = std::array<double, 3>;
using DMat3 = std::array<std::array<double, 3>, 3>;

bool is_finite(const HknpVec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

DVec3 widen(const HknpVec3& v) { return {v[0], v[1], v[2]}; }

double dot(const DVec3& a, const DVec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

DMat3 diagonal(double a, double b, double c) {
  return {{{a, 0.0, 0.0}, {0.0, b, 0.0}, {0.0, 0.0, c}}};
}

HknpSolidMass make_solid(double volume, const DVec3& centre, const DMat3& inertia) {
  HknpSolidMass solid;
  solid.volume = static_cast<float>(volume);
  for (std::size_t i = 0; i < 3; ++i) {
    solid.centre[i] = static_cast<float>(centre[i]);
    for (std::size_t j = 0; j < 3; ++j) solid.inertia[i][j] = static_cast<float>(inertia[i][j]);
  }
  return solid;
}

void require_radius(float radius) {
  if (!std::isfinite(radius) || !(radius > 0.0f)) {
    throw std::invalid_argument("a solid needs a finite positive radius");
  }
}

void require_density(float density) {
  if (!std::isfinite(density) || !(density > 0.0f)) {
    throw std::invalid_argument("a body needs a finite positive density");
  }
}

void require_finite(const HknpVec3& point) {
  if (!is_finite(point)) throw std::invalid_argument("a solid needs a finite centre or end point");
}

}  // namespace

HknpSolidMass sphere_solid(const HknpVec3& centre, float radius) {
  require_finite(centre);
  require_radius(radius);
  const double r = radius;
  const double volume = 4.0 / 3.0 * std::numbers::pi * r * r * r;
  const double moment = 2.0 / 5.0 * volume * r * r;
  return make_solid(volume, widen(centre), diagonal(moment, moment, moment));
}

HknpSolidMass capsule_solid(const HknpVec3& point_a, const HknpVec3& point_b, float radius) {
  require_finite(point_a);
  require_finite(point_b);
  require_radius(radius);
  const DVec3 a = widen(point_a);
  const DVec3 b = widen(point_b);
  const DVec3 segment{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const double length = std::sqrt(dot(segment, segment));
  if (!(length > 0.0)) return sphere_solid(point_a, radius);

  const double r = radius;
  const double r2 = r * r;
  const double cylinder = std::numbers::pi * r2 * length;
  const double ball = 4.0 / 3.0 * std::numbers::pi * r2 * r;
  const double volume = cylinder + ball;
  const double dome_offset = length / 2.0 + 3.0 * r / 8.0;
  const double axial = std::numbers::pi * r2 * r2 * length / 2.0 + 2.0 / 5.0 * ball * r2;
  const double transverse = cylinder * (r2 / 4.0 + length * length / 12.0) +
                            ball * (2.0 / 5.0 * r2 - 9.0 / 64.0 * r2 + dome_offset * dome_offset);
  const DVec3 unit{segment[0] / length, segment[1] / length, segment[2] / length};
  DMat3 inertia{};
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      inertia[i][j] = (i == j ? transverse : 0.0) + (axial - transverse) * unit[i] * unit[j];
    }
  }
  const DVec3 centre{(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, (a[2] + b[2]) / 2.0};
  return make_solid(volume, centre, inertia);
}

HknpPrincipalAxes principal_axes(const HknpMat3& inertia) {
  for (const auto& row : inertia) {
    if (!is_finite(row)) throw std::invalid_argument("an inertia tensor must be finite");
  }
  DMat3 a{};
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      a[i][j] = 0.5 * (static_cast<double>(inertia[i][j]) + static_cast<double>(inertia[j][i]));
    }
  }
  DMat3 v = diagonal(1.0, 1.0, 1.0);
  for (int sweep = 0; sweep < k_jacobi_sweeps; ++sweep) {
    const double scale = std::fabs(a[0][0]) + std::fabs(a[1][1]) + std::fabs(a[2][2]);
    const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
    if (off <= 1.0e-30 * scale * scale) break;
    for (std::size_t p = 0; p < 3; ++p) {
      for (std::size_t q = p + 1; q < 3; ++q) {
        if (a[p][q] == 0.0) continue;
        const double theta = 0.5 * (a[q][q] - a[p][p]) / a[p][q];
        const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0);
        const double s = t * c;
        for (std::size_t k = 0; k < 3; ++k) {
          const double akp = a[k][p];
          const double akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = c * akq + s * akp;
        }
        for (std::size_t k = 0; k < 3; ++k) {
          const double apk = a[p][k];
          const double aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = c * aqk + s * apk;
        }
        for (std::size_t k = 0; k < 3; ++k) {
          const double vkp = v[k][p];
          const double vkq = v[k][q];
          v[k][p] = c * vkp - s * vkq;
          v[k][q] = c * vkq + s * vkp;
        }
      }
    }
  }
  std::array<std::size_t, 3> order{0, 1, 2};
  std::sort(order.begin(), order.end(), [&a](std::size_t x, std::size_t y) { return a[x][x] < a[y][y]; });
  HknpPrincipalAxes result;
  for (std::size_t k = 0; k < 3; ++k) {
    result.moments[k] = static_cast<float>(a[order[k]][order[k]]);
    for (std::size_t i = 0; i < 3; ++i) result.axes[k][i] = static_cast<float>(v[i][order[k]]);
  }
  return result;
}

HknpMassProperties compose_mass_properties(const HknpSolidMass& solid, float density,
                                           std::uint64_t packed_orientation) {
  require_density(density);
  const HknpPrincipalAxes axes = principal_axes(solid.inertia);
  HknpMassProperties mass;
  mass.center = solid.centre;
  mass.volume = solid.volume;
  mass.mass = solid.volume * density;
  for (std::size_t k = 0; k < 3; ++k) {
    mass.inertia_raw[k] = axes.moments[k] * density * k_stored_inertia_factor;
  }
  mass.packed_orientation = packed_orientation;
  return mass;
}

HknpDynInertia compose_dyn_inertia(const HknpSolidMass& solid, float density, const HknpVec4& orientation) {
  require_density(density);
  const HknpPrincipalAxes axes = principal_axes(solid.inertia);
  HknpDynInertia dyn;
  dyn.density = density;
  dyn.mass = solid.volume * density;
  dyn.inverse_mass = 1.0f / dyn.mass;
  for (std::size_t k = 0; k < 3; ++k) {
    dyn.inverse_inertia[k] = 1.0f / (density * axes.moments[k] * k_stored_inertia_factor);
  }
  dyn.center = solid.centre;
  dyn.orientation = orientation;
  return dyn;
}

}  // namespace fo4::nif
