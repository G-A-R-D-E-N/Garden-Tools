#include "f4lod/terrain_compile.hpp"

#include <cassert>

int main() {
    f4lod::TerrainWorld world;
    world.complete = true;
    world.worldspace = {"Fallout4.esm", 0x18AA2};
    world.default_land_height = 64.0F;
    world.default_water_height = 20000000.0F; // recovered no-water range (> 2^24)

    f4lod::TerrainCell cell;
    cell.cell = {"Fallout4.esm", 0x100};
    cell.land = {"Fallout4.esm", 0x200};
    cell.x = -4;
    cell.y = 8;
    cell.heightmap.valid = true;
    cell.heightmap.land = cell.land;
    cell.heightmap.min_height = 64.0F;
    cell.heightmap.max_height = 64.0F;
    cell.heightmap.heights.fill(64.0F);
    world.cells.push_back(cell);

    f4lod::TerrainBlockPlan plan;
    plan.lod_level = 4;
    plan.x = -4;
    plan.y = 8;
    plan.cells.push_back({0, 0, 0});

    auto options = f4lod::default_v01_terrain_block_options("Commonwealth", plan);
    assert(options.use_xlodgen_quality);
    assert(options.quality == 0);
    assert(options.max_vertices == 32767U);
    assert(options.lock_cell_borders);

    // Compile at non-zero Quality so the smoke test validates the normal greedy
    // path independently of the special LOD4 full-geometry mode.
    auto fast_options = options;
    fast_options.quality = 1;
    const auto compiled = f4lod::compile_terrain_block_in_memory(
        world, plan, "Commonwealth", fast_options);
    assert(compiled.valid);
    assert(!compiled.renderable);
    assert(compiled.relative_output_path.generic_string() ==
           "meshes/terrain/Commonwealth/Commonwealth.4.-4.8.btr");
    assert(compiled.source_vertices == 129U * 129U);
    assert(compiled.source_triangles == 128U * 128U * 2U);
    assert(compiled.output_vertices <= compiled.source_vertices);
    assert(compiled.output_triangles <= compiled.source_triangles);
    assert(compiled.requested_quality == 1);
    assert(compiled.simplification_error <= 0.0001F);
    assert(!compiled.forced_by_limits);
    assert(compiled.document.valid);
    assert(!compiled.document.renderable);
    assert(compiled.document.nif.blocks.size() == 4U);
    assert(compiled.document.nif.blocks[0].type == "BSMultiBoundNode");
    assert(compiled.document.nif.blocks[1].type == "BSTriShape");

    auto full = options;
    full.quality = -1;
    const auto full_compiled = f4lod::compile_terrain_block_in_memory(
        world, plan, "Commonwealth", full);
    assert(full_compiled.valid);
    assert(full_compiled.output_vertices == 129U * 129U);
    assert(full_compiled.output_triangles == 128U * 128U * 2U);
    assert(full_compiled.simplification_error == 0.0F);

    // LOD4 Quality=-1 bypasses the greedy point target in the reference
    // engine. A deliberately tiny MaxVerts setting must not reject the full
    // pre-triangulated LOD4 path as long as the current writer can hold it.
    auto full_tiny_target = full;
    full_tiny_target.max_vertices = 512U;
    const auto full_tiny_compiled = f4lod::compile_terrain_block_in_memory(
        world, plan, "Commonwealth", full_tiny_target);
    assert(full_tiny_compiled.valid);
    assert(full_tiny_compiled.output_vertices == 129U * 129U);

    f4lod::TerrainBlockPlan lod8 = plan;
    lod8.lod_level = 8;
    const auto lod8_defaults = f4lod::default_v01_terrain_block_options("Commonwealth", lod8);
    assert(lod8_defaults.quality == 5);
    assert(!lod8_defaults.lock_cell_borders);

    // Reference Program.cs clamps non-LOD4 -1 to TerrainLOD's constructor
    // threshold 0. It does not reject the setting and does not use LOD4's
    // pre-triangulated full-geometry path.
    auto zero_threshold_lod8 = lod8_defaults;
    zero_threshold_lod8.quality = -1;
    const auto lod8_quality_minus_one = f4lod::compile_terrain_block_in_memory(
        world, lod8, "Commonwealth", zero_threshold_lod8);
    assert(lod8_quality_minus_one.valid);
    assert(lod8_quality_minus_one.requested_quality == -1);
    assert(lod8_quality_minus_one.simplification_error <= 0.0001F);
    assert(lod8_quality_minus_one.output_vertices < 257U * 257U);

    auto tiny_budget = fast_options;
    tiny_budget.use_xlodgen_quality = false;
    tiny_budget.triangle_budget = 1U;
    tiny_budget.target_error = 1.0F;
    const auto simplified = f4lod::compile_terrain_block_in_memory(
        world, plan, "Commonwealth", tiny_budget);
    assert(simplified.valid || !simplified.error.empty());

    auto invalid = fast_options;
    invalid.use_xlodgen_quality = false;
    invalid.target_error = 2.0F;
    assert(!f4lod::compile_terrain_block_in_memory(
        world, plan, "Commonwealth", invalid).valid);

    return 0;
}
