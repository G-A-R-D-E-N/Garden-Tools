#include "f4lod/terrain_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace f4lod {
namespace {

constexpr std::array<std::uint32_t, 4> kExpectedLevels{4U, 8U, 16U, 32U};
constexpr std::size_t kMaxTerrainVertices = 32767U;

void add(
    TerrainProfileValidation& result,
    TerrainProfileSeverity severity,
    std::string code,
    std::string message,
    std::uint32_t level) {
    if (severity == TerrainProfileSeverity::error) result.valid = false;
    result.diagnostics.push_back(TerrainProfileDiagnostic{
        severity,
        std::move(code),
        std::move(message),
        level,
    });
}

int typical_quality_max(std::uint32_t level) noexcept {
    switch (level) {
    case 4U: return 5;
    case 8U:
    case 16U: return 8;
    case 32U: return 10;
    default: return 0;
    }
}

} // namespace

std::size_t TerrainProfileValidation::warning_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == TerrainProfileSeverity::warning;
        }));
}

std::size_t TerrainProfileValidation::error_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == TerrainProfileSeverity::error;
        }));
}

TerrainLodProfile default_v01_terrain_lod_profile() {
    TerrainLodProfile profile;
    profile.name = "F4LODGen v0.1 Balanced";
    profile.levels = {{
        TerrainLodLevelProfile{4U, 0, 32767U, 65535U, true, false, -1.0F},
        TerrainLodLevelProfile{8U, 5, 32767U, 65535U, false, false, 0.0F},
        TerrainLodLevelProfile{16U, 5, 32767U, 65535U, false, false, 0.0F},
        TerrainLodLevelProfile{32U, 10, 32767U, 65535U, false, false, 550.0F},
    }};
    return profile;
}

const TerrainLodLevelProfile* find_terrain_lod_level(
    const TerrainLodProfile& profile,
    std::uint32_t lod_level) noexcept {
    for (const auto& level : profile.levels) {
        if (level.lod_level == lod_level) return &level;
    }
    return nullptr;
}

