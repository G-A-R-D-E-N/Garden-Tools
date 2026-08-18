#pragma once

#include "f4lod/btr_writer.hpp"
#include "f4lod/terrain.hpp"
#include "f4lod/terrain_blocks.hpp"
#include "f4lod/terrain_profile.hpp"
#include "f4lod/terrain_quality.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace f4lod {

struct TerrainBlockCompileOptions {
    bool use_xlodgen_quality{true};
    int quality{0};
    std::size_t max_vertices{32767U};
    std::size_t max_triangles{std::numeric_limits<std::uint32_t>::max()};
    bool lock_cell_borders{false};
    bool regularize{false};
    float water_delta{-1.0F};
    float skirts{256.0F};

    std::size_t triangle_budget{65536};
    float target_error{0.01F};

    TerrainBtrBuildOptions btr;
};

struct TerrainBlockCompileResult {
    bool valid{};
    bool renderable{};
    std::string error;
    std::filesystem::path relative_output_path;
    std::size_t source_vertices{};
    std::size_t source_triangles{};
    std::size_t output_vertices{};
    std::size_t output_triangles{};
    int requested_quality{};
    float simplification_error{};
    float simplification_error_world_units{};
    bool forced_by_limits{};
    bool optimize_unseen{};
    float water_delta{-1.0F};
    float skirts{};
    std::size_t skirt_vertices{};
    std::size_t skirt_triangles{};
    std::size_t shifted_underwater_vertices{};
    std::size_t underwater_triangles{};
    std::size_t greedy_insertions{};
    TerrainSimplifiedMesh mesh;
    TerrainBtrDocument document;
};

[[nodiscard]] TerrainBlockCompileOptions terrain_block_options_from_level_profile(
    const std::string& worldspace_editor_id,
    const TerrainBlockPlan& plan,
    const TerrainLodLevelProfile& level);

[[nodiscard]] TerrainBlockCompileOptions default_v01_terrain_block_options(
    const std::string& worldspace_editor_id,
    const TerrainBlockPlan& plan);

[[nodiscard]] TerrainBlockCompileResult compile_terrain_block_in_memory(
    const TerrainWorld& world,
    const TerrainBlockPlan& plan,
    const std::string& worldspace_editor_id,
    const TerrainBlockCompileOptions& options);

} // namespace f4lod
