#pragma once

#include "fo4/nif/hknp_decode.hpp"

#include <array>
#include <cstdint>

namespace fo4::nif {

using HknpMat3 = std::array<HknpVec3, 3>;

// Constraint: stored inertia is 1.5 times the physical tensor.
inline constexpr float k_stored_inertia_factor = 1.5f;

// Constraint: volume and inertia are for unit density, and inertia is about centre; a body scales them by its density.
struct HknpSolidMass {
  float volume{0.0f};
  HknpVec3 centre{};
  HknpMat3 inertia{};
};

struct HknpPrincipalAxes {
  // Invariant: moments ascend, and axes[k] is the unit eigenvector of moments[k].
  HknpVec3 moments{};
  HknpMat3 axes{};
};

// Requires a finite centre and a finite positive radius, otherwise std::invalid_argument is raised.
HknpSolidMass sphere_solid(const HknpVec3& centre, float radius);

// Constraint: the capsule is the sphere-swept segment point_a to point_b; coincident ends give a sphere.
HknpSolidMass capsule_solid(const HknpVec3& point_a, const HknpVec3& point_b, float radius);

// Constraint: the input is read as symmetric, so only its symmetric part is diagonalised.
HknpPrincipalAxes principal_axes(const HknpMat3& inertia);

// Constraint: packed_orientation is the caller's input, and the principal axes are not packed here.
HknpMassProperties compose_mass_properties(const HknpSolidMass& solid, float density, std::uint64_t packed_orientation);

// Constraint: inverse_inertia is 1 / (density * inertia_raw); orientation is the caller's.
HknpDynInertia compose_dyn_inertia(const HknpSolidMass& solid, float density, const HknpVec4& orientation);

}  // namespace fo4::nif
