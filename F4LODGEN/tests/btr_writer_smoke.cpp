#include "f4lod/btr_geometry.hpp"
#include "f4lod/btr_writer.hpp"
#include "f4lod/nif.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-btr-writer-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    f4lod::TerrainSimplifiedMesh mesh;
    mesh.valid = true;
    mesh.vertices = {
        {1000.0F, 2000.0F, 64.0F},
        {1128.0F, 2000.0F, 72.0F},
        {1128.0F, 2128.0F, 80.0F},
        {1000.0F, 2128.0F, 68.0F},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};

    f4lod::TerrainBtrBuildOptions options;
    options.shape_transform.translation_x = 1000.0F;
    options.shape_transform.translation_y = 2000.0F;
    options.shape_transform.translation_z = 0.0F;
    options.shape_transform.scale = 1.0F;
    options.uv_origin_x = 1000.0F;
    options.uv_origin_y = 2000.0F;
    options.uv_span_x = 128.0F;
    options.uv_span_y = 128.0F;

    const auto path = root / "GeometryOnly.btr";
    const auto write = f4lod::write_fallout4_terrain_btr(path, mesh, options);
    assert(write.valid);
    assert(!write.renderable);
    assert(write.bytes_written > 0);

    const auto btr = f4lod::inspect_fallout4_btr(path);
    assert(btr.valid);
    assert(btr.nif.blocks.size() == 4);
    assert(btr.terrain_shape == 1);
    assert(btr.nif.blocks[1].type == "BSTriShape");
    assert(btr.bounds_position[0] == 1064.0F);
    assert(btr.bounds_position[1] == 2064.0F);

    const auto geometry = f4lod::inspect_fallout4_btr_geometry(path, btr.terrain_shape);
    assert(geometry.valid);
    assert(geometry.type == "BSTriShape");
    assert(geometry.vertex_count == 4);
    assert(geometry.triangle_count == 2);
    assert(geometry.full_precision_positions);
    assert(geometry.vertex_stride == 24);
    assert(geometry.vertex_size_words == 5U);
    assert(geometry.data_size == 108);
    assert(geometry.translation[0] == 1000.0F);
    assert(geometry.translation[1] == 2000.0F);
    assert(geometry.scale == 1.0F);
    assert(geometry.position_min[0] == 0.0F);
    assert(geometry.position_max[0] == 128.0F);
    assert(geometry.position_min[1] == 0.0F);
    assert(geometry.position_max[1] == 128.0F);
    assert(geometry.vertex_flag3 == 0U);
    assert(geometry.subindex_triangle_count == 0U);
    assert(geometry.subindex_a == 0U);
    assert(geometry.shader_properties[0] == -1);

    auto half_options = options;
    half_options.position_precision = f4lod::TerrainBtrVertexPrecision::half;
    const auto half_path = root / "Half.btr";
    const auto half_write = f4lod::write_fallout4_terrain_btr(half_path, mesh, half_options);
    assert(half_write.valid);
    const auto half_btr = f4lod::inspect_fallout4_btr(half_path);
    assert(half_btr.valid);
    const auto half_geometry = f4lod::inspect_fallout4_btr_geometry(half_path, half_btr.terrain_shape);
    assert(half_geometry.valid);
    assert(!half_geometry.full_precision_positions);
    assert(half_geometry.vertex_stride == 16U);
    assert(half_geometry.vertex_size_words == 5U);
    assert(half_geometry.float_size_words == 2U);

    // Generated terrain does not use an arbitrary world transform. TerrainLOD
    // stores the mesh in 0..4096 quad-local space and applies lodLevel as the
    // node scale. ShiftZ centers the local Z values and ZSHIFT is added to the
    // node translation only.
    f4lod::TerrainSimplifiedMesh recovered_mesh;
    recovered_mesh.valid = true;
    constexpr float origin_x = -16384.0F;
    constexpr float origin_y = 32768.0F;
    recovered_mesh.vertices = {
        {origin_x,          origin_y,          64.0F},
        {origin_x + 512.0F, origin_y,          72.0F},
        {origin_x + 512.0F, origin_y + 512.0F, 80.0F},
        {origin_x,          origin_y + 512.0F, 68.0F},
    };
    recovered_mesh.indices = {0, 1, 2, 0, 2, 3};

    f4lod::TerrainBtrBuildOptions recovered;
    recovered.use_recovered_terrain_block_space = true;
    recovered.lod_level = 4U;
    recovered.block_origin_x = origin_x;
    recovered.block_origin_y = origin_y;
    recovered.z_shift = 12.0F;
    recovered.reserved_string_count = 2U;

    const auto recovered_path = root / "RecoveredBlock.btr";
    const auto recovered_write = f4lod::write_fallout4_terrain_btr(
        recovered_path, recovered_mesh, recovered);
    assert(recovered_write.valid);
    const auto recovered_btr = f4lod::inspect_fallout4_btr(recovered_path);
    assert(recovered_btr.valid);
    assert(recovered_btr.nif.strings.size() >= 2U);
    assert(recovered_btr.nif.strings[0] == "chunk");
    assert(recovered_btr.nif.strings[1] == "Land");
    assert(recovered_btr.nif.blocks[1].type == "BSTriShape");

    const auto recovered_geometry = f4lod::inspect_fallout4_btr_geometry(
        recovered_path, recovered_btr.terrain_shape);
    assert(recovered_geometry.valid);
    assert(!recovered_geometry.full_precision_positions);
    assert(recovered_geometry.vertex_size_words == 5U);
    assert(recovered_geometry.float_size_words == 2U);
    assert(std::fabs(recovered_geometry.translation[0]) < 0.001F);
    assert(std::fabs(recovered_geometry.translation[1]) < 0.001F);
    assert(std::fabs(recovered_geometry.translation[2] - 84.0F) < 0.001F);
    assert(std::fabs(recovered_geometry.scale - 4.0F) < 0.001F);
    assert(std::fabs(recovered_geometry.position_min[0]) < 0.001F);
    assert(std::fabs(recovered_geometry.position_max[0] - 128.0F) < 0.01F);
    assert(std::fabs(recovered_geometry.position_min[1]) < 0.001F);
    assert(std::fabs(recovered_geometry.position_max[1] - 128.0F) < 0.01F);
    assert(std::fabs(recovered_geometry.position_min[2] + 2.0F) < 0.01F);
    assert(std::fabs(recovered_geometry.position_max[2] - 2.0F) < 0.01F);
    assert(std::fabs(recovered_geometry.uv_min[0]) < 0.001F);
    assert(std::fabs(recovered_geometry.uv_max[0] - 0.03125F) < 0.001F);
    assert(std::fabs(recovered_geometry.uv_min[1] - 0.96875F) < 0.001F);
    assert(std::fabs(recovered_geometry.uv_max[1] - 1.0F) < 0.001F);
    assert(std::fabs(recovered_btr.bounds_position[0] - 256.0F) < 0.01F);
    assert(std::fabs(recovered_btr.bounds_position[1] - 256.0F) < 0.01F);
    assert(std::fabs(recovered_btr.bounds_position[2]) < 0.01F);

    auto material_options = options;
    material_options.additional_blocks.push_back({"BSLightingShaderProperty", {0x00}});
    material_options.shader_property_0 = 4;
    const auto material_document = f4lod::build_fallout4_terrain_btr_document(mesh, material_options);
    assert(material_document.valid);
    assert(material_document.renderable);

    auto bad_ref = options;
    bad_ref.shader_property_0 = 99;
    assert(!f4lod::build_fallout4_terrain_btr_document(mesh, bad_ref).valid);

    std::filesystem::remove_all(root);
    return 0;
}
