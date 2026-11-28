#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/lod/lod.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>
#include <optional>
#include <set>

namespace fo4::lod {

struct LandGrids {
  // Invariant: keyed by the CELL XCLC grid coordinate, heights in game units.
  std::map<std::pair<std::int32_t, std::int32_t>, env::LandHeightGrid> grids;
  // Counts CELLs that must decode to an XCLC grid and did not.
  std::size_t bad_cells{0};
  // Counts LANDs that must decode to finite VHGT heights and did not.
  std::size_t bad_land{0};
  // Counts LANDs whose VHGT heights must be present but are not.
  std::size_t missing_vhgt{0};
  // Invariant: the first CELL at a grid coordinate wins; later ones must be counted here.
  std::size_t duplicate_cells{0};
  // Invariant: a presence-only collect fills this instead of grids, with the same validity rule.
  std::set<std::pair<std::int32_t, std::int32_t>> present;

  bool has_land(const std::pair<std::int32_t, std::int32_t>& key) const {
    return grids.count(key) != 0 || present.count(key) != 0;
  }
};

// Constraint: presence-only land has no heights, so it must never reach a tile build or a below-terrain cull.
enum class LandDetail { heights, presence };

struct TerrainChunk {
  std::int32_t level{0};
  std::int32_t origin_cell_x{0};
  std::int32_t origin_cell_y{0};
  std::size_t side{0};
  // Invariant: row-major, row = y (south to north) and column = x (west to east); NaN means no vertex.
  std::vector<float> heights;
  // Invariant: row-major per cell, index cy * cells + cx; 1 when the cell had LAND.
  std::vector<std::uint8_t> cell_present;
  // Invariant: counts each later conflicting write to a filled shared-edge vertex (1 cm tolerance).
  std::size_t seam_conflicts{0};
  // Invariant: non-finite heights are dropped and must stay absent.
  std::size_t non_finite{0};
};

LandGrids collect_land_grids(const env::Environment& environment, const FormKey& worldspace,
                             LandDetail detail = LandDetail::heights);

// Invariant: one decode per worldspace, reused by every tile of that world.
struct WorldTerrain {
  LandGrids land;
  // Invariant: keyed like land.grids; a cell without its own water height uses the worldspace default.
  std::map<std::pair<std::int32_t, std::int32_t>, float> water_heights;
  // Invariant: a worldspace with no WATR record has no water, so its cells never gain a default surface.
  bool has_water{false};
  float default_water_height{0.0f};
  // Invariant: the worldspace's own DNAM land height, absent when the WRLD stores none; a cell without LAND is filled with it.
  std::optional<float> default_land_height;
  // Invariant: counts every cell that gained a water surface, from its own height or the worldspace default.
  std::size_t water_cells{0};
  // Counts CELLs and WRLDs whose water could not be decoded; they must not abort the world.
  std::size_t water_errors{0};
};

WorldTerrain collect_world_terrain(const env::Environment& environment, const FormKey& worldspace,
                                   LandDetail detail = LandDetail::heights);

TerrainChunk assemble_terrain_chunk(const LandGrids& land, const LodSettings& settings,
                                    std::int32_t level, std::int32_t origin_cell_x,
                                    std::int32_t origin_cell_y);

}  // namespace fo4::lod