TerrainProfileValidation validate_terrain_lod_profile(
    const TerrainLodProfile& profile) {
    TerrainProfileValidation result;

    if (profile.name.empty()) {
        add(result, TerrainProfileSeverity::warning,
            "profile-name-empty",
            "Terrain LOD profile has no display name.", 0U);
    }

    for (const auto expected : kExpectedLevels) {
        const auto count = static_cast<std::size_t>(std::count_if(
            profile.levels.begin(), profile.levels.end(), [expected](const auto& level) {
                return level.lod_level == expected;
            }));
        if (count == 0U) {
            add(result, TerrainProfileSeverity::error,
                "lod-level-missing",
                "Terrain LOD profile must configure LOD" + std::to_string(expected) + '.', expected);
        } else if (count > 1U) {
            add(result, TerrainProfileSeverity::error,
                "lod-level-duplicate",
                "Terrain LOD profile configures LOD" + std::to_string(expected) + " more than once.", expected);
        }
    }

    for (const auto& level : profile.levels) {
        const bool known_level = std::find(
            kExpectedLevels.begin(), kExpectedLevels.end(), level.lod_level) != kExpectedLevels.end();
        if (!known_level) {
            add(result, TerrainProfileSeverity::error,
                "lod-level-unsupported",
                "Terrain LOD level must be one of 4, 8, 16, or 32.", level.lod_level);
            continue;
        }

        if (level.quality < -1) {
            add(result, TerrainProfileSeverity::error,
                "quality-invalid",
                "Quality must be -1 or a non-negative quad-local Z-deviation threshold.", level.lod_level);
        }
        if (level.quality > typical_quality_max(level.lod_level)) {
            add(result, TerrainProfileSeverity::warning,
                "quality-above-typical-range",
                "Quality is above the typical xLODGen guidance range for this LOD level; distant terrain may lose too much detail.",
                level.lod_level);
        }
        if (level.max_vertices < 4U || level.max_vertices > kMaxTerrainVertices) {
            add(result, TerrainProfileSeverity::error,
                "maxverts-invalid",
                "MaxVerts must be between 4 and 32767 until 65535-vertex BTR chunk splitting is enabled.",
                level.lod_level);
        }
        if (level.max_triangles == 0U) {
            add(result, TerrainProfileSeverity::error,
                "maxtriangles-invalid",
                "Terrain triangle safety ceiling must be greater than zero.", level.lod_level);
        }
        if (!std::isfinite(level.water_delta) || level.water_delta < -1.0F) {
            add(result, TerrainProfileSeverity::error,
                "waterdelta-invalid",
                "WaterDelta must be finite and at least -1.", level.lod_level);
        }

        if (level.lod_level == 4U) {
            if (!level.protect_cell_borders && level.quality != -1) {
                add(result, TerrainProfileSeverity::warning,
                    "lod4-protect-borders-disabled",
                    "LOD4 normally protects cell borders to avoid visible terrain drops at the nearest LOD transition.",
                    level.lod_level);
            }
            if (level.water_delta >= 0.0F) {
                add(result, TerrainProfileSeverity::warning,
                    "lod4-optimize-unseen-enabled",
                    "Optimize Unseen/WaterDelta is normally left off for LOD4.", level.lod_level);
            }
        }
        if (level.lod_level == 32U && level.water_delta == 0.0F) {
            add(result, TerrainProfileSeverity::warning,
                "lod32-waterdelta-zero",
                "LOD32 is also used for the map; WaterDelta=0 actively flattens interior underwater samples rather than disabling Optimize Unseen.",
                level.lod_level);
        }
    }

    const auto* lod4 = find_terrain_lod_level(profile, 4U);
    const auto* lod8 = find_terrain_lod_level(profile, 8U);
    const auto* lod16 = find_terrain_lod_level(profile, 16U);
    const auto* lod32 = find_terrain_lod_level(profile, 32U);
    if (lod4 && lod8 && lod16) {
        const int q4 = lod4->quality < 0 ? 0 : lod4->quality;
        const int q8 = lod8->quality < 0 ? 0 : lod8->quality;
        const int q16 = lod16->quality < 0 ? 0 : lod16->quality;
        if (q8 < q4) {
            add(result, TerrainProfileSeverity::warning,
                "lod8-higher-quality-than-lod4",
                "LOD8 uses a lower effective Quality threshold than LOD4. This is valid but unusual because farther terrain is being refined more aggressively.", 8U);
        }
        if (q16 < q8) {
            add(result, TerrainProfileSeverity::warning,
                "lod16-higher-quality-than-lod8",
                "LOD16 uses a lower effective Quality threshold than LOD8. This is valid but may be an accidental cross-level mismatch.", 16U);
        }
        if (q16 - q8 > 8) {
            add(result, TerrainProfileSeverity::warning,
                "lod16-quality-jump",
                "LOD8 to LOD16 Quality changes very sharply; inspect transitions before accepting the profile.", 16U);
        }
    }
    if (lod32 && lod16) {
        const int q32 = lod32->quality < 0 ? 0 : lod32->quality;
        const int q16 = lod16->quality < 0 ? 0 : lod16->quality;
        if (q32 < q16) {
            add(result, TerrainProfileSeverity::warning,
                "lod32-higher-quality-than-lod16",
                "LOD32 is map/farthest terrain and is configured with a lower effective Quality threshold than LOD16. This may be intentional for map coastline detail.", 32U);
        }
    }

    return result;
}

const char* terrain_profile_severity_name(
    TerrainProfileSeverity severity) noexcept {
    switch (severity) {
    case TerrainProfileSeverity::warning: return "warning";
    case TerrainProfileSeverity::error: return "error";
    }
    return "warning";
}

} // namespace f4lod
