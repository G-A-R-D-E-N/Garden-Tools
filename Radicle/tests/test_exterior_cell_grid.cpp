#include "fo4/forms/cell.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

void expect_layout(std::int32_t x,
                   std::int32_t y,
                   std::int32_t block_x,
                   std::int32_t block_y,
                   std::int32_t subblock_x,
                   std::int32_t subblock_y) {
  const auto layout = fo4::forms::exterior_cell_grid_layout(x, y);
  EXPECT_EQ(layout.cell_x, x);
  EXPECT_EQ(layout.cell_y, y);
  EXPECT_EQ(layout.block_x, block_x);
  EXPECT_EQ(layout.block_y, block_y);
  EXPECT_EQ(layout.subblock_x, subblock_x);
  EXPECT_EQ(layout.subblock_y, subblock_y);
}

}

TEST(ExteriorCellGrid, UsesFloorDivisionAcrossPositiveAndNegativeBoundaries) {
  expect_layout(0, 0, 0, 0, 0, 0);
  expect_layout(7, 7, 0, 0, 0, 0);
  expect_layout(8, 8, 0, 0, 1, 1);
  expect_layout(31, 31, 0, 0, 3, 3);
  expect_layout(32, 32, 1, 1, 4, 4);

  expect_layout(-1, -1, -1, -1, -1, -1);
  expect_layout(-8, -8, -1, -1, -1, -1);
  expect_layout(-9, -9, -1, -1, -2, -2);
  expect_layout(-31, -31, -1, -1, -4, -4);
  expect_layout(-32, -32, -1, -1, -4, -4);
  expect_layout(-33, -33, -2, -2, -5, -5);

  expect_layout(-33, 32, -2, 1, -5, 4);
}

TEST(ExteriorCellGrid, EnforcesCreationKitCoordinateRange) {
  EXPECT_TRUE(fo4::forms::exterior_cell_coordinate_valid(-110));
  EXPECT_TRUE(fo4::forms::exterior_cell_coordinate_valid(110));
  EXPECT_FALSE(fo4::forms::exterior_cell_coordinate_valid(-111));
  EXPECT_FALSE(fo4::forms::exterior_cell_coordinate_valid(111));

  EXPECT_NO_THROW(static_cast<void>(
      fo4::forms::exterior_cell_grid_layout(-110, 110)));
  EXPECT_THROW(
      static_cast<void>(fo4::forms::exterior_cell_grid_layout(-111, 0)),
      fo4::forms::CellError);
  EXPECT_THROW(
      static_cast<void>(fo4::forms::exterior_cell_grid_layout(0, 111)),
      fo4::forms::CellError);
}

TEST(ExteriorCellGrid, PacksGroupLabelAsXHigh16YLow16) {
  EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(1, 2), 0x00010002u);
  EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(-1, -2), 0xFFFFFFFEu);
  EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(-2, 4), 0xFFFE0004u);

  const auto label = fo4::forms::exterior_grid_group_label(-2, 4);
  const std::array<std::byte, 4> expected{
      std::byte{0x04}, std::byte{0x00}, std::byte{0xFE}, std::byte{0xFF}};
  EXPECT_EQ(label, expected);
}

TEST(ExteriorCellGrid, RejectsGroupCoordinatesThatCannotFitTheHeader) {
  EXPECT_THROW(
      static_cast<void>(fo4::forms::exterior_grid_group_label_u32(32768, 0)),
      fo4::forms::CellError);
  EXPECT_THROW(
      static_cast<void>(fo4::forms::exterior_grid_group_label_u32(0, -32769)),
      fo4::forms::CellError);
}
