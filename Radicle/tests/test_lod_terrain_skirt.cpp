#include "fo4/lod/terrain_tile.hpp"

#include "fo4/nif/nif.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace {

fo4::lod::WorldTerrain terrain_with(
    int cells,
    const std::function<float(int cx, int cy, std::size_t row, std::size_t column)>& height,
    bool with_water, float water_height) {
  fo4::lod::WorldTerrain terrain;
  for (int cy = 0; cy < cells; ++cy) {
    for (int cx = 0; cx < cells; ++cx) {
      fo4::env::LandHeightGrid grid{};
      for (std::size_t row = 0; row < fo4::env::k_land_grid; ++row) {
        for (std::size_t column = 0; column < fo4::env::k_land_grid; ++column) {
          grid.heights[row * fo4::env::k_land_grid + column] = height(cx, cy, row, column);
        }
      }
      const std::pair<std::int32_t, std::int32_t> key{cx, cy};
      terrain.land.grids.emplace(key, grid);
      if (with_water) terrain.water_heights.emplace(key, water_height);
    }
  }
  terrain.has_water = with_water;
  terrain.default_water_height = water_height;
  return terrain;
}

std::size_t unreferenced_land_vertices(const fo4::lod::TerrainTile& tile) {
  std::vector<bool> referenced(tile.land_vertices.size(), false);
  for (const auto index : tile.land_triangles) {
    if (index < referenced.size()) referenced[index] = true;
  }
  std::size_t missing = 0;
  for (const auto used : referenced) {
    if (!used) ++missing;
  }
  return missing;
}

// Invariant: lattice x, y are distinct, so a skirt twin is a vertex repeating an earlier x, y, even on partial borders.
std::size_t skirt_vertex_count(const fo4::lod::TerrainTile& tile) {
  std::set<std::pair<float, float>> seen;
  std::size_t twins = 0;
  for (const auto& vertex : tile.land_vertices) {
    if (!seen.emplace(vertex.x, vertex.y).second) ++twins;
  }
  return twins;
}

std::size_t land_index_at(const fo4::lod::TerrainTile& tile, std::size_t land_count, float x, float y) {
  for (std::size_t index = 0; index < land_count; ++index) {
    if (tile.land_vertices[index].x == x && tile.land_vertices[index].y == y) return index;
  }
  return land_count;
}

std::vector<std::array<std::uint16_t, 3>> skirt_triangles_of(const fo4::lod::TerrainTile& tile, std::size_t land,
                                                              const std::vector<std::pair<float, float>>& ring) {
  std::vector<std::array<std::uint16_t, 3>> expected;
  for (std::size_t k = 0; k < ring.size(); ++k) {
    const auto next = (k + 1) % ring.size();
    const auto top0 = land_index_at(tile, land, ring[k].first, ring[k].second);
    const auto top1 = land_index_at(tile, land, ring[next].first, ring[next].second);
    EXPECT_LT(top0, land) << k;
    EXPECT_LT(top1, land) << k;
    const auto bottom0 = land + k;
    const auto bottom1 = land + next;
    expected.push_back({static_cast<std::uint16_t>(top1), static_cast<std::uint16_t>(top0),
                        static_cast<std::uint16_t>(bottom0)});
    expected.push_back({static_cast<std::uint16_t>(top1), static_cast<std::uint16_t>(bottom0),
                        static_cast<std::uint16_t>(bottom1)});
  }
  std::sort(expected.begin(), expected.end());
  return expected;
}

std::vector<std::array<std::uint16_t, 3>> triangles_from(const fo4::lod::TerrainTile& tile, std::size_t first) {
  std::vector<std::array<std::uint16_t, 3>> got;
  for (std::size_t triangle = first; triangle < tile.land_triangles.size() / 3; ++triangle) {
    got.push_back({tile.land_triangles[triangle * 3], tile.land_triangles[triangle * 3 + 1],
                   tile.land_triangles[triangle * 3 + 2]});
  }
  std::sort(got.begin(), got.end());
  return got;
}

std::array<float, 3> triangle_normal(const fo4::lod::TerrainTile& tile, std::size_t triangle) {
  const auto& p0 = tile.land_vertices.at(tile.land_triangles.at(triangle * 3));
  const auto& p1 = tile.land_vertices.at(tile.land_triangles.at(triangle * 3 + 1));
  const auto& p2 = tile.land_vertices.at(tile.land_triangles.at(triangle * 3 + 2));
  const float e1[3] = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
  const float e2[3] = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
  return {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
}

}  // namespace

