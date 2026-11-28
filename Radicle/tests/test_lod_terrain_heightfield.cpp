#include "fo4/lod/terrain_heightfield.hpp"

#include "env_node_fixture.hpp"
#include "fo4/lod/generate_support.hpp"
#include "fo4/lod/parallel.hpp"
#include "fo4/plugin/reader.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using fo4::lod::assemble_terrain_chunk;
using fo4::lod::collect_land_grids;
using fo4::lod::LandGrids;
using fo4::lod::LodError;
using fo4::lod::LodSettings;
using fo4::lod::parallel_for;
using fo4::lod::TerrainChunk;

std::vector<std::byte> xclc(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  return out.bytes;
}

std::vector<std::byte> vhgt(float offset,
                            std::vector<std::int8_t> deltas =
                                std::vector<std::int8_t>(fo4::env::k_land_vertices, 0)) {
  fo4::test::ByteWriter out;
  out.f32(offset);
  for (const auto delta : deltas) out.u8(static_cast<std::uint8_t>(delta));
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

Node land(std::uint32_t id, std::vector<std::byte> heights) {
  return rec("LAND", id, {{"VHGT", std::move(heights)}});
}

std::vector<Node> grid_cell(std::uint32_t id, std::int32_t x, std::int32_t y,
                            std::vector<Node> land_records = {}) {
  return {rec("CELL", id, {{"XCLC", xclc(x, y)}}), grp(id, 6, {grp(id, 9, std::move(land_records))})};
}

std::vector<std::byte> world_plugin(const std::vector<std::vector<Node>>& cells) {
  std::vector<Node> flat;
  for (const auto& cell : cells) flat.insert(flat.end(), cell.begin(), cell.end());
  return make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                       grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(flat))})})})});
}

fo4::env::Environment load_env(const std::vector<std::byte>& bytes) {
  std::vector<fo4::Plugin> plugins;
  plugins.push_back(fo4::read_plugin(bytes, "Test.esp"));
  return fo4::env::Environment::from_plugins(std::move(plugins));
}

fo4::FormKey worldspace() {
  return fo4::FormKey::parse("000800:Test.esp");
}

LodSettings settings_at_origin() {
  return LodSettings{};
}

LodSettings far_harbor_settings() {
  return LodSettings{-73, -59, 256, 4, 32};
}

float at(const TerrainChunk& chunk, std::size_t row, std::size_t column) {
  return chunk.heights.at(row * chunk.side + column);
}

}  // namespace

// Detects: a missing cell must stay absent, and a cell must land at its own grid offset.
TEST(LodTerrainHeightfield, AbsentCellsStayNaNAndCellsLandAtTheirGridOffset) {
  const auto env = load_env(world_plugin({grid_cell(0x802, 0, 0, {land(0x812, vhgt(10.0f))}),
                                          grid_cell(0x803, 1, 0, {land(0x813, vhgt(10.0f))}),
                                          grid_cell(0x804, 2, 0)}));
  const auto grids = collect_land_grids(env, worldspace());
  EXPECT_EQ(grids.grids.size(), 2u);
  EXPECT_EQ(grids.bad_cells, 0u);
  EXPECT_EQ(grids.bad_land, 0u);
  EXPECT_EQ(grids.missing_vhgt, 0u);
  EXPECT_EQ(grids.duplicate_cells, 0u);
  const auto chunk = assemble_terrain_chunk(grids, settings_at_origin(), 4, 0, 0);
  EXPECT_EQ(chunk.side, 129u);
  EXPECT_EQ(chunk.heights.size(), 129u * 129u);
  EXPECT_EQ(chunk.cell_present,
            (std::vector<std::uint8_t>{1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(chunk.seam_conflicts, 0u);
  EXPECT_EQ(chunk.non_finite, 0u);
  EXPECT_FLOAT_EQ(at(chunk, 0, 0), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 32, 32), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 32, 64), 80.0f);
  EXPECT_TRUE(std::isnan(at(chunk, 32, 65)));
  EXPECT_TRUE(std::isnan(at(chunk, 33, 0)));
}

// Detects: a later neighbour must not overwrite the first writer's shared edge without a count.
TEST(LodTerrainHeightfield, FirstWriterKeepsTheSharedEdgeAndCountsEveryConflictingWrite) {
  const auto env = load_env(world_plugin({grid_cell(0x802, 0, 0, {land(0x812, vhgt(10.0f))}),
                                          grid_cell(0x803, 1, 0, {land(0x813, vhgt(11.0f))}),
                                          grid_cell(0x804, 0, 1, {land(0x814, vhgt(12.0f))})}));
  const auto chunk =
      assemble_terrain_chunk(collect_land_grids(env, worldspace()), settings_at_origin(), 4, 0, 0);
  EXPECT_FLOAT_EQ(at(chunk, 0, 32), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 32, 0), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 32, 32), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 0, 33), 88.0f);
  EXPECT_EQ(chunk.seam_conflicts, 66u);
}

