#pragma once

#include "f4lod/terrain_job.hpp"
#include "f4lod/terrain_profile.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainBlockPreview {
    std::uint32_t lod_level{};
    std::int32_t block_x{};
    std::int32_t block_y{};
    std::filesystem::path relative_output_path;
    std::size_t source_vertices{};
    std::size_t source_triangles{};
    std::size_t output_vertices{};
    std::size_t output_triangles{};
    int requested_quality{};
    float simplification_error{}; // quad-local error
    float simplification_error_world_units{};
    std::size_t greedy_insertions{};
    bool forced_by_limits{};
    bool optimize_unseen{};
    float water_delta{-1.0F};
    std::size_t shifted_underwater_vertices{};
    std::size_t underwater_triangles{};
    bool renderable{};
};

struct TerrainLevelPreview {
    std::uint32_t lod_level{};
    std::size_t source_vertices{};
    std::size_t source_triangles{};
    std::size_t output_vertices{};
    std::size_t output_triangles{};
    int requested_quality{};
    float water_delta{-1.0F};
    bool protect_cell_borders{};
    float max_simplification_error{}; // quad-local
    float max_simplification_error_world_units{};
    std::size_t greedy_insertions{};
    std::size_t forced_blocks{};
    std::size_t optimize_unseen_blocks{};
    std::size_t shifted_underwater_vertices{};
    std::size_t underwater_triangles{};
    std::vector<TerrainBlockPreview> blocks;
};

struct TerrainGenerationPreview {
    bool valid{};
    std::string error;
    std::string worldspace_editor_id;
    std::string profile_name;
    std::vector<TerrainProfileDiagnostic> profile_diagnostics;
    std::vector<TerrainLevelPreview> levels;
    std::size_t total_blocks{};
    std::size_t renderable_blocks{};
    std::size_t greedy_insertions{};
    std::size_t forced_blocks{};
    std::size_t optimize_unseen_blocks{};
    std::size_t shifted_underwater_vertices{};
};

[[nodiscard]] TerrainGenerationPreview preview_terrain_generation(
    const TerrainGenerationPlan& plan,
    const TerrainLodProfile& profile);

[[nodiscard]] TerrainGenerationPreview preview_terrain_generation(
    const TerrainGenerationPlan& plan);

} // namespace f4lod
