#include "uvd_tile_check_internal.hpp"
#include "uvd_tile_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <unordered_set>

namespace fo4::previs::detail {

namespace {

using Slots = std::vector<std::optional<UvdTileBody>>;

std::uint32_t bit_length(std::uint32_t x) { return x == 0 ? 0u : 32u - static_cast<std::uint32_t>(std::countl_zero(x)); }

std::uint64_t map_all(std::uint32_t width) { return (std::uint64_t{1} << width) - 1; }
std::uint64_t map_high(std::uint32_t width) { return width == 0 ? 0 : std::uint64_t{1} << (width - 1); }

void validate_reciprocity(const Slots& slots, UvdTileClaims& claims) {
  const std::uint64_t n = slots.size();
  const auto key = [n](std::uint64_t slot, std::uint64_t face, std::uint64_t target) { return (slot * 6 + face) * n + target; };
  std::unordered_set<std::uint64_t> links;
  for (std::uint64_t slot = 0; slot < n; ++slot) {
    if (!slots[slot]) {
      continue;
    }
    for (const UvdPortal& p : slots[slot]->portals) {
      if (p.outside == 0 && p.target != slot) {
        links.insert(key(slot, p.face, p.target));
      }
    }
  }
  for (const std::uint64_t k : links) {
    const std::uint64_t target = k % n;
    const std::uint64_t slot = k / n / 6;
    const std::uint64_t face = (k / n) % 6;
    claims.check(UvdClaim::PortalReciprocity, links.contains(key(target, face ^ 1u, slot)),
                 [&] { return "slot " + std::to_string(slot) + " face " + std::to_string(face) + " target " + std::to_string(target); });
  }
}

void validate_tile(std::size_t slot, const UvdTileBody& d, UvdTileClaims& claims) {
  const auto where = [slot] { return "slot " + std::to_string(slot); };
  std::uint64_t run = 0;
  bool chain = true;
  bool ordered = true;
  for (const UvdCell& c : d.cells) {
    if (c.portal_start != run) {
      chain = false;
    }
    run += c.portal_count;
    for (const auto* axis : {&c.x, &c.y, &c.z}) {
      if (!((*axis)[0] <= (*axis)[1])) {
        ordered = false;
      }
    }
  }
  claims.check(UvdClaim::CellPortalChain, chain, where);
  claims.check(UvdClaim::CellPortalSum, run == d.portals.size(), where);
  claims.check(UvdClaim::CellAabbOrdered, ordered, where);
  if (d.leaf) {
    std::unordered_set<std::uint32_t> clusters;
    for (const UvdCell& c : d.cells) clusters.insert(c.cluster);
    claims.check(UvdClaim::LeafClusterCount, clusters.size() == d.cluster_count, where);
  }
}

void validate_portal(const UvdPortal& p, const Slots& slots, std::size_t slot, std::size_t index, UvdTileClaims& claims) {
  const auto where = [slot, index] { return "slot " + std::to_string(slot) + " portal " + std::to_string(index); };
  claims.check(UvdClaim::PortalFace, p.face <= 5, where);
  claims.check(UvdClaim::PortalOutsideTarget, (p.outside == 1) == (p.target == k_uvd_outside_target), where);
  if (p.outside != 0) {
    claims.check(UvdClaim::PortalOutsideCellHier, p.target_cell == 0 && p.hier == 0, where);
    return;
  }
  const bool has = p.target < slots.size() && slots[p.target].has_value();
  claims.check(UvdClaim::PortalTargetTile, has, where);
  if (!has) {
    return;
  }
  const UvdTileBody& target = *slots[p.target];
  claims.check(UvdClaim::PortalTargetCell, p.target_cell < target.cells.size(), where);
  claims.check(UvdClaim::PortalHierarchy, (p.hier == 1) == !target.leaf, where);
  if (p.hier == 0 && p.target != slot) {
    claims.check(UvdClaim::PortalPlaneBoundary, p.plane == ((p.face & 1u) != 0 ? 0xffffu : 0u), where);
  }
  if (p.hier == 0 && p.door == 0) {
    claims.check(UvdClaim::PortalRects, p.rect0[0] <= p.rect0[1] && p.rect1[0] <= p.rect1[1], where);
  }
}

void validate_kd(const UvdTileBytes& raw, const UvdTileBody& d, std::size_t slot, UvdTileClaims& claims) {
  const auto where = [slot] { return "slot " + std::to_string(slot); };
  const UvdKd& kd = *d.kd;
  const std::uint64_t n = kd.node_count;
  const std::array<double, 3> lo{d.box[0], d.box[1], d.box[2]};
  const std::array<double, 3> hi{d.box[3], d.box[4], d.box[5]};
  const UvdKdWalk walk = walk_uvd_kd(kd.symbols, kd.splits, lo, hi);
  const std::uint64_t leaves = static_cast<std::uint64_t>(std::count(kd.symbols.begin(), kd.symbols.end(), std::uint8_t{3}));
  claims.check(UvdClaim::KdConsume, walk.ok && leaves == (n + 1) / 2, where);
  claims.check(UvdClaim::KdReencode, encode_uvd_nodes(kd.symbols) == raw.kd->nodes, where);
  bool no_leaf_cut = true;
  for (std::size_t i = 0; i < kd.splits.size(); ++i) {
    if (i < kd.symbols.size() && kd.symbols[i] == 3 && !(kd.splits[i] == 0.0f)) {
      no_leaf_cut = false;
    }
  }
  claims.check(UvdClaim::KdLeafNoCut, no_leaf_cut, where);
  bool cuts_ok = true;
  for (std::size_t i = 0; i < walk.cuts.size(); ++i) {
    const auto& c = walk.cuts[i];
    if (c && c->stored && !(c->cut >= walk.regions[i].lo[c->axis] && c->cut <= walk.regions[i].hi[c->axis])) {
      cuts_ok = false;
    }
  }
  claims.check(UvdClaim::KdCutsInside, cuts_ok, where);
  const std::uint32_t w = kd.map_width;
  const std::uint64_t all = map_all(w);
  const std::uint64_t high = map_high(w);
  bool map_ok = true;
  for (const std::uint32_t v : kd.entries) {
    const bool good = v == all || (v >= high ? !d.bsp.empty() && v - high < d.bsp.size() : v < d.cells.size());
    if (!good) {
      map_ok = false;
    }
  }
  claims.check(UvdClaim::KdMapEntries, map_ok, where);
  if (d.bsp.empty()) {
    claims.check(UvdClaim::KdMapWidth, w == bit_length(static_cast<std::uint32_t>(d.cells.size() - 1)) + 1, where);
    std::vector<char> seen(d.cells.size(), 0);
    for (const std::uint32_t v : kd.entries) {
      if (v < seen.size()) {
        seen[v] = 1;
      }
    }
    claims.check(UvdClaim::KdEveryCell, std::count(seen.begin(), seen.end(), char{0}) == 0, where);
  } else {
    const std::int64_t top = std::max<std::int64_t>(static_cast<std::int64_t>(d.cells.size()) - 1, static_cast<std::int64_t>(d.bsp.size()) - 1);
    claims.check(UvdClaim::KdMapWidthBsp, w == bit_length(static_cast<std::uint32_t>(top)) + 1, where);
  }
}

void validate_bsp(const UvdTileBody& d, std::size_t slot, UvdTileClaims& claims) {
  const auto where = [slot] { return "slot " + std::to_string(slot); };
  bool plane_ok = true;
  for (const UvdPlane& p : d.planes) {
    const double len = std::hypot(static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2]));
    if (!std::all_of(p.begin(), p.end(), [](float v) { return std::isfinite(v); }) || std::abs(len - 1) > 1e-3) {
      plane_ok = false;
    }
  }
  claims.check(UvdClaim::PlaneUnit, plane_ok, where);
  if (d.bsp.empty()) {
    return;
  }
  const UvdKd& kd = *d.kd;
  claims.check(UvdClaim::BspPlaneRange, std::all_of(d.bsp.begin(), d.bsp.end(), [&](const UvdBspNode& b) { return b.plane < d.planes.size(); }), where);
  const auto child_ok = [&](const UvdBspNode& b) {
    return (b.low_is_cell != 0 ? b.low < d.cells.size() : b.low < d.bsp.size()) &&
           (b.high_is_cell != 0 ? b.high < d.cells.size() : b.high < d.bsp.size());
  };
  claims.check(UvdClaim::BspChildRange, std::all_of(d.bsp.begin(), d.bsp.end(), child_ok), where);
  const std::uint64_t all = map_all(kd.map_width);
  const std::uint64_t high = map_high(kd.map_width);
  std::unordered_set<std::uint64_t> reached;
  for (const std::uint32_t v : kd.entries) {
    if (v != all && v >= high) {
      reached.insert(v - high);
    }
  }
  std::vector<std::uint64_t> stack(reached.begin(), reached.end());
  while (!stack.empty()) {
    const std::uint64_t at = stack.back();
    stack.pop_back();
    if (at >= d.bsp.size()) {
      continue;
    }
    const UvdBspNode& b = d.bsp[at];
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 2> kids{{{b.low_is_cell, b.low}, {b.high_is_cell, b.high}}};
    for (const auto& [is_cell, idx] : kids) {
      if (is_cell == 0 && reached.insert(idx).second) {
        stack.push_back(idx);
      }
    }
  }
  claims.check(UvdClaim::BspReachable, reached.size() == d.bsp.size(), where);
}

}  // namespace

void validate_uvd_bodies(const UvdTileModel& model, const UvdBodies& bodies, UvdTileClaims& claims) {
  validate_reciprocity(bodies.slots, claims);
  for (std::size_t slot = 0; slot < bodies.slots.size(); ++slot) {
    if (!bodies.slots[slot]) {
      continue;
    }
    const UvdTileBody& d = *bodies.slots[slot];
    validate_tile(slot, d, claims);
    for (std::size_t i = 0; i < d.portals.size(); ++i) validate_portal(d.portals[i], bodies.slots, slot, i, claims);
    if (d.leaf && d.kd && model.slots[slot] && model.slots[slot]->kd) {
      validate_kd(*model.slots[slot], d, slot, claims);
      validate_bsp(d, slot, claims);
    }
  }
}

}  // namespace fo4::previs::detail
