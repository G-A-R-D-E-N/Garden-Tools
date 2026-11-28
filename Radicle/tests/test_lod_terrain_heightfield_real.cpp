#include "fo4/environment/environment.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/terrain_heightfield.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

// Detects: the Commonwealth LAND set must not drift and chunk heights must match their grids.
TEST(LodTerrainHeightfieldReal, AssemblesCommonwealthHeightsFromFallout4Master) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path master = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;
  const std::vector<std::filesystem::path> paths{master};
  const auto env = fo4::env::Environment::load(paths);

  const auto world = fo4::FormKey::parse("00003C:Fallout4.esm");
  const auto land = fo4::lod::collect_land_grids(env, world);
  const auto cells = env.worldspace_cells(world).size();
  using CellKey = std::pair<std::int32_t, std::int32_t>;
  std::cout << "Commonwealth exterior cells " << cells << ", grids " << land.grids.size()
            << ", bad CELL " << land.bad_cells << ", bad LAND " << land.bad_land
            << ", LAND without VHGT " << land.missing_vhgt << ", duplicate grids "
            << land.duplicate_cells << '\n';
  EXPECT_EQ(land.grids.size(), 36864u);
  EXPECT_LE(land.grids.size() + land.bad_cells + land.bad_land + land.missing_vhgt, cells);

  const auto origin = land.grids.find(CellKey{0, 0});
  ASSERT_NE(origin, land.grids.end()) << "the Commonwealth master should carry a LAND at cell 0,0";
  constexpr std::size_t k_side = fo4::env::k_land_grid;
  EXPECT_FLOAT_EQ(origin->second.heights[0], 512.0f);
  EXPECT_FLOAT_EQ(origin->second.heights[1], 536.0f);
  EXPECT_FLOAT_EQ(origin->second.heights[2], 544.0f);
  EXPECT_FLOAT_EQ(origin->second.heights[3], 544.0f);
  EXPECT_FLOAT_EQ(origin->second.heights[16 * k_side + 16], 664.0f);

  // Invariant: the measured Commonwealth lodsettings southwest corner anchors the block grid.
  const fo4::lod::LodSettings commonwealth{-96, -96, 256, 4, 32};

  std::size_t pairs = 0;
  std::size_t mismatched_pairs = 0;
  for (const auto& [key, grid] : land.grids) {
    const auto [x, y] = key;
    if (const auto east = land.grids.find(CellKey{x + 1, y}); east != land.grids.end()) {
      ++pairs;
      bool differs = false;
      for (std::size_t row = 0; row < k_side; ++row) {
        if (std::fabs(grid.heights[row * k_side + k_side - 1] - east->second.heights[row * k_side]) > 0.01f) {
          differs = true;
        }
      }
      if (differs) ++mismatched_pairs;
    }
    if (const auto north = land.grids.find(CellKey{x, y + 1}); north != land.grids.end()) {
      ++pairs;
      bool differs = false;
      for (std::size_t column = 0; column < k_side; ++column) {
        if (std::fabs(grid.heights[(k_side - 1) * k_side + column] - north->second.heights[column]) > 0.01f) {
          differs = true;
        }
      }
      if (differs) ++mismatched_pairs;
    }
  }
  std::cout << "neighbour pairs " << pairs << ", pairs disagreeing on the shared edge " << mismatched_pairs
            << '\n';
  EXPECT_EQ(pairs, 73344u);
  EXPECT_EQ(mismatched_pairs, 649u);

  const auto chunk = fo4::lod::assemble_terrain_chunk(land, commonwealth, 32, 0, 0);
  EXPECT_EQ(chunk.side, 1025u);
  EXPECT_EQ(chunk.heights.size(), 1025u * 1025u);
  constexpr std::size_t k_cell_span = fo4::env::k_land_grid - 1;
  std::size_t present = 0;
  std::size_t height_mismatches = 0;
  for (std::int32_t cy = 0; cy < 32; ++cy) {
    for (std::int32_t cx = 0; cx < 32; ++cx) {
      const auto grid = land.grids.find(CellKey{cx, cy});
      const auto index = static_cast<std::size_t>(cy) * 32 + static_cast<std::size_t>(cx);
      EXPECT_EQ(chunk.cell_present[index] == 1, grid != land.grids.end()) << cx << ',' << cy;
      if (grid == land.grids.end()) continue;
      ++present;
      for (std::size_t row = 1; row < k_cell_span; ++row) {
        for (std::size_t column = 1; column < k_cell_span; ++column) {
          const auto chunk_index = (static_cast<std::size_t>(cy) * k_cell_span + row) * chunk.side +
                                   static_cast<std::size_t>(cx) * k_cell_span + column;
          if (chunk.heights[chunk_index] != grid->second.heights[row * k_side + column]) ++height_mismatches;
        }
      }
    }
  }
  std::cout << "level 32 chunk at (0,0): side " << chunk.side << ", present cells " << present
            << ", interior height mismatches " << height_mismatches << ", seam conflicts "
            << chunk.seam_conflicts << ", non-finite " << chunk.non_finite << ", height floats "
            << chunk.heights.size() << " (" << chunk.heights.size() * sizeof(float) << " bytes)" << '\n';
  EXPECT_EQ(present, 1024u);
  EXPECT_EQ(height_mismatches, 0u);
  EXPECT_GT(chunk.seam_conflicts, 0u);
  EXPECT_EQ(chunk.non_finite, 0u);
}
