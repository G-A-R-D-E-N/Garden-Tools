#include "fo4/lod/terrain_tile.hpp"

#include "fo4/lod/lod.hpp"
#include "fo4/nif/nif.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
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

std::vector<float> block_floats(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t count) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::vector<float> out;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t bits = 0;
    for (std::size_t k = 0; k < 4; ++k) bits |= static_cast<std::uint32_t>(bytes[info.offset + i * 4 + k]) << (8 * k);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    out.push_back(value);
  }
  return out;
}

std::uint32_t block_word(const fo4::nif::NifFile& nif, std::uint32_t block, std::size_t index) {
  const auto& info = nif.blocks().at(block);
  const auto* bytes = reinterpret_cast<const unsigned char*>(nif.bytes().data());
  std::uint32_t value = 0;
  for (std::size_t k = 0; k < 4; ++k) value |= static_cast<std::uint32_t>(bytes[info.offset + index * 4 + k]) << (8 * k);
  return value;
}

std::size_t count_type(const fo4::nif::NifFile& nif, std::string_view type) {
  std::size_t count = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type == type) ++count;
  }
  return count;
}

void add_water(fo4::lod::WorldTerrain& terrain, std::int32_t cx, std::int32_t cy, float height) {
  terrain.water_heights.emplace(std::pair<std::int32_t, std::int32_t>{cx, cy}, height);
}

// Invariant: a level-4 water segment table has 16 entries; -1 marks an empty one, else the start index of its quad.
std::array<std::int32_t, 16> segment_starts(std::initializer_list<std::pair<std::size_t, std::int32_t>> quads) {
  std::array<std::int32_t, 16> out{};
  out.fill(-1);
  for (const auto& [segment, start] : quads) out.at(segment) = start;
  return out;
}

}  // namespace

// Detects (regression): level-4 water written as one plain BSTriShape for the whole tile. Measured on all 4,235
// vanilla level-4 water shapes: one BSSubIndexTriShape per distinct height in ascending order, each with 16 segments
// where segment cx * 4 + cy holds that cell's quad (so cell 1,2 is segment 6, not 9), a start of 0 for an empty one,
// and the block order node, land, shader, texture set, water node, then shape and shader per height, then bounds.
TEST(LodTerrainTile, SplitsLevelFourWaterIntoOneSegmentedShapePerHeightInAscendingOrder) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  terrain.has_water = true;
  add_water(terrain, 1, 2, 300.0f);
  add_water(terrain, 0, 0, 450.0f);
  add_water(terrain, 3, 2, 100.0f);
  add_water(terrain, 2, 1, 300.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  EXPECT_EQ(tile.water_cells, 4u);
  ASSERT_EQ(tile.water_shapes.size(), 3u);
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain), fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.blocks().size(), 15u);
  EXPECT_EQ(count_type(nif, "BSTriShape"), 1u);
  EXPECT_EQ(count_type(nif, "BSSubIndexTriShape"), 3u);
  EXPECT_EQ(count_type(nif, "BSEffectShaderProperty"), 3u);

  const std::array<float, 3> heights{100.0f, 300.0f, 450.0f};
  const std::vector<std::array<std::int32_t, 16>> starts{segment_starts({{14, 0}}), segment_starts({{6, 0}, {9, 6}}),
                                                         segment_starts({{0, 0}})};
  for (std::uint32_t shape = 0; shape < heights.size(); ++shape) {
    const auto block = 5 + 2 * shape;
    EXPECT_EQ(nif.blocks().at(block).type, "BSSubIndexTriShape") << shape;
    EXPECT_EQ(nif.blocks().at(block + 1).type, "BSEffectShaderProperty") << shape;
    EXPECT_EQ(block_word(nif, block, 23), block + 1) << shape;
    EXPECT_FLOAT_EQ(tile.water_shapes[shape].vertices.front().z * 4.0f, heights[shape]) << shape;
    const auto table = fo4::nif::read_segments(nif, block);
    EXPECT_EQ(table.primitives, tile.water_shapes[shape].triangles.size() / 3) << shape;
    EXPECT_EQ(table.total_segments, 16u) << shape;
    ASSERT_EQ(table.segments.size(), 16u) << shape;
    for (std::size_t k = 0; k < table.segments.size(); ++k) {
      const auto& segment = table.segments[k];
      const bool wet = starts[shape][k] >= 0;
      EXPECT_EQ(segment.primitives, wet ? 2u : 0u) << shape << ' ' << k;
      EXPECT_EQ(segment.start_index, wet ? static_cast<std::uint32_t>(starts[shape][k]) : 0u) << shape << ' ' << k;
      EXPECT_EQ(segment.parent_array_index, 0xFFFFFFFFU) << shape << ' ' << k;
      EXPECT_TRUE(segment.sub_segments.empty()) << shape << ' ' << k;
    }
  }
  EXPECT_EQ((std::vector<std::uint32_t>{block_word(nif, 4, 18), block_word(nif, 4, 19), block_word(nif, 4, 20),
                                        block_word(nif, 4, 21), block_word(nif, 4, 22)}),
            (std::vector<std::uint32_t>{3u, 5u, 7u, 9u, 11u}));
  EXPECT_EQ(block_word(nif, 0, 21), 13u);
  EXPECT_EQ(block_word(nif, 11, 0), 12u);
  EXPECT_EQ(block_word(nif, 13, 0), 14u);
  EXPECT_EQ(block_floats(nif, 12, 6), (std::vector<float>{8192.0f, 6144.0f, 275.0f, 8192.0f, 6144.0f, 175.0f}));
}

