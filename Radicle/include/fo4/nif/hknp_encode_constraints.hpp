#pragma once

#include "fo4/nif/hknp_decode.hpp"

#include <cstddef>
#include <vector>

namespace fo4::nif {

struct HknpPositionMotor {
  // Constraint: the defaults are the values of the one position motor object measured in vanilla Meshes.ba2.
  float tau{0.8f};
  float damping{1.0f};
  float proportional_recovery_velocity{5.0f};
  float constant_recovery_velocity{0.2f};
};

// Constraint: each writer returns one constraint data object as its class lays it out, with no fixups; fields the decoder does not keep come from a vanilla template.
std::vector<std::byte> encode_ragdoll_constraint(const HknpConstraint& constraint);
std::vector<std::byte> encode_limited_hinge_constraint(const HknpConstraint& constraint);
std::vector<std::byte> encode_ball_and_socket_constraint(const HknpConstraint& constraint);
std::vector<std::byte> encode_prismatic_constraint(const HknpConstraint& constraint);
std::vector<std::byte> encode_breakable_constraint(const HknpConstraint& constraint);
std::vector<std::byte> encode_position_motor(const HknpPositionMotor& motor);

}  // namespace fo4::nif