// Detects: sub-centimetre LAND rounding must not count as a seam conflict.
TEST(LodTerrainHeightfield, SubCentimetreDifferencesOnASharedEdgeAgree) {
  const auto env = load_env(world_plugin({grid_cell(0x802, 0, 0, {land(0x812, vhgt(10.0f))}),
                                          grid_cell(0x803, 1, 0, {land(0x813, vhgt(10.0005f))})}));
  const auto chunk =
      assemble_terrain_chunk(collect_land_grids(env, worldspace()), settings_at_origin(), 4, 0, 0);
  EXPECT_EQ(chunk.seam_conflicts, 0u);
  EXPECT_FLOAT_EQ(at(chunk, 0, 32), 80.0f);
}

// Detects: the chunk side and cell stride must be 32 * level + 1 at every level, not just level 4.
TEST(LodTerrainHeightfield, LevelEightChunkPlacesHeightsAtTheThirtyTwoVertexStride) {
  const auto env = load_env(world_plugin({grid_cell(0x802, 5, 3, {land(0x812, vhgt(20.0f))})}));
  const auto chunk =
      assemble_terrain_chunk(collect_land_grids(env, worldspace()), settings_at_origin(), 8, 0, 0);
  EXPECT_EQ(chunk.side, 257u);
  EXPECT_EQ(chunk.heights.size(), 257u * 257u);
  EXPECT_EQ(chunk.cell_present[3 * 8 + 5], 1);
  EXPECT_FLOAT_EQ(at(chunk, 96, 160), 160.0f);
  EXPECT_FLOAT_EQ(at(chunk, 128, 192), 160.0f);
  EXPECT_TRUE(std::isnan(at(chunk, 95, 160)));
  EXPECT_TRUE(std::isnan(at(chunk, 96, 193)));
}

// Detects: alignment must follow the worldspace southwest, or every DLC03FarHarbor block is rejected.
TEST(LodTerrainHeightfield, AcceptsAnOriginAlignedToTheWorldspaceSouthwestRatherThanToZero) {
  const auto env = load_env(world_plugin({grid_cell(0x802, -9, 5, {land(0x812, vhgt(3.0f))}),
                                          grid_cell(0x803, -73, -59, {land(0x813, vhgt(4.0f))})}));
  const auto grids = collect_land_grids(env, worldspace());
  const auto four = assemble_terrain_chunk(grids, far_harbor_settings(), 4, -9, 5);
  EXPECT_EQ(four.cell_present[0], 1);
  EXPECT_FLOAT_EQ(at(four, 0, 0), 24.0f);
  EXPECT_FLOAT_EQ(at(four, 32, 32), 24.0f);
  const auto thirty_two = assemble_terrain_chunk(grids, far_harbor_settings(), 32, -73, -59);
  EXPECT_EQ(thirty_two.side, 1025u);
  EXPECT_EQ(thirty_two.cell_present[0], 1);
  EXPECT_FLOAT_EQ(at(thirty_two, 0, 0), 32.0f);
}