// Detects (regression): a land shape without the vanilla skirt, or one that drifts from the measured arithmetic:
// nv = nLand + nBorder, nt = nLandTris + 2 * nBorder, the ring as the LAST vertices from the south-west corner
// counter-clockwise, each twin on its land vertex's x, y and UV, and (t1,t0,b0),(t1,b0,b1) per ring edge.
// Land at world z -352 is local -88, so the skirt must land on the vanilla -338. A flat tile has the vanilla counts (3x3 lattice; vanilla border vertices sit one sample in from the corners, ours at mid-edge),
// 9 land + 8 twins = 17 vertices and 8 + 16 = 24 triangles, as in 3,595 vanilla tiles.
TEST(LodTerrainTile, AFlatLevelFourTileCarriesTheVanillaSkirtShape) {
  const auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return -352.0f; }, false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_EQ(tile.side, 3u);
  ASSERT_EQ(tile.land_vertices.size(), 17u);
  ASSERT_EQ(tile.land_triangles.size() / 3, 24u);
  const auto last = tile.side - 1;
  const auto land = tile.side * tile.side;
  const auto border = 4 * last;
  ASSERT_EQ(tile.land_vertices.size(), land + border);
  ASSERT_EQ(tile.land_triangles.size() / 3, 2 * last * last + 2 * border);
  const float spacing = 4096.0f / static_cast<float>(last);
  std::vector<std::pair<float, float>> ring;
  for (std::size_t i = 0; i < last; ++i) ring.emplace_back(static_cast<float>(i) * spacing, 0.0f);
  for (std::size_t i = 0; i < last; ++i) ring.emplace_back(4096.0f, static_cast<float>(i) * spacing);
  for (std::size_t i = last; i > 0; --i) ring.emplace_back(static_cast<float>(i) * spacing, 4096.0f);
  for (std::size_t i = last; i > 0; --i) ring.emplace_back(0.0f, static_cast<float>(i) * spacing);
  ASSERT_EQ(ring.size(), border);
  for (std::size_t k = 0; k < border; ++k) {
    const auto top0 = land_index_at(tile, land, ring[k].first, ring[k].second);
    ASSERT_LT(top0, land);
    const auto bottom0 = land + k;
    const auto& twin = tile.land_vertices[top0];
    const auto& skirt = tile.land_vertices[bottom0];
    EXPECT_FLOAT_EQ(skirt.x, ring[k].first) << k;
    EXPECT_FLOAT_EQ(skirt.y, ring[k].second) << k;
    EXPECT_FLOAT_EQ(skirt.u, twin.u) << k;
    EXPECT_FLOAT_EQ(skirt.v, twin.v) << k;
    EXPECT_FLOAT_EQ(skirt.u, skirt.x / 4096.0f) << k;
    EXPECT_FLOAT_EQ(skirt.v, 1.0f - skirt.y / 4096.0f) << k;
    EXPECT_FLOAT_EQ(twin.z, -88.0f) << k;
    EXPECT_FLOAT_EQ(skirt.z, -338.0f) << k;
  }
  EXPECT_EQ(triangles_from(tile, 2 * last * last), skirt_triangles_of(tile, land, ring));
  // Invariant: land triangles never touch a skirt vertex and wind counter-clockwise seen from above.
  for (std::size_t triangle = 0; triangle < 2 * last * last; ++triangle) {
    for (std::size_t corner = 0; corner < 3; ++corner) EXPECT_LT(tile.land_triangles[triangle * 3 + corner], land);
    EXPECT_GT(triangle_normal(tile, triangle)[2], 0.0f) << triangle;
  }
  // Invariant: every skirt triangle faces away from the tile centre, the same winding as the land.
  for (std::size_t triangle = 2 * last * last; triangle < tile.land_triangles.size() / 3; ++triangle) {
    float cx = 0.0f;
    float cy = 0.0f;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      cx += tile.land_vertices[tile.land_triangles[triangle * 3 + corner]].x / 3.0f - 2048.0f / 3.0f;
      cy += tile.land_vertices[tile.land_triangles[triangle * 3 + corner]].y / 3.0f - 2048.0f / 3.0f;
    }
    const auto normal = triangle_normal(tile, triangle);
    EXPECT_GT(normal[0] * cx + normal[1] * cy, 0.0f) << triangle;
  }
}

