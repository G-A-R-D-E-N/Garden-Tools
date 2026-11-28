#include "fo4/lod/terrain_heightfield.hpp"

#include "fo4/environment/terrain.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/parallel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fo4::lod {
namespace {

constexpr std::size_t k_cell_span = env::k_land_grid - 1;
constexpr float k_seam_tolerance = 0.01f;

// Block alignment must follow the LodSettings southwest corner, not absolute zero.
void check_placement(const LodSettings& settings, std::int32_t level, std::int32_t origin_cell_x,
                     std::int32_t origin_cell_y) {
  const LodTile tile{std::string{}, level, origin_cell_x, origin_cell_y, LodTileKind::terrain};
  const auto placement = check_lod_tile_placement(settings, tile);
  if (placement != LodTilePlacement::ok) {
    throw LodError(std::string{"terrain chunk placement is "} +
                   std::string{lod_tile_placement_name(placement)});
  }
}

bool all_finite(const env::LandHeightGrid& grid) {
  for (const float height : grid.heights) {
    if (!std::isfinite(height)) return false;
  }
  return true;
}

}  // namespace

LandGrids collect_land_grids(const env::Environment& environment, const FormKey& worldspace, LandDetail detail) {
  LandGrids out;
  // The worldspace persistent CELL sits outside the block grid and must not be walked.
  const auto cells = environment.worldspace_cells(worldspace);
  if (cells.empty()) return out;
  const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
  const auto chunks = std::min<std::size_t>(cells.size(), std::max<std::size_t>(1, hardware));
  std::vector<LandGrids> parts(chunks);
  parallel_for(chunks, [&](std::size_t index) {
    auto& part = parts[index];
    const auto begin = cells.size() * index / chunks;
    const auto end = cells.size() * (index + 1) / chunks;
    for (auto slot = begin; slot < end; ++slot) {
      const auto* cell = cells[slot];
      std::int32_t grid_x = 0;
      std::int32_t grid_y = 0;
      try {
        const auto data = env::decode_cell(environment, *cell);
        if (!data.grid) {
          ++part.bad_cells;
          continue;
        }
        grid_x = data.grid->x;
        grid_y = data.grid->y;
      } catch (const env::WorldDecodeError&) {
        ++part.bad_cells;
        continue;
      }
      const auto* land = environment.cell_landscape(cell->record->form_key);
      if (land == nullptr) continue;
      // cell_landscape does not filter deleted records; a deleted LAND must mean no terrain.
      if (land->record->header.flags.is_deleted()) continue;
      env::LandscapeData landscape;
      try {
        landscape = env::decode_landscape(environment, *land);
      } catch (const env::WorldDecodeError&) {
        ++part.bad_land;
        continue;
      }
      auto heights = env::decode_land_heights(landscape);
      if (!heights) {
        // Invariant: decode_land_heights also rejects non-finite values, which are bad_land, not a missing VHGT.
        const bool vhgt_present = landscape.height_offset && landscape.heights.size() == env::k_land_vertices;
        ++(vhgt_present ? part.bad_land : part.missing_vhgt);
        continue;
      }
      if (!all_finite(*heights)) {
        ++part.bad_land;
        continue;
      }
      if (detail == LandDetail::presence) {
        if (!part.present.insert({grid_x, grid_y}).second) ++part.duplicate_cells;
        continue;
      }
      if (!part.grids.try_emplace({grid_x, grid_y}, std::move(*heights)).second) ++part.duplicate_cells;
    }
  });
  // Invariant: chunks merge in cell order, so a parallel collect reports the same grids and counts as a serial one.
  for (auto& part : parts) {
    out.bad_cells += part.bad_cells;
    out.bad_land += part.bad_land;
    out.missing_vhgt += part.missing_vhgt;
    out.duplicate_cells += part.duplicate_cells;
    for (auto& entry : part.grids) {
      if (!out.grids.try_emplace(entry.first, std::move(entry.second)).second) ++out.duplicate_cells;
    }
    for (const auto& key : part.present) {
      if (!out.present.insert(key).second) ++out.duplicate_cells;
    }
  }
  return out;
}

