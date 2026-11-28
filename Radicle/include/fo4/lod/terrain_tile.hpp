#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/terrain_heightfield.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fo4::lod {

// Invariant: stored coordinates are world / level, so a tile always spans 0..4096 in x and y.
struct TileVertex {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float u{0.0f};
  float v{0.0f};
};

// Invariant: one quad per cell, four vertices and two triangles; cells[quad] = cx * level + cy, the segment at level 4.
struct TileWaterShape {
  std::vector<TileVertex> vertices;
  std::vector<std::uint16_t> triangles;
  std::vector<std::uint16_t> cells;
  std::array<float, 4> bound{};
};

struct TerrainTile {
  std::int32_t level{0};
  std::int32_t origin_cell_x{0};
  std::int32_t origin_cell_y{0};
  // Invariant: side = (heightfield side - 1) / step + 1 stays <= 129 so a u16 index holds every vertex.
  std::size_t step{1};
  std::size_t side{0};
  // Invariant: land vertices first, then one skirt twin per touched ring vertex of the land box, counter-clockwise.
  std::vector<TileVertex> land_vertices;
  std::vector<std::uint16_t> land_triangles;
  // Invariant: level 4 holds one shape per distinct water height, ascending; every coarser level holds one.
  std::vector<TileWaterShape> water_shapes;
  std::size_t land_cells{0};
  std::size_t water_cells{0};
  // Invariant: center xyz and radius in stored units; extents are half sizes in world units.
  std::array<float, 4> land_bound{};
  std::array<float, 6> land_aabb{};
  std::array<float, 6> water_aabb{};
};

// Invariant: every tile of a world reuses one WorldTerrain, so a world is decoded once per run.
TerrainTile build_terrain_tile(const WorldTerrain& terrain, const LodSettings& settings, std::int32_t level,
                               std::int32_t cell_x, std::int32_t cell_y);

// Constraint: single-tile callers decode the world here; batch callers must pass a WorldTerrain instead.
TerrainTile build_terrain_tile(const env::Environment& environment, const FormKey& worldspace,
                               const LodSettings& settings, std::int32_t level, std::int32_t cell_x,
                               std::int32_t cell_y);

// Invariant: the texture stem is Data\Textures\Terrain\<World>\<World>.<level>.<x>.<y>.
std::vector<std::byte> write_terrain_btr(const TerrainTile& tile, std::string_view worldspace);

}  // namespace fo4::lod
