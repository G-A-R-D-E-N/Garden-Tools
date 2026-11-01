#include "fo4/lod/object_merge.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using fo4::lod::k_shipped_max_shapes_per_object_tile;
using fo4::lod::LodObject;
using fo4::lod::LodObjectPlan;
using fo4::lod::LodSettings;
using fo4::lod::plan_object_lod;

namespace {

constexpr double k_cell = 4096.0;

LodSettings commonwealth() {
  return LodSettings{-96, -96, 256, 4, 32};
}

LodObject at(double x, double y, std::string mesh = "mesh") { return LodObject{x, y, std::move(mesh)}; }

}  // namespace

// Detects: a placement that must map a world position to the measured cell-coordinate tile grid.
TEST(LodObjectPlan, MapsObjectsToTheMeasuredTileGrid) {
  const auto objects = std::vector<LodObject>{at(0.0, 0.0), at(-96.0 * k_cell, -96.0 * k_cell),
                                              at(-96.0 * k_cell + 3 * 16 * k_cell + 1.0,
                                                 -96.0 * k_cell + 5 * 16 * k_cell + 1.0)};
  const auto plan = plan_object_lod(objects, commonwealth(), 16);
  ASSERT_EQ(plan.tiles.size(), 3u);
  EXPECT_EQ(plan.tiles[0].x, -96);
  EXPECT_EQ(plan.tiles[0].y, -96);
  EXPECT_EQ(plan.tiles[1].x, -96 + 3 * 16);
  EXPECT_EQ(plan.tiles[1].y, -96 + 5 * 16);
  EXPECT_EQ(plan.tiles[2].x, 0);
  EXPECT_EQ(plan.tiles[2].y, 0);
  EXPECT_EQ(plan.tiles[0].objects, (std::vector<std::size_t>{1}));
  EXPECT_EQ(plan.tiles[1].objects, (std::vector<std::size_t>{2}));
  EXPECT_EQ(plan.tiles[2].objects, (std::vector<std::size_t>{0}));
}

// Detects: a floor division that must round a position southwest of the origin to the tile below it.
TEST(LodObjectPlan, UsesFloorDivisionForNegativeOffsets) {
  const auto objects = std::vector<LodObject>{at(-96.0 * k_cell - 1.0, -96.0 * k_cell - 1.0)};
  const auto plan = plan_object_lod(objects, commonwealth(), 16);
  ASSERT_EQ(plan.tiles.size(), 1u);
  EXPECT_EQ(plan.tiles[0].x, -96 - 16);
  EXPECT_EQ(plan.tiles[0].y, -96 - 16);
}

// Detects: a batch that must split once it passes the measured twelve-shape tile cap.
TEST(LodObjectPlan, SplitsWhenTheBatchExceedsTheMeasuredCap) {
  std::vector<LodObject> objects;
  for (int index = 0; index < 13; ++index) objects.push_back(at(0.0, 0.0));
  const auto plan = plan_object_lod(objects, commonwealth(), 16);
  ASSERT_EQ(plan.tiles.size(), 2u);
  EXPECT_EQ(plan.tiles[0].objects.size(), k_shipped_max_shapes_per_object_tile);
  EXPECT_EQ(plan.tiles[1].objects.size(), 1u);
  EXPECT_EQ(plan.split_tiles, 1u);
  EXPECT_EQ(plan.tiles[0].x, plan.tiles[1].x);
  EXPECT_EQ(plan.tiles[0].y, plan.tiles[1].y);
}

// Detects: an ordering that must sort tiles by coordinate while keeping the input object order inside a tile.
TEST(LodObjectPlan, OrdersTilesAndKeepsObjectOrder) {
  const auto objects = std::vector<LodObject>{at(0.0, 0.0, "b"), at(0.0, 0.0, "a"),
                                              at(-96.0 * k_cell, -96.0 * k_cell, "c")};
  const auto plan = plan_object_lod(objects, commonwealth(), 16);
  ASSERT_EQ(plan.tiles.size(), 2u);
  EXPECT_EQ(plan.tiles[0].y, -96);
  EXPECT_EQ(plan.tiles[0].objects, (std::vector<std::size_t>{2}));
  EXPECT_EQ(plan.tiles[1].y, 0);
  EXPECT_EQ(plan.tiles[1].objects, (std::vector<std::size_t>{0, 1}));
}

// Detects: a non-finite position that must be dropped and counted instead of mapped into a tile.
TEST(LodObjectPlan, DropsNonFinitePositions) {
  const auto objects = std::vector<LodObject>{
      at(std::numeric_limits<double>::quiet_NaN(), 0.0),
      at(0.0, std::numeric_limits<double>::infinity()), at(0.0, 0.0)};
  const auto plan = plan_object_lod(objects, commonwealth(), 16);
  EXPECT_EQ(plan.dropped_objects, 2u);
  ASSERT_EQ(plan.tiles.size(), 1u);
  EXPECT_EQ(plan.tiles[0].objects, (std::vector<std::size_t>{2}));
}

// Detects: levels and caps that must be refused instead of producing an unusable grid.
TEST(LodObjectPlan, RejectsInvalidLevelsAndCaps) {
  const auto objects = std::vector<LodObject>{at(0.0, 0.0)};
  EXPECT_THROW(plan_object_lod(objects, commonwealth(), 3), std::invalid_argument);
  EXPECT_THROW(plan_object_lod(objects, commonwealth(), 2), std::invalid_argument);
  EXPECT_THROW(plan_object_lod(objects, commonwealth(), 64), std::invalid_argument);
  EXPECT_THROW(plan_object_lod(objects, commonwealth(), 16, 0), std::invalid_argument);
}

// Detects: an empty listing that must produce an empty plan with zero counters.
TEST(LodObjectPlan, HandlesAnEmptyListing) {
  const auto plan = plan_object_lod({}, commonwealth(), 16);
  EXPECT_TRUE(plan.tiles.empty());
  EXPECT_EQ(plan.split_tiles, 0u);
  EXPECT_EQ(plan.dropped_objects, 0u);
}