// Detects: an origin that satisfies only the absolute multiple-of-level rule must be rejected.
TEST(LodTerrainHeightfield, RejectsAnOriginThatIsNotBlockAlignedToTheSouthwest) {
  const auto grids = LandGrids{};
  EXPECT_THROW(assemble_terrain_chunk(grids, far_harbor_settings(), 4, -8, 4), LodError);
  EXPECT_THROW(assemble_terrain_chunk(grids, far_harbor_settings(), 4, -9, 6), LodError);
}

// Detects: a chunk origin that is not on its own level's block grid must be rejected.
TEST(LodTerrainHeightfield, RejectsAChunkOriginThatIsNotOnItsOwnLevelGrid) {
  const auto grids = LandGrids{};
  EXPECT_THROW(assemble_terrain_chunk(grids, settings_at_origin(), 4, 2, 0), LodError);
  EXPECT_THROW(assemble_terrain_chunk(grids, settings_at_origin(), 4, 0, 3), LodError);
  EXPECT_THROW(assemble_terrain_chunk(grids, settings_at_origin(), 4, -5, 0), LodError);
  EXPECT_THROW(assemble_terrain_chunk(grids, settings_at_origin(), 8, 4, 0), LodError);
  EXPECT_THROW(assemble_terrain_chunk(grids, settings_at_origin(), 32, 16, 0), LodError);
}

// Detects: levels outside the settings range, or not a power of two, must be rejected.
TEST(LodTerrainHeightfield, RejectsLevelsOutsideTheSettingsRangeOrNotAPowerOfTwo) {
  const auto grids = LandGrids{};
  for (const std::int32_t level : {0, 2, 12, 64}) {
    EXPECT_THROW(assemble_terrain_chunk(grids, settings_at_origin(), level, 0, 0), LodError) << level;
  }
  const LodSettings high_min{0, 0, 64, 8, 32};
  EXPECT_THROW(assemble_terrain_chunk(grids, high_min, 4, 0, 0), LodError);
}

// Detects: bad CELLs, bad LANDs, missing VHGT and duplicate grids must be counted, not dropped.
TEST(LodTerrainHeightfield, CountsBadCellsBadLandMissingVhgtAndDuplicates) {
  const std::vector<std::vector<Node>> cells{
      grid_cell(0x802, 0, 0, {land(0x812, std::vector<std::byte>(100, std::byte{0}))}),
      grid_cell(0x803, 1, 0, {rec("LAND", 0x813)}),
      {rec("CELL", 0x804, {{"XCLC", std::vector<std::byte>(4, std::byte{1})}}),
       grp(0x804, 6, {grp(0x804, 9, {land(0x814, vhgt(10.0f))})})},
      {rec("CELL", 0x806, {}), grp(0x806, 6, {grp(0x806, 9, {land(0x816, vhgt(10.0f))})})},
      grid_cell(0x805, 3, 0, {land(0x815, vhgt(10.0f))}),
      grid_cell(0x807, 7, 0, {land(0x817, vhgt(10.0f))}),
      grid_cell(0x808, 7, 0, {land(0x818, vhgt(20.0f))})};
  const auto env = load_env(world_plugin(cells));
  const auto grids = collect_land_grids(env, worldspace());
  EXPECT_EQ(grids.grids.size(), 2u);
  EXPECT_EQ(grids.bad_cells, 2u);
  EXPECT_EQ(grids.bad_land, 1u);
  EXPECT_EQ(grids.missing_vhgt, 1u);
  EXPECT_EQ(grids.duplicate_cells, 1u);
  EXPECT_FLOAT_EQ(grids.grids.at({7, 0}).heights[0], 80.0f);
  const auto chunk = assemble_terrain_chunk(grids, settings_at_origin(), 8, 0, 0);
  EXPECT_EQ(chunk.side, 257u);
  std::vector<std::uint8_t> expected(64, 0);
  expected[3] = 1;
  expected[7] = 1;
  EXPECT_EQ(chunk.cell_present, expected);
  EXPECT_FLOAT_EQ(at(chunk, 0, 96), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 0, 224), 80.0f);
}

