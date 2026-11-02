// Real-data proof of the exterior block/sub-block label formula.
//
// #71's safety rule is "do not guess block/sub-block coordinate bucketing".
// The hand-written boundary cases in test_exterior_cell_grid.cpp encode the
// rule; this file checks it against every exterior CELL Bethesda ships, which
// is what turns it from a reading of xEdit's source into a measured fact.
//
// Needs the game's Data folder, so it is gated on FO4_DATA_DIR and skips
// without it. CI has no game files and will skip.

#include "fo4/forms/cell.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace {

constexpr auto k_cell = fo4::Signature::from_string("CELL");
constexpr auto k_wrld = fo4::Signature::from_string("WRLD");
constexpr auto k_xclc = fo4::Signature::from_string("XCLC");

struct GridCounts {
  std::size_t checked{0};
  std::size_t negative{0};
};

std::optional<std::pair<std::int32_t, std::int32_t>> cell_grid(const fo4::Record& record) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature != k_xclc || sub.data.size() < 8) continue;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::memcpy(&x, sub.data.data(), 4);
    std::memcpy(&y, sub.data.data() + 4, 4);
    return std::pair{x, y};
  }
  return std::nullopt;
}

// Walks the WRLD hierarchy carrying the enclosing block/sub-block labels down,
// then asserts every CELL's own XCLC reproduces them through the public API.
void check_group(const fo4::Group& group,
                 std::optional<std::uint32_t> block,
                 std::optional<std::uint32_t> subblock,
                 GridCounts& counts) {
  if (group.header.group_type == fo4::forms::k_exterior_cell_block_group_type) {
    block = group.header.label_as_u32();
  } else if (group.header.group_type == fo4::forms::k_exterior_cell_subblock_group_type) {
    subblock = group.header.label_as_u32();
  }

  for (const auto& child : group.children) {
    if (const auto* nested = std::get_if<fo4::Group>(&child)) {
      check_group(*nested, block, subblock, counts);
      continue;
    }
    const auto& record = std::get<fo4::Record>(child);
    if (record.header.signature != k_cell || !block || !subblock) continue;
    const auto grid = cell_grid(record);
    if (!grid) continue;

    const auto [x, y] = *grid;
    const auto layout = fo4::forms::exterior_cell_grid_layout(x, y);
    EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(layout.block_x, layout.block_y), *block)
        << "block label mismatch at cell " << x << "," << y;
    EXPECT_EQ(fo4::forms::exterior_grid_group_label_u32(layout.subblock_x, layout.subblock_y),
              *subblock)
        << "sub-block label mismatch at cell " << x << "," << y;

    ++counts.checked;
    if (x < 0 || y < 0) ++counts.negative;
  }
}

}  // namespace

TEST(ExteriorCellGridRealData, ReproducesShippedWorldspaceLabels) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  GridCounts counts;
  int files = 0;
  for (const char* name : {"Fallout4.esm", "DLCCoast.esm", "DLCNukaWorld.esm", "DLCRobot.esm"}) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;

    auto plugin = fo4::read_plugin_file(path);
    for (const auto& group : plugin.groups) {
      if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top) ||
          group.header.top_label() != k_wrld) {
        continue;
      }
      check_group(group, std::nullopt, std::nullopt, counts);
    }
    ++files;
  }

  if (files == 0) {
    GTEST_SKIP() << "no official masters found under " << dir;
  }

  // Guard against the walk silently finding nothing and passing vacuously.
  EXPECT_GT(counts.checked, 10000u);
  // Negative coordinates are the case floor division gets right and C
  // truncation gets wrong, so the sample has to actually contain them.
  EXPECT_GT(counts.negative, 1000u);
}
