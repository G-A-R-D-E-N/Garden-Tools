#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_mass.hpp"

#include <optional>
#include <span>

namespace fo4::nif {

struct FittedBox {
  HknpVec3 centre{};
  // Invariant: each row of axes is a unit vector, and the three rows are orthogonal.
  HknpMat3 axes{};
  HknpVec3 half_extents{};
};

struct FittedSphere {
  HknpVec3 centre{};
  float radius{0.0f};
};

struct FittedCapsule {
  HknpVec3 point_a{};
  HknpVec3 point_b{};
  float radius{0.0f};
};

// Constraint: nullopt for an empty or non-finite list; otherwise the box contains every point.
std::optional<FittedBox> fit_oriented_box(std::span<const HknpVec3> points);

// Constraint: nullopt for an empty or non-finite list; otherwise the sphere contains every point.
std::optional<FittedSphere> fit_bounding_sphere(std::span<const HknpVec3> points);

// Constraint: nullopt for an empty or non-finite list; otherwise every point lies within radius of the segment.
std::optional<FittedCapsule> fit_capsule(std::span<const HknpVec3> points);

}  // namespace fo4::nif
