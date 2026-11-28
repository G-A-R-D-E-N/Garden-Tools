#include "uvd_tile_check_internal.hpp"
#include "uvd_tile_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

namespace fo4::previs::detail {

namespace {

constexpr float k_flt_max = std::numeric_limits<float>::max();
constexpr float k_two_pow_24 = 16777216.0f;

std::uint64_t leaf_count(std::span<const std::uint8_t> symbols) {
  return static_cast<std::uint64_t>(std::count(symbols.begin(), symbols.end(), std::uint8_t{3}));
}

bool slot_paths_match(const UvdTileModel& model, std::span<const std::uint8_t> symbols) {
  try {
    const UvdSlotPaths paths = uvd_slot_paths(symbols);
    return paths.path_bits == model.path_bits && paths.bytes == model.slot_paths;
  } catch (const UvdTileLayoutError&) {
    return false;
  }
}

bool cell_starts_match(const UvdTileModel& model) {
  const std::size_t n = model.slots.size();
  if (model.cell_starts.size() < 4 * (n + 1)) {
    return false;
  }
  std::uint64_t run = 0;
  bool ok = true;
  for (std::size_t i = 0; i < n; ++i) {
    if (rd32(model.cell_starts, 4 * i) != run) {
      ok = false;
    }
    run += model.slots[i] ? model.slots[i]->cells.size() / 36 : 0;
  }
  return ok && rd32(model.cell_starts, 4 * n) == run;
}

void validate_top_kd(const UvdTileModel& model, const UvdHeader& h, UvdTileClaims& claims) {
  const std::size_t n = model.slots.size();
  const auto& sym = h.top_symbols;
  const auto where = [] { return std::string{"header"}; };
  const std::array<double, 3> lo{h.world_min[0], h.world_min[1], h.world_min[2]};
  const std::array<double, 3> hi{h.world_max[0], h.world_max[1], h.world_max[2]};
  const UvdKdWalk walk = walk_uvd_kd(sym, h.top_splits, lo, hi, false);
  claims.check(UvdClaim::TopConsume, walk.ok && leaf_count(sym) == (n + 1) / 2, where);
  bool flt_max = h.top_splits.size() == n;
  for (std::size_t i = 0; i < sym.size(); ++i) {
    const bool is_max = i < h.top_splits.size() && h.top_splits[i] == k_flt_max;
    if ((sym[i] == 3) != is_max) {
      flt_max = false;
    }
  }
  claims.check(UvdClaim::TopLeafFltMax, flt_max, where);
  claims.check(UvdClaim::TopReencode, encode_uvd_nodes(sym) == model.top_kd.nodes, where);
  bool leaf_ok = true;
  for (std::size_t i = 0; i < n; ++i) {
    const auto& tile = model.slots[i];
    if (!tile) {
      if (sym[i] == 3) {
        leaf_ok = false;
      }
      continue;
    }
    const auto at = [i] { return "slot " + std::to_string(i); };
    bool same = i < walk.regions.size();
    for (std::size_t a = 0; same && a < 3; ++a) {
      same = static_cast<double>(tile->box[a]) == walk.regions[i].lo[a] && static_cast<double>(tile->box[3 + a]) == walk.regions[i].hi[a];
    }
    claims.check(UvdClaim::TopBox, same, at);
    claims.check(UvdClaim::TopLeafFlag, tile->leaf == (sym[i] == 3), at);
    claims.check(UvdClaim::LodPositive, std::isfinite(h.lod[i]) && h.lod[i] > 0, at);
  }
  claims.check(UvdClaim::TopLeafSlot, leaf_ok, where);
  bool lod_ok = true;
  for (std::size_t i = 0; i < n; ++i) {
    if ((h.lod[i] == -1.0f) != !model.slots[i].has_value()) {
      lod_ok = false;
    }
  }
  claims.check(UvdClaim::LodEmpty, lod_ok, where);
  claims.check(UvdClaim::SlotPaths, slot_paths_match(model, sym), where);
  claims.check(UvdClaim::CellStarts, cell_starts_match(model), where);
}

void validate_objects(const UvdHeader& h, UvdTileClaims& claims) {
  const auto where = [] { return std::string{"header"}; };
  claims.check(UvdClaim::HeaderLodBase, h.lod_base == 512.0f && h.banner.starts_with("T 512.0 "), where);
  bool world_ok = true;
  for (std::size_t a = 0; a < 3; ++a) {
    if (!(std::isfinite(h.world_min[a]) && std::isfinite(h.world_max[a]) && h.world_min[a] <= h.world_max[a])) {
      world_ok = false;
    }
  }
  claims.check(UvdClaim::HeaderWorld, world_ok, where);
  claims.check(UvdClaim::HeaderReserved, h.reserved10 == 0, where);
  bool boxes_ok = true;
  for (const auto& b : h.objects.boxes) {
    if (!std::all_of(b.begin(), b.end(), [](float v) { return std::isfinite(v); })) {
      boxes_ok = false;
    }
    for (std::size_t a = 0; a < 3; ++a) {
      if (!(b[a] <= b[3 + a])) {
        boxes_ok = false;
      }
    }
  }
  claims.check(UvdClaim::ObjectBoxes, boxes_ok, where);
  const std::unordered_set<std::uint32_t> ids(h.objects.ids.begin(), h.objects.ids.end());
  claims.check(UvdClaim::ObjectIds, ids.size() == h.objects.ids.size(), where);
  if (!h.objects.distances) {
    return;
  }
  for (std::size_t i = 0; i < h.objects.distances->size(); ++i) {
    const auto& d = (*h.objects.distances)[i];
    const auto at = [i] { return "object " + std::to_string(i); };
    bool xyz = i < h.objects.boxes.size();
    for (std::size_t a = 0; xyz && a < 3; ++a) xyz = d[a] == h.objects.boxes[i][a] && d[4 + a] == h.objects.boxes[i][3 + a];
    claims.check(UvdClaim::ObjectDistanceBox, xyz, at);
    claims.check(UvdClaim::ObjectPadding, d[3] == 0.0f && (d[7] == k_flt_max || d[7] == k_two_pow_24), at);
  }
}

void validate_clusters_gates_child(const UvdTileModel& model, const UvdHeader& h, UvdTileClaims& claims) {
  const auto where = [] { return std::string{"header"}; };
  std::uint64_t cursor = 0;
  bool chain = true;
  bool ordered = true;
  for (const UvdCluster& c : h.clusters) {
    if (c.portal_start != cursor) {
      chain = false;
    }
    cursor += c.portal_count;
    if (!(c.x[0] <= c.x[1] && c.y[0] <= c.y[1] && c.z[0] <= c.z[1])) {
      ordered = false;
    }
  }
  claims.check(UvdClaim::ClusterChain, chain, where);
  claims.check(UvdClaim::ClusterSum, cursor * 16 == h.cluster_portals.size(), where);
  claims.check(UvdClaim::ClusterAabb, ordered, where);
  claims.check(UvdClaim::GatesPresence, (h.gates.count > 0) == (!h.gates.records.empty() && !h.gates.trailer.empty()), where);
  for (std::size_t i = 0; i < h.child_trees.size(); ++i) {
    if (!h.child_trees[i]) {
      continue;
    }
    const UvdKd& t = *h.child_trees[i];
    const auto at = [i] { return "tree " + std::to_string(i); };
    const UvdKdWalk walk = walk_uvd_kd(t.symbols, {}, {0, 0, 0}, {1, 1, 1});
    claims.check(UvdClaim::ChildConsume, walk.ok && leaf_count(t.symbols) == (static_cast<std::uint64_t>(t.node_count) + 1) / 2, at);
    claims.check(UvdClaim::ChildReencode, model.child.trees[i] && encode_uvd_nodes(t.symbols) == model.child.trees[i]->nodes, at);
  }
  const std::size_t leaves = static_cast<std::size_t>(std::count_if(model.slots.begin(), model.slots.end(), [](const auto& t) { return t && t->leaf; }));
  claims.check(UvdClaim::ChildRows, h.child_rows.size() == leaves, where);
}

}  // namespace

void validate_uvd_header(const UvdTileModel& model, const UvdHeader& header, UvdTileClaims& claims) {
  validate_top_kd(model, header, claims);
  validate_objects(header, claims);
  validate_clusters_gates_child(model, header, claims);
}

}  // namespace fo4::previs::detail
