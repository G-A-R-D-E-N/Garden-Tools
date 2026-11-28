#pragma once

#include "fo4/nif/nif.hpp"

#include <cstdint>
#include <vector>

namespace fo4::nif {

enum class CollisionIssue : std::uint8_t {
  dangling_object,
  system_without_object,
  body_out_of_range,
  layer_zero,
  bsx_mismatch,
  one_child_compound,
  unsupported_shape,
  unread_atom,
  undecodable_system,
};

struct CollisionFinding {
  CollisionIssue issue{CollisionIssue::dangling_object};
  std::uint32_t block{0};
};

// Constraint: malformed collision structure raises fo4::parse_error through read_collision_blobs; only semantic faults become findings.
// Constraint: layer_zero is raised for static bodies only, and one_child_compound for dynamic compounds only, because vanilla has both shapes otherwise.
std::vector<CollisionFinding> lint_collision(const NifFile& nif);

}  // namespace fo4::nif