// Detects (regression): a skirt depth that is not the measured vanilla constant per level in local units
// (250, 250, 187.5, 125 at levels 4, 8, 16, 32), or one taken from an absolute z instead of each border
// vertex's own land z, which a sloped tile (distinct land z all round the ring) exposes.
TEST(LodTerrainTile, DropsTheSkirtByTheVanillaDepthOfEachLevelFromItsOwnLandVertex) {
  for (const auto& entry : {std::pair<std::int32_t, float>{4, 250.0f}, {8, 250.0f}, {16, 187.5f}, {32, 125.0f}}) {
    const std::int32_t level = entry.first;
    const float drop = entry.second;
    const auto terrain = terrain_with(
        level,
        [level](int cx, int cy, std::size_t row, std::size_t column) {
          const auto x = static_cast<float>(cx * 32) + static_cast<float>(column);
          const auto y = static_cast<float>(cy * 32) + static_cast<float>(row);
          return (x + 2.0f * y) * static_cast<float>(level) * 0.25f;
        },
        false, 0.0f);
    const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, level, 0, 0);
    const auto skirt = skirt_vertex_count(tile);
    ASSERT_GT(skirt, 0u) << level;
    const auto land = tile.land_vertices.size() - skirt;
    std::set<float> distinct;
    for (std::size_t k = 0; k < skirt; ++k) {
      const auto& bottom = tile.land_vertices[land + k];
      const auto twin = land_index_at(tile, land, bottom.x, bottom.y);
      ASSERT_LT(twin, land) << level << ' ' << k;
      EXPECT_NEAR(tile.land_vertices[twin].z - bottom.z, drop, 1e-3f) << level << ' ' << k;
      distinct.insert(bottom.z);
    }
    EXPECT_GT(distinct.size(), 4u) << level;
  }
}

// Detects (regression): a skirt that pushes the vertex count past the u16 index or the file's vertex field, or a
// writer that stores fewer vertices or triangles than the tile holds. The roughest level-4 field is the largest
// lattice the 2,048-triangle budget lets through (33 a side), so its ring (128 twins, 256 skirt triangles) is the
// largest the budget allows.
TEST(LodTerrainTile, KeepsTheSkirtedLargestLatticeInsideTheIndexAndFileLimits) {
  const auto terrain = terrain_with(
      4,
      [](int cx, int cy, std::size_t row, std::size_t column) {
        return 40.0f * static_cast<float>((row * 7 + column * 3 + cx + cy) % 11);
      },
      false, 0.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_EQ(tile.side, 33u);
  const auto land = tile.side * tile.side;
  const auto border = 4 * (tile.side - 1);
  EXPECT_EQ(tile.land_vertices.size(), land + border);
  EXPECT_EQ(tile.land_triangles.size() / 3, 2 * (tile.side - 1) * (tile.side - 1) + 2 * border);
  EXPECT_LE(tile.land_vertices.size(), 65535u);
  for (const auto index : tile.land_triangles) EXPECT_LT(index, tile.land_vertices.size());
  EXPECT_EQ(unreferenced_land_vertices(tile), 0u);
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1u);
  EXPECT_EQ(shapes[0].uvs.size(), tile.land_vertices.size() * 2);
  EXPECT_EQ(shapes[0].tris.size(), tile.land_triangles.size());
}

// Detects (regression): a skirt that bridges a missing border cell or drops the border altogether. A cell missing
// at the south-west corner leaves no land along its two border spans, so only the 14 ring edges with land inside
// get a skirt. The land quads still reach every side, so the bounding-box ring is the full 16-edge lattice ring
// (24 land vertices, 15 twins for ring positions 1..15, 14 skirted edges); the gap stays unskirted. Vanilla
// full-grid worlds never have such a gap, so this span rule itself is UNVERIFIED.
TEST(LodTerrainTile, SkirtsOnlyTheBorderSpansThatHaveLand) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  terrain.land.grids.erase(decltype(terrain.land.grids)::key_type{0, 0});
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_EQ(tile.side, 5u);
  EXPECT_EQ(skirt_vertex_count(tile), 15u);
  EXPECT_EQ(tile.land_vertices.size(), 24u + 15u);
  EXPECT_EQ(tile.land_triangles.size() / 3, 30u + 2 * 14u);
  const auto land = tile.land_vertices.size() - 15u;
  for (std::size_t triangle = 30; triangle < tile.land_triangles.size() / 3; ++triangle) {
    bool on_missing_span = true;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const auto& vertex = tile.land_vertices.at(tile.land_triangles[triangle * 3 + corner]);
      if (!((vertex.y == 0.0f && vertex.x <= 1024.0f) || (vertex.x == 0.0f && vertex.y <= 1024.0f))) {
        on_missing_span = false;
      }
    }
    EXPECT_FALSE(on_missing_span) << triangle;
  }
  for (std::size_t index = land; index < tile.land_vertices.size(); ++index) {
    EXPECT_FALSE(tile.land_vertices[index].x == 0.0f && tile.land_vertices[index].y == 0.0f) << index;
  }
}

