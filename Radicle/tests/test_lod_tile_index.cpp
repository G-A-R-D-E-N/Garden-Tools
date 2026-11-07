#include "fo4/lod/tile_index.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

using fo4::lod::build_lod_tile_index;
using fo4::lod::find_lod_tile_grid;
using fo4::lod::LodTile;
using fo4::lod::LodTileGrid;
using fo4::lod::LodTileIndex;
using fo4::lod::LodTileKind;
using fo4::lod::lod_tile_index_contains;

namespace {

std::vector<std::string> shipped_paths() {
  return {
      "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-80.-80.BTR",
      "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-80.-64.BTR",
      "Meshes\\Terrain\\Commonwealth\\Objects\\Commonwealth.16.-80.-80.BTO",
      "Meshes\\Terrain\\DLC03FarHarbor\\DLC03FarHarbor.4.-9.5.BTR",
  };
}

}  // namespace

// Detects: an index that must group shipped paths into worldspace/kind/level grids in a stable order.
TEST(LodTileIndex, GroupsShippedPathsIntoOrderedGrids) {
  const auto paths = shipped_paths();
  const auto index = build_lod_tile_index(paths);
  ASSERT_EQ(index.grids.size(), 3u);
  EXPECT_EQ(index.grids[0].worldspace, "Commonwealth");
  EXPECT_EQ(index.grids[0].kind, LodTileKind::object);
  EXPECT_EQ(index.grids[0].level, 16);
  EXPECT_EQ(index.grids[1].worldspace, "Commonwealth");
  EXPECT_EQ(index.grids[1].kind, LodTileKind::terrain);
  EXPECT_EQ(index.grids[1].level, 16);
  EXPECT_EQ(index.grids[2].worldspace, "DLC03FarHarbor");
  EXPECT_EQ(index.grids[2].level, 4);
  EXPECT_EQ(index.unparsed_paths, 0u);
  EXPECT_EQ(index.duplicate_paths, 0u);
}

// Detects: a grid whose tiles must be sorted by coordinate instead of archive order.
TEST(LodTileIndex, SortsTilesByCoordinate) {
  const auto paths = shipped_paths();
  const auto index = build_lod_tile_index(paths);
  const auto* grid = find_lod_tile_grid(index, "Commonwealth", LodTileKind::terrain, 16);
  ASSERT_NE(grid, nullptr);
  ASSERT_EQ(grid->tiles.size(), 2u);
  EXPECT_EQ(grid->tiles[0], (LodTile{"Commonwealth", 16, -80, -80, LodTileKind::terrain}));
  EXPECT_EQ(grid->tiles[1], (LodTile{"Commonwealth", 16, -80, -64, LodTileKind::terrain}));
}

// Detects: paths that are not tile paths, which must be counted instead of silently dropped.
TEST(LodTileIndex, CountsUnparsablePaths) {
  std::vector<std::string> paths{"",
                                 "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-80.BTR",
                                 "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-80.-80.dds",
                                 "textures\\terrain\\x.dds",
                                 "Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-80.-80.BTR"};
  const auto index = build_lod_tile_index(paths);
  EXPECT_EQ(index.unparsed_paths, 4u);
  EXPECT_EQ(index.grids.size(), 1u);
}

// Detects: a repeated tile path, which must be counted and indexed once.
TEST(LodTileIndex, CountsDuplicateTilePaths) {
  std::vector<std::string> paths{"Meshes\\Terrain\\Commonwealth\\Commonwealth.16.-80.-80.BTR",
                                 "meshes/terrain/Commonwealth/Commonwealth.16.-80.-80.BTR"};
  const auto index = build_lod_tile_index(paths);
  ASSERT_EQ(index.grids.size(), 1u);
  EXPECT_EQ(index.grids[0].tiles.size(), 1u);
  EXPECT_EQ(index.duplicate_paths, 1u);
}

// Detects: lookup and containment that must answer for the indexed tile only.
TEST(LodTileIndex, FindsGridsAndAnswersContainment) {
  const auto paths = shipped_paths();
  const auto index = build_lod_tile_index(paths);
  ASSERT_NE(find_lod_tile_grid(index, "Commonwealth", LodTileKind::terrain, 16), nullptr);
  EXPECT_EQ(find_lod_tile_grid(index, "Commonwealth", LodTileKind::terrain, 8), nullptr);
  EXPECT_EQ(find_lod_tile_grid(index, "Commonwealth", LodTileKind::object, 8), nullptr);
  EXPECT_EQ(find_lod_tile_grid(index, "NukaWorld", LodTileKind::terrain, 16), nullptr);
  EXPECT_TRUE(lod_tile_index_contains(
      index, LodTile{"Commonwealth", 16, -80, -80, LodTileKind::terrain}));
  EXPECT_FALSE(lod_tile_index_contains(
      index, LodTile{"Commonwealth", 16, -80, -48, LodTileKind::terrain}));
  EXPECT_TRUE(lod_tile_index_contains(
      index, LodTile{"Commonwealth", 16, -80, -80, LodTileKind::object}));
  EXPECT_FALSE(lod_tile_index_contains(
      index, LodTile{"Commonwealth", 8, -80, -80, LodTileKind::object}));
}

// Detects: worldspace spelling that must match exactly, because the directory name is the worldspace.
TEST(LodTileIndex, KeepsWorldspaceSpellingExact) {
  std::vector<std::string> paths{"Meshes\\Terrain\\Commonwealth\\Commonwealth.4.-96.-96.BTR",
                                 "Meshes\\Terrain\\commonwealth\\commonwealth.4.-96.-96.BTR"};
  const auto index = build_lod_tile_index(paths);
  EXPECT_EQ(index.grids.size(), 2u);
}

// Detects: an empty listing that must produce an empty index with zero counters.
TEST(LodTileIndex, HandlesAnEmptyListing) {
  const auto index = build_lod_tile_index({});
  EXPECT_TRUE(index.grids.empty());
  EXPECT_EQ(index.unparsed_paths, 0u);
  EXPECT_EQ(index.duplicate_paths, 0u);
  EXPECT_EQ(find_lod_tile_grid(index, "Commonwealth", LodTileKind::terrain, 16), nullptr);
  EXPECT_FALSE(lod_tile_index_contains(
      index, LodTile{"Commonwealth", 16, 0, 0, LodTileKind::terrain}));
}
