#pragma once

#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/physics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::hkx::detail {

struct ByteWrite {
  std::size_t at{0};
  std::vector<std::byte> bytes;
};

struct CapsuleRefusal {
  bool shared{false};
  std::string reason;
};

// Invariant: writes holds data-section offsets; it is empty when the requested endpoints and radius equal the stored ones.
struct CapsuleChange {
  HkxVec3 a{};
  HkxVec3 b{};
  float radius{0.0f};
  std::vector<ByteWrite> writes;
  std::vector<HkxVec3> vertices;
  // Invariant: old decoded shape inertia divided by new, per lane, so a motion row derived from the shape scales the same way.
  std::array<double, 3> inertia_ratio{1.0, 1.0, 1.0};
};

// Constraint: nullopt means the stored polytope, plane offsets, volume, centre and inertia all match the capsule model measured on vanilla data.
std::optional<CapsuleRefusal> check_capsule(const HkxPackfile& pack, std::size_t object);

// Constraint: the capsule is verified against the capsule model inside this call, and the direction of the capsule must not change.
std::optional<CapsuleRefusal> derive_capsule(const HkxPackfile& pack, std::size_t object, const std::optional<HkxVec3>& a,
                                             const std::optional<HkxVec3>& b, const std::optional<float>& radius,
                                             CapsuleChange& out);

}  // namespace fo4::hkx::detail