// Detects (regression): a ring taken from the lattice rim instead of the bounding box of the land quads. With the
// last cell column missing (land x 0..3072, the Far Harbor clipped-tile shape) the rim leaves the cut line at
// x = 3072 open and finds no land inside the x = 4096 side; the box ring skirts the cut line. Level 4, step 32,
// 5x5 lattice: 20 land vertices in 4 columns, 12 land quads, ring of 14 edges all with land inside.
TEST(LodTerrainTile, SkirtsTheCutLineWhereTheLandExtentIsUnderFullWidth) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  for (int cy = 0; cy < 4; ++cy) terrain.land.grids.erase({3, cy});
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_EQ(tile.side, 5u);
  constexpr std::size_t land = 20;
  ASSERT_EQ(tile.land_vertices.size(), land + 14u);
  ASSERT_EQ(tile.land_triangles.size() / 3, 24u + 28u);
  std::vector<std::pair<float, float>> ring;
  for (int i = 0; i < 3; ++i) ring.emplace_back(static_cast<float>(i) * 1024.0f, 0.0f);
  for (int i = 0; i < 4; ++i) ring.emplace_back(3072.0f, static_cast<float>(i) * 1024.0f);
  for (int i = 3; i > 0; --i) ring.emplace_back(static_cast<float>(i) * 1024.0f, 4096.0f);
  for (int i = 4; i > 0; --i) ring.emplace_back(0.0f, static_cast<float>(i) * 1024.0f);
  for (std::size_t k = 0; k < ring.size(); ++k) {
    const auto& twin = tile.land_vertices[land_index_at(tile, land, ring[k].first, ring[k].second)];
    const auto& skirt = tile.land_vertices[land + k];
    EXPECT_FLOAT_EQ(skirt.x, ring[k].first) << k;
    EXPECT_FLOAT_EQ(skirt.y, ring[k].second) << k;
    EXPECT_FLOAT_EQ(twin.z - skirt.z, 250.0f) << k;
  }
  EXPECT_EQ(triangles_from(tile, 24), skirt_triangles_of(tile, land, ring));
  std::size_t on_cut_line = 0;
  for (std::size_t first = 72; first < tile.land_triangles.size(); first += 3) {
    const auto at_cut = [&](std::size_t corner) {
      return tile.land_vertices.at(tile.land_triangles[first + corner]).x == 3072.0f;
    };
    if (at_cut(0) && at_cut(1) && at_cut(2)) ++on_cut_line;
  }
  EXPECT_EQ(on_cut_line, 8u);
  for (const auto& vertex : tile.land_vertices) EXPECT_LE(vertex.x, 3072.0f);
}

// Detects (regression): a skirt added to the water shape, water vertices lowered by the land drop, or water
// triangles wound clockwise from above (vanilla water, 20,436 sampled, is counter-clockwise like the land).
TEST(LodTerrainTile, LeavesTheWaterShapeWithoutASkirt) {
  constexpr float k_water = 450.0f;
  const auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, true, k_water);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_GT(skirt_vertex_count(tile), 0u);
  EXPECT_EQ(tile.water_cells, 16u);
  ASSERT_EQ(tile.water_shapes.size(), 1u);
  const auto& water = tile.water_shapes.front();
  EXPECT_EQ(water.vertices.size(), 64u);
  EXPECT_EQ(water.triangles.size() / 3, 32u);
  for (const auto& vertex : water.vertices) EXPECT_FLOAT_EQ(vertex.z, k_water / 4.0f);
  for (std::size_t first = 0; first + 2 < water.triangles.size(); first += 3) {
    const auto& p0 = water.vertices.at(water.triangles[first]);
    const auto& p1 = water.vertices.at(water.triangles[first + 1]);
    const auto& p2 = water.vertices.at(water.triangles[first + 2]);
    EXPECT_GT((p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x), 0.0f) << first / 3;
  }
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 2u);
  EXPECT_EQ(shapes[1].tris.size(), 96u);
}