// Detects (invariant): a presence-only collection accepting a cell full collection rejects, so plan lists a tile never written.
TEST(LodTerrainHeightfield, PresenceOnlyCollectionKeepsTheSameCellsAndCounts) {
  const std::vector<std::vector<Node>> cells{
      grid_cell(0x802, 0, 0, {land(0x812, std::vector<std::byte>(100, std::byte{0}))}),
      grid_cell(0x803, 1, 0, {rec("LAND", 0x813)}),
      {rec("CELL", 0x804, {{"XCLC", std::vector<std::byte>(4, std::byte{1})}}),
       grp(0x804, 6, {grp(0x804, 9, {land(0x814, vhgt(10.0f))})})},
      {rec("CELL", 0x806, {}), grp(0x806, 6, {grp(0x806, 9, {land(0x816, vhgt(10.0f))})})},
      grid_cell(0x805, 3, 0, {land(0x815, vhgt(10.0f))}),
      grid_cell(0x807, 7, 0, {land(0x817, vhgt(10.0f))}),
      grid_cell(0x808, 7, 0, {land(0x818, vhgt(20.0f))}),
      grid_cell(0x809, 9, 0, {land(0x819, vhgt(1.0e38f))})};
  const auto env = load_env(world_plugin(cells));
  const auto full = fo4::lod::collect_world_terrain(env, worldspace());
  const auto presence = fo4::lod::collect_world_terrain(env, worldspace(), fo4::lod::LandDetail::presence);
  EXPECT_TRUE(presence.land.grids.empty());
  const std::set<std::pair<std::int32_t, std::int32_t>> expected{{3, 0}, {7, 0}};
  EXPECT_EQ(presence.land.present, expected);
  for (const auto& entry : full.land.grids) EXPECT_TRUE(presence.land.has_land(entry.first));
  EXPECT_EQ(full.land.grids.size(), presence.land.present.size());
  EXPECT_EQ(presence.land.bad_cells, full.land.bad_cells);
  EXPECT_EQ(presence.land.bad_land, full.land.bad_land);
  EXPECT_EQ(presence.land.missing_vhgt, full.land.missing_vhgt);
  EXPECT_EQ(presence.land.duplicate_cells, full.land.duplicate_cells);
  EXPECT_EQ(presence.land.duplicate_cells, 1u);
  EXPECT_EQ(presence.land.bad_land, 2u);

  const fo4::lod::LodWorld world{worldspace(), "TestWorld", LodSettings{0, 0, 64, 4, 32}};
  const auto full_tiles = fo4::lod::terrain_tiles(world, full);
  const auto presence_tiles = fo4::lod::terrain_tiles(world, presence);
  ASSERT_FALSE(full_tiles.empty());
  ASSERT_EQ(presence_tiles.size(), full_tiles.size());
  for (std::size_t index = 0; index < full_tiles.size(); ++index) {
    EXPECT_EQ(fo4::lod::lod_ledger_key(presence_tiles[index]), fo4::lod::lod_ledger_key(full_tiles[index]));
  }
  EXPECT_THROW(assemble_terrain_chunk(presence.land, settings_at_origin(), 4, 0, 0), LodError);
}

// Detects: a grid whose heights are not all finite must be rejected as a bad LAND.
TEST(LodTerrainHeightfield, RejectsAGridWhoseHeightsAreNotAllFinite) {
  const auto env = load_env(world_plugin({grid_cell(0x802, 0, 0, {land(0x812, vhgt(1.0e38f))}),
                                          grid_cell(0x803, 1, 0, {land(0x813, vhgt(10.0f))})}));
  const auto grids = collect_land_grids(env, worldspace());
  EXPECT_EQ(grids.grids.size(), 1u);
  EXPECT_EQ(grids.bad_land, 1u);
  EXPECT_EQ(grids.grids.count({0, 0}), 0u);
  EXPECT_EQ(grids.grids.count({1, 0}), 1u);
}

// Detects: a non-finite VHGT offset must count as a bad LAND, not reach the height grids.
TEST(LodTerrainHeightfield, RejectsANonFiniteVhgtOffset) {
  const auto env = load_env(world_plugin(
      {grid_cell(0x802, 0, 0, {land(0x812, vhgt(std::numeric_limits<float>::quiet_NaN()))})}));
  const auto grids = collect_land_grids(env, worldspace());
  EXPECT_EQ(grids.grids.size(), 0u);
  EXPECT_EQ(grids.bad_land, 1u);
}