WorldTerrain collect_world_terrain(const env::Environment& environment, const FormKey& worldspace,
                                   LandDetail detail) {
  WorldTerrain out;
  out.land = collect_land_grids(environment, worldspace, detail);
  if (const auto* world = environment.winner(worldspace)) {
    try {
      out.default_land_height = env::decode_worldspace(environment, *world).default_land_height;
      const auto selection = env::resolve_water(environment, *world);
      // Constraint: a WRLD always resolves a height, so only a real WATR record means the world has water.
      out.has_water = selection.record != nullptr;
      if (selection.height) out.default_water_height = *selection.height;
    } catch (const env::WorldDecodeError&) {
      ++out.water_errors;
    }
  }
  const auto cells = environment.worldspace_cells(worldspace);
  struct WaterPart {
    std::map<std::pair<std::int32_t, std::int32_t>, float> heights;
    std::size_t cells{0};
    std::size_t errors{0};
  };
  if (!cells.empty()) {
    const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
    const auto chunks = std::min<std::size_t>(cells.size(), std::max<std::size_t>(1, hardware));
    std::vector<WaterPart> parts(chunks);
    parallel_for(chunks, [&](std::size_t index) {
      auto& part = parts[index];
      const auto begin = cells.size() * index / chunks;
      const auto end = cells.size() * (index + 1) / chunks;
      for (auto slot = begin; slot < end; ++slot) {
        const auto* cell = cells[slot];
        try {
          const auto data = env::decode_cell(environment, *cell);
          if (!data.grid) continue;
          const auto selection = env::resolve_water(environment, *cell);
          auto height = selection.height;
          // Invariant: an ocean cell stores no XCLW, so vanilla draws it at the worldspace default height.
          if (!height && out.has_water) height = out.default_water_height;
          if (!height) continue;
          if (!part.heights.emplace(std::pair<std::int32_t, std::int32_t>{data.grid->x, data.grid->y}, *height)
                   .second) {
            continue;
          }
          ++part.cells;
        } catch (const env::WorldDecodeError&) {
          ++part.errors;
        }
      }
    });
    // Invariant: chunks merge in cell order, so the first cell of a grid keeps its height exactly as a serial run does.
    for (auto& part : parts) {
      out.water_errors += part.errors;
      for (auto& entry : part.heights) {
        if (!out.water_heights.emplace(entry.first, entry.second).second) continue;
        ++out.water_cells;
      }
    }
  }
  return out;
}

TerrainChunk assemble_terrain_chunk(const LandGrids& land, const LodSettings& settings,
                                    std::int32_t level, std::int32_t origin_cell_x,
                                    std::int32_t origin_cell_y) {
  check_placement(settings, level, origin_cell_x, origin_cell_y);
  if (land.grids.empty() && !land.present.empty()) throw LodError("terrain chunk needs heights, land is presence-only");
  const auto cells = static_cast<std::size_t>(level);
  TerrainChunk chunk;
  chunk.level = level;
  chunk.origin_cell_x = origin_cell_x;
  chunk.origin_cell_y = origin_cell_y;
  chunk.side = cells * k_cell_span + 1;
  chunk.heights.assign(chunk.side * chunk.side, std::numeric_limits<float>::quiet_NaN());
  chunk.cell_present.assign(cells * cells, 0);
  for (std::size_t cy = 0; cy < cells; ++cy) {
    for (std::size_t cx = 0; cx < cells; ++cx) {
      const std::pair<std::int32_t, std::int32_t> key{
          origin_cell_x + static_cast<std::int32_t>(cx), origin_cell_y + static_cast<std::int32_t>(cy)};
      const auto found = land.grids.find(key);
      if (found == land.grids.end()) continue;
      chunk.cell_present[cy * cells + cx] = 1;
      for (std::size_t row = 0; row < env::k_land_grid; ++row) {
        for (std::size_t column = 0; column < env::k_land_grid; ++column) {
          const float value = found->second.heights[row * env::k_land_grid + column];
          if (!std::isfinite(value)) {
            ++chunk.non_finite;
            continue;
          }
          float& slot = chunk.heights[(cy * k_cell_span + row) * chunk.side + cx * k_cell_span + column];
          if (!std::isnan(slot)) {
            if (std::fabs(slot - value) > k_seam_tolerance) ++chunk.seam_conflicts;
            continue;
          }
          slot = value;
        }
      }
    }
  }
  return chunk;
}

}  // namespace fo4::lod