// Detects (regression): a malformed level-4 water shape written with a segment table that names the wrong quads
// instead of failing: a shape without one cell per quad, or two quads claiming one cell.
TEST(LodTerrainTile, RefusesALevelFourWaterShapeWhoseCellsDoNotMatchItsQuads) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  terrain.has_water = true;
  add_water(terrain, 0, 0, 100.0f);
  add_water(terrain, 1, 0, 100.0f);
  auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_EQ(tile.water_shapes.size(), 1u);
  EXPECT_NO_THROW(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  auto doubled = tile;
  doubled.water_shapes.front().cells = {4, 4};
  EXPECT_THROW(fo4::lod::write_terrain_btr(doubled, "Commonwealth"), std::invalid_argument);
  tile.water_shapes.front().cells.pop_back();
  EXPECT_THROW(fo4::lod::write_terrain_btr(tile, "Commonwealth"), std::invalid_argument);
}

// Detects (regression): coarser tiles split by height or segmented like level 4 (vanilla: all 1,548 tiles at levels
// 8, 16 and 32 carry one plain BSTriShape with no segment table), or quads in row order (vanilla orders them by
// height, then column, then row).
TEST(LodTerrainTile, KeepsCoarserWaterAsOnePlainShapeOrderedByHeightThenColumnThenRow) {
  auto terrain = terrain_with(8, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  terrain.has_water = true;
  add_water(terrain, 5, 0, 200.0f);
  add_water(terrain, 2, 3, 400.0f);
  add_water(terrain, 2, 1, 200.0f);
  add_water(terrain, 1, 7, 400.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 8, 0, 0);
  ASSERT_EQ(tile.water_shapes.size(), 1u);
  const auto& water = tile.water_shapes.front();
  ASSERT_EQ(water.vertices.size(), 16u);
  const std::vector<std::pair<float, float>> origins{{1024.0f, 512.0f}, {2560.0f, 0.0f}, {512.0f, 3584.0f},
                                                     {1024.0f, 1536.0f}};
  for (std::size_t quad = 0; quad < origins.size(); ++quad) {
    EXPECT_FLOAT_EQ(water.vertices[quad * 4].x, origins[quad].first) << quad;
    EXPECT_FLOAT_EQ(water.vertices[quad * 4].y, origins[quad].second) << quad;
  }
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::terrain), fo4::lod::LodTileStructure::ok);
  EXPECT_EQ(nif.blocks().size(), 11u);
  EXPECT_EQ(nif.blocks().at(5).type, "BSTriShape");
  EXPECT_EQ(count_type(nif, "BSSubIndexTriShape"), 0u);
}

// Detects (regression): a water sphere that is the vertex mean and an AABB spanning the whole tile. Vanilla centres
// the sphere on the box of the water vertices with the radius to the farthest one (5,783 of 5,783 shapes) and the
// AABB is that box in world units (all 5,709 water tiles, within half-float rounding).
TEST(LodTerrainTile, FitsTheWaterSphereAndAabbToTheWetCells) {
  auto terrain = terrain_with(4, [](int, int, std::size_t, std::size_t) { return 10.0f; }, false, 0.0f);
  terrain.has_water = true;
  add_water(terrain, 0, 0, 100.0f);
  add_water(terrain, 1, 0, 100.0f);
  add_water(terrain, 3, 2, 100.0f);
  const auto tile = fo4::lod::build_terrain_tile(terrain, fo4::lod::LodSettings{}, 4, 0, 0);
  ASSERT_EQ(tile.water_shapes.size(), 1u);
  EXPECT_EQ(tile.water_shapes.front().bound, (std::array<float, 4>{2048.0f, 1536.0f, 25.0f, 2560.0f}));
  EXPECT_EQ(tile.water_aabb, (std::array<float, 6>{8192.0f, 6144.0f, 100.0f, 8192.0f, 6144.0f, 0.0f}));
  const auto nif = fo4::nif::read_nif(fo4::lod::write_terrain_btr(tile, "Commonwealth"));
  const auto shape_floats = block_floats(nif, 5, 22);
  EXPECT_EQ((std::vector<float>(shape_floats.begin() + 18, shape_floats.end())),
            (std::vector<float>{2048.0f, 1536.0f, 25.0f, 2560.0f}));
  EXPECT_EQ(block_floats(nif, 8, 6), (std::vector<float>{8192.0f, 6144.0f, 100.0f, 8192.0f, 6144.0f, 0.0f}));
}
