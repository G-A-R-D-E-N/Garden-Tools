#include "fo4/environment/navmesh_rules.hpp"

#include "fo4/environment/navmesh.hpp"

#include <cstddef>

namespace fo4::env {
namespace {

bool triangle_in_range(const NavmeshGeometry& geometry, std::int16_t index) {
  if (index < 0 || static_cast<std::size_t>(index) >= geometry.triangles.size()) return false;
  for (const auto vertex : geometry.triangles[static_cast<std::size_t>(index)].vertices) {
    if (vertex >= geometry.vertices.size()) return false;
  }
  return true;
}

}  // namespace

PathVerdict door_verdict(const std::optional<LockData>& lock, const DoorState& state) {
  if (!state.locked) return PathVerdict::passable;
  if (!lock || lock->level != k_lock_requires_key || lock->key.owner().empty()) return PathVerdict::unknown;
  return state.npc_has_key ? PathVerdict::needs_opening : PathVerdict::blocked;
}

PathVerdict edge_link_verdict(std::uint32_t link_type) {
  return link_type == 0 ? PathVerdict::passable : PathVerdict::unknown;
}

std::optional<TeleportLeg> teleport_leg(const Environment& env, const RecordRef& load_door,
                                        const DoorState& destination_state) {
  const auto placed = decode_placed_reference(env, load_door);
  if (!placed.teleport) return std::nullopt;
  TeleportLeg leg;
  leg.destination_door = placed.teleport->door;
  const RecordRef* door = env.winner(leg.destination_door);
  if (door == nullptr || !is_placed_reference_signature(door->record->header.signature)) return leg;
  const auto cell = env.cell_of(*door);
  if (!cell) return leg;
  leg.destination_cell = *cell;
  const auto destination = decode_placed_reference(env, *door);
  for (const auto& child : env.cell_children(leg.destination_cell)) {
    if (child.ref->record->header.signature != Signature::from_string("NAVM")) continue;
    const auto geometry = decode_navmesh(env, *child.ref);
    if (!geometry) continue;
    for (const auto& link : geometry->doors) {
      if (link.door != leg.destination_door) continue;
      if (!triangle_in_range(*geometry, link.triangle)) continue;
      leg.navmesh = child.ref->record->form_key;
      leg.triangle = link.triangle;
      leg.verdict = door_verdict(destination.lock, destination_state);
      return leg;
    }
  }
  return leg;
}

}  // namespace fo4::env
