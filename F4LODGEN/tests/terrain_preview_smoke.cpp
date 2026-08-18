#include "f4lod/terrain_preview.hpp"

#include <cassert>

int main() {
    f4lod::TerrainGenerationPlan plan;
    plan.valid = true;
    plan.worldspace_editor_id = "Commonwealth";
    plan.worldspace = {"Fallout4.esm", 0x18AA2};
    plan.terrain.complete = true;
    plan.terrain.worldspace = plan.worldspace;
    plan.terrain.default_land_height = 64.0F;
    plan.terrain.default_water_height = 20000000.0F;

    f4lod::TerrainCell cell;
    cell.cell = {"Fallout4.esm", 0x100};
    cell.land = {"Fallout4.esm", 0x200};
    cell.x = 0;
    cell.y = 0;
    cell.heightmap.valid = true;
    cell.heightmap.land = cell.land;
    cell.heightmap.heights.fill(64.0F);
    cell.heightmap.min_height = 64.0F;
    cell.heightmap.max_height = 64.0F;
    plan.terrain.cells.push_back(cell);

    f4lod::TerrainBlockPlan lod4;
    lod4.lod_level = 4;
    lod4.x = 0;
    lod4.y = 0;
    lod4.cells.push_back({0, 0, 0});

    f4lod::TerrainBlockPlan lod8;
    lod8.lod_level = 8;
    lod8.x = 0;
    lod8.y = 0;
    lod8.cells.push_back({0, 0, 0});

    plan.levels.push_back({4, {lod4}});
    plan.levels.push_back({8, {lod8}});

    auto profile = f4lod::default_v01_terrain_lod_profile();
    profile.name = "CI fast coordinated profile";
    profile.levels[0].quality = 1;
    const auto preview = f4lod::preview_terrain_generation(plan, profile);
    assert(preview.valid);
    assert(preview.worldspace_editor_id == "Commonwealth");
    assert(preview.profile_name == "CI fast coordinated profile");
    assert(preview.profile_diagnostics.empty());
    assert(preview.total_blocks == 2U);
    assert(preview.renderable_blocks == 0U);
    assert(preview.levels.size() == 2U);

    assert(preview.levels[0].lod_level == 4U);
    assert(preview.levels[0].requested_quality == 1);
    assert(preview.levels[0].protect_cell_borders);
    assert(preview.levels[0].water_delta == -1.0F);
    assert(preview.levels[0].blocks.size() == 1U);
    assert(preview.levels[0].blocks[0].source_vertices == 129U * 129U);
    assert(preview.levels[0].blocks[0].source_triangles == 128U * 128U * 2U);
    assert(preview.levels[0].blocks[0].output_vertices <= preview.levels[0].blocks[0].source_vertices);
    assert(preview.levels[0].blocks[0].requested_quality == 1);
    assert(preview.levels[0].blocks[0].simplification_error <= 0.0001F);
    assert(!preview.levels[0].blocks[0].forced_by_limits);
    assert(preview.levels[0].blocks[0].relative_output_path.generic_string() ==
           "meshes/terrain/Commonwealth/Commonwealth.4.0.0.btr");

    assert(preview.levels[1].lod_level == 8U);
    assert(preview.levels[1].requested_quality == 5);
    assert(!preview.levels[1].protect_cell_borders);
    assert(preview.levels[1].water_delta == 0.0F);
    assert(preview.levels[1].blocks[0].source_vertices == 257U * 257U);
    assert(preview.levels[1].blocks[0].requested_quality == 5);

    auto unusual_profile = profile;
    unusual_profile.name = "Fixture unusual";
    unusual_profile.levels[0].protect_cell_borders = false;
    const auto unusual = f4lod::preview_terrain_generation(plan, unusual_profile);
    assert(unusual.valid);
    assert(unusual.profile_name == "Fixture unusual");
    assert(!unusual.profile_diagnostics.empty());

    // Non-LOD4 Quality=-1 is normalized by the reference engine to effective
    // threshold 0. Preview must accept and compile it rather than rejecting the
    // profile as invalid.
    auto zero_threshold_profile = profile;
    zero_threshold_profile.levels[1].quality = -1;
    const auto zero_threshold = f4lod::preview_terrain_generation(plan, zero_threshold_profile);
    assert(zero_threshold.valid);
    assert(zero_threshold.levels[1].requested_quality == -1);
    assert(zero_threshold.levels[1].blocks[0].requested_quality == -1);
    assert(zero_threshold.levels[1].blocks[0].simplification_error <= 0.0001F);

    auto bad = plan;
    bad.valid = false;
    bad.error = "fixture";
    assert(!f4lod::preview_terrain_generation(bad, profile).valid);

    const auto defaults = f4lod::default_v01_terrain_lod_profile();
    assert(defaults.name == "F4LODGen v0.1 Balanced");

    return 0;
}