// Detects: caller-supplied non-finite heights must stay absent and be counted.
TEST(LodTerrainHeightfield, NonFiniteGridHeightsStayAbsentAndAreCounted) {
  LandGrids grids;
  fo4::env::LandHeightGrid grid;
  grid.heights[0] = std::numeric_limits<float>::infinity();
  grid.heights[1] = std::numeric_limits<float>::quiet_NaN();
  grid.heights[2] = 80.0f;
  grids.grids[{0, 0}] = grid;
  const auto chunk = assemble_terrain_chunk(grids, settings_at_origin(), 4, 0, 0);
  EXPECT_EQ(chunk.cell_present[0], 1);
  EXPECT_EQ(chunk.non_finite, 2u);
  EXPECT_TRUE(std::isnan(at(chunk, 0, 0)));
  EXPECT_TRUE(std::isnan(at(chunk, 0, 1)));
  EXPECT_FLOAT_EQ(at(chunk, 0, 2), 80.0f);
}

// Detects: negative grid coordinates must map by their signed value, not unsigned arithmetic.
TEST(LodTerrainHeightfield, NegativeOriginMapsCellsByTheirSignedGridCoordinates) {
  const auto env = load_env(world_plugin({grid_cell(0x802, -4, -4, {land(0x812, vhgt(1.0f))}),
                                          grid_cell(0x803, -1, -1, {land(0x813, vhgt(2.0f))})}));
  const auto chunk =
      assemble_terrain_chunk(collect_land_grids(env, worldspace()), settings_at_origin(), 4, -4, -4);
  EXPECT_EQ(chunk.cell_present,
            (std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
  EXPECT_FLOAT_EQ(at(chunk, 0, 0), 8.0f);
  EXPECT_FLOAT_EQ(at(chunk, 32, 32), 8.0f);
  EXPECT_FLOAT_EQ(at(chunk, 96, 96), 16.0f);
  EXPECT_FLOAT_EQ(at(chunk, 128, 128), 16.0f);
  EXPECT_TRUE(std::isnan(at(chunk, 64, 64)));
  EXPECT_EQ(chunk.seam_conflicts, 0u);
}

// Detects: LAND rows and columns must not be read transposed (row is y, column is x).
TEST(LodTerrainHeightfield, HeightRowsAreYAndColumnsAreX) {
  std::vector<std::int8_t> deltas(fo4::env::k_land_vertices, 0);
  deltas[33] = 2;
  const auto env = load_env(world_plugin({grid_cell(0x802, 0, 0, {land(0x812, vhgt(10.0f, deltas))})}));
  const auto chunk =
      assemble_terrain_chunk(collect_land_grids(env, worldspace()), settings_at_origin(), 4, 0, 0);
  EXPECT_FLOAT_EQ(at(chunk, 0, 0), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 0, 1), 80.0f);
  EXPECT_FLOAT_EQ(at(chunk, 1, 0), 96.0f);
  EXPECT_FLOAT_EQ(at(chunk, 32, 5), 96.0f);
}

// Detects: a worker exception lost on its own thread (terminate or silent success) instead of reaching the caller,
// and a count that is not a whole number of workers skipping or repeating an index.
TEST(LodParallelFor, RunsEveryIndexExactlyOnceForSerialAndThreadedCounts) {
  for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{257}}) {
    std::vector<std::atomic<int>> runs(count);
    parallel_for(count, [&](std::size_t index) { runs[index].fetch_add(1); });
    for (std::size_t index = 0; index < count; ++index) EXPECT_EQ(runs[index].load(), 1) << count << ":" << index;
  }
}

TEST(LodParallelFor, RethrowsAWorkerFailureOnTheCaller) {
  EXPECT_THROW(parallel_for(257, [](std::size_t index) { if (index == 100) throw std::runtime_error("worker"); }),
               std::runtime_error);
  EXPECT_THROW(parallel_for(1, [](std::size_t) { throw std::runtime_error("serial"); }), std::runtime_error);
}
