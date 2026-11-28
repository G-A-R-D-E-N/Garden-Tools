#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/placed.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace fo4::env {

enum class PathVerdict : std::uint8_t { unknown, passable, needs_opening, blocked };

struct DoorState {
  bool locked{false};
  bool npc_has_key{false};
};

inline constexpr std::uint8_t k_lock_requires_key = 255;

PathVerdict door_verdict(const std::optional<LockData>& lock, const DoorState& state);

PathVerdict edge_link_verdict(std::uint32_t link_type);

struct TeleportLeg {
  FormKey destination_door;
  FormKey destination_cell;
  std::optional<FormKey> navmesh;
  std::int16_t triangle{-1};
  std::optional<std::array<float, 3>> approach;
  PathVerdict verdict{PathVerdict::unknown};
};

std::optional<TeleportLeg> teleport_leg(const Environment& env, const RecordRef& load_door,
                                        const DoorState& destination_state);

}  // namespace fo4::env
