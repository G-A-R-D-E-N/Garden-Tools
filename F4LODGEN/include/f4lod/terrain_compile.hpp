#pragma once

#include "f4lod/btr_writer.hpp"
#include "f4lod/terrain.hpp"
#include "f4lod/terrain_blocks.hpp"
#include "f4lod/terrain_profile.hpp"
#include "f4lod/terrain_quality.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace f4lod {

struct TerrainBlockCompileOptions {
    // Recovered xLODGen/LODGen path: -1 = full LOD4 geometry. Quality 0..N
    // is evaluated as vertical error in quad-local Z after dividing world Z by
    // the LOD level. LOD4 ProtectCellBorders has the recovered per-cell /4 path.
    bool use_xlodgen_quality{true};
    int quality{0};
    std::size_t max_vertices{32767};
    std::size_t max_triangles{65535};
    bool lock_cell_borders{false};
    bool regularize{false};
    // WaterDelta is applied to the regular terrain heightfield before greedy
    // Quality reduction.
    float water_delta{-1.0F};
    // FO4 xLODGen writes Skirts=256. Zero disables skirts; negative values use
    // the recovered absolute-Z mode.
    float skirts{256.0F};

    // Legacy relative simplification controls retained for experiments and
    // direct regression coverage. They are ignored when use_xlodgen_quality is true.
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
    // Compatibility name: quad-local error for the xLODGen-style path.
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
