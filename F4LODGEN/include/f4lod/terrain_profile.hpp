#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

enum class TerrainProfileSeverity {
    warning,
    error,
};

struct TerrainLodLevelProfile {
    std::uint32_t lod_level{};
    int quality{};
    std::size_t max_vertices{32767U};
    std::size_t max_triangles{65535U};
    bool protect_cell_borders{};
    bool regularize{};
    float water_delta{-1.0F};
    // Recovered ZSHIFTLOD4/8/16/32 semantics. This is applied only as terrain
    // and water node translation; it never mutates source/generated vertices.
    float z_shift{};
};

struct TerrainLodProfile {
    std::string name;
    std::array<TerrainLodLevelProfile, 4> levels{};
};

struct TerrainProfileDiagnostic {
    TerrainProfileSeverity severity{TerrainProfileSeverity::warning};
    std::string code;
    std::string message;
    std::uint32_t lod_level{};
};

struct TerrainProfileValidation {
    bool valid{true};
    std::vector<TerrainProfileDiagnostic> diagnostics;

    [[nodiscard]] std::size_t warning_count() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
};

// Fallout 4 v0.1 defaults based on the xLODGen behavior already implemented in
// the compiler. The four levels are intentionally represented as one profile;
// they are not independent presets.
[[nodiscard]] TerrainLodProfile default_v01_terrain_lod_profile();

[[nodiscard]] const TerrainLodLevelProfile* find_terrain_lod_level(
    const TerrainLodProfile& profile,
    std::uint32_t lod_level) noexcept;

// Validates both file-format constraints and cross-level coherence. Typical
// xLODGen guidance is reported as warnings, not hard errors, because unusual
// profiles can be intentional.
[[nodiscard]] TerrainProfileValidation validate_terrain_lod_profile(
    const TerrainLodProfile& profile);

[[nodiscard]] const char* terrain_profile_severity_name(
    TerrainProfileSeverity severity) noexcept;

} // namespace f4lod
