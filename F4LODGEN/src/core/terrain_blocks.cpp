#include "f4lod/terrain_blocks.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <tuple>

namespace f4lod {
namespace {

bool supported_lod_level(std::uint32_t level) noexcept {
    return level == 4U || level == 8U || level == 16U || level == 32U;
}

std::int32_t block_coordinate(
    std::int32_t cell,
    std::int32_t southwest,
    std::uint32_t level) noexcept {
    const auto delta = static_cast<std::int64_t>(cell) - static_cast<std::int64_t>(southwest);
    const auto block_delta = (delta / static_cast<std::int64_t>(level)) * static_cast<std::int64_t>(level);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(southwest) + block_delta);
}

std::string normalize_worldspace_name(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return c == '/' || c == '\\' || c == ':' || c < 0x20;
    }), value.end());
    return value;
}

} // namespace

TerrainBlockPlanningResult plan_terrain_blocks(
    const TerrainWorld& world,
    LodGridAnchor anchor,
    std::uint32_t lod_level) {
    TerrainBlockPlanningResult result;
    if (!supported_lod_level(lod_level)) {
        result.valid = false;
        result.error = "LOD level must be one of 4, 8, 16, or 32.";
        return result;
    }

    using BlockKey = std::pair<std::int32_t, std::int32_t>;
    std::map<BlockKey, TerrainBlockPlan> blocks;

    for (std::size_t index = 0; index < world.cells.size(); ++index) {
        const auto& cell = world.cells[index];
        if (cell.x < anchor.southwest_x || cell.y < anchor.southwest_y) {
            result.valid = false;
            std::ostringstream message;
            message << "Terrain cell (" << cell.x << ", " << cell.y
                    << ") lies southwest of the configured LOD grid anchor ("
                    << anchor.southwest_x << ", " << anchor.southwest_y << ").";
            result.error = message.str();
            return result;
        }

        const auto block_x = block_coordinate(cell.x, anchor.southwest_x, lod_level);
        const auto block_y = block_coordinate(cell.y, anchor.southwest_y, lod_level);
        const BlockKey key{block_x, block_y};

        auto [it, inserted] = blocks.try_emplace(key);
        if (inserted) {
            it->second.lod_level = lod_level;
            it->second.x = block_x;
            it->second.y = block_y;
        }

        it->second.cells.push_back(TerrainBlockCellRef{
            index,
            cell.x - block_x,
            cell.y - block_y,
        });
    }

    result.blocks.reserve(blocks.size());
    for (auto& [_, block] : blocks) {
        std::sort(block.cells.begin(), block.cells.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.local_y != rhs.local_y) {
                return lhs.local_y < rhs.local_y;
            }
            if (lhs.local_x != rhs.local_x) {
                return lhs.local_x < rhs.local_x;
            }
            return lhs.terrain_cell_index < rhs.terrain_cell_index;
        });
        result.blocks.push_back(std::move(block));
    }

    return result;
}

std::string terrain_btr_relative_path(
    const std::string& worldspace_name,
    const TerrainBlockPlan& block) {
    const auto safe_name = normalize_worldspace_name(worldspace_name);
    if (safe_name.empty()) {
        return {};
    }

    std::ostringstream path;
    path << "meshes/terrain/" << safe_name << '/'
         << safe_name << '.' << block.lod_level << '.' << block.x << '.' << block.y << ".btr";
    return path.str();
}

} // namespace f4lod
