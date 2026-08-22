#pragma once

#include "f4lod/terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace f4lod {

struct LodGridAnchor {
    std::int32_t southwest_x{};
    std::int32_t southwest_y{};
};

struct TerrainBlockCellRef {
    std::size_t terrain_cell_index{};
    std::int32_t local_x{};
    std::int32_t local_y{};
};

struct TerrainBlockPlan {
    std::uint32_t lod_level{};
    std::int32_t x{};
    std::int32_t y{};
    std::vector<TerrainBlockCellRef> cells;
};

struct TerrainBlockPlanningResult {
    bool valid{true};
    std::string error;
    std::vector<TerrainBlockPlan> blocks;
};

[[nodiscard]] TerrainBlockPlanningResult plan_terrain_blocks(
    const TerrainWorld& world,
    LodGridAnchor anchor,
    std::uint32_t lod_level);

[[nodiscard]] std::string terrain_btr_relative_path(
    const std::string& worldspace_name,
    const TerrainBlockPlan& block);

} // namespace f4lod
