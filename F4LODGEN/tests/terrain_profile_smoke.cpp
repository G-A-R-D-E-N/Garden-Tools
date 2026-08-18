#include "f4lod/terrain_profile.hpp"

#include <cassert>
#include <string>

int main() {
    const auto defaults = f4lod::default_v01_terrain_lod_profile();
    const auto validation = f4lod::validate_terrain_lod_profile(defaults);
    assert(validation.valid);
    assert(validation.error_count() == 0U);
    assert(validation.warning_count() == 0U);

    const auto* lod4 = f4lod::find_terrain_lod_level(defaults, 4U);
    const auto* lod8 = f4lod::find_terrain_lod_level(defaults, 8U);
    const auto* lod16 = f4lod::find_terrain_lod_level(defaults, 16U);
    const auto* lod32 = f4lod::find_terrain_lod_level(defaults, 32U);
    assert(lod4 && lod8 && lod16 && lod32);
    assert(lod4->quality == 0);
    assert(lod8->quality == 5);
    assert(lod16->quality == 5);
    assert(lod32->quality == 10);
    assert(lod4->protect_cell_borders);
    assert(lod4->water_delta == -1.0F);
    assert(lod32->water_delta == 550.0F);

    // The reference engine only gives LOD4 a dedicated full-geometry path.
    // At LOD8/16/32, -1 is accepted and normalized to effective threshold 0.
    auto normalized = defaults;
    normalized.levels[1].quality = -1;
    normalized.levels[2].quality = -1;
    normalized.levels[3].quality = -1;
    const auto normalized_result = f4lod::validate_terrain_lod_profile(normalized);
    assert(normalized_result.valid);
    assert(normalized_result.error_count() == 0U);

    auto invalid = defaults;
    invalid.levels[0].quality = -2;
    invalid.levels[2].lod_level = 8U;
    invalid.levels[3].max_vertices = 65535U;
    const auto invalid_result = f4lod::validate_terrain_lod_profile(invalid);
    assert(!invalid_result.valid);
    assert(invalid_result.error_count() >= 3U);

    auto unusual = defaults;
    unusual.levels[0].protect_cell_borders = false;
    unusual.levels[0].water_delta = 0.0F;
    unusual.levels[1].quality = 0;
    unusual.levels[2].quality = 9;
    unusual.levels[3].quality = 4;
    const auto unusual_result = f4lod::validate_terrain_lod_profile(unusual);
    assert(unusual_result.valid);
    assert(unusual_result.warning_count() >= 4U);

    assert(std::string(f4lod::terrain_profile_severity_name(
        f4lod::TerrainProfileSeverity::warning)) == "warning");
    assert(std::string(f4lod::terrain_profile_severity_name(
        f4lod::TerrainProfileSeverity::error)) == "error");

    return 0;
}
